/*
 * git-imap-send - drops patches into an imap Drafts folder
 *                 derived from isync/mbsync - mailbox synchronizer
 *
 * Copyright (C) 2000-2002 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 2002-2004 Oswald Buddenhagen <ossi@users.sf.net>
 * Copyright (C) 2004 Theodore Y. Ts'o <tytso@mit.edu>
 * Copyright (C) 2006 Mike McCormack
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "git-compat-util.h"
#include "config.h"
#include "credential.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "run-command.h"
#include "parse-options.h"
#include "setup.h"
#include "strbuf.h"
#if defined(NO_OPENSSL) && !defined(HAVE_OPENSSL_CSPRNG)
typedef void *SSL;
#endif
#ifdef USE_CURL_FOR_IMAP_SEND
#include "http.h"
#endif

#if defined(USE_CURL_FOR_IMAP_SEND)
/* Always default to curl if it's available. */
#define USE_CURL_DEFAULT 1
#else
/* We don't have curl, so continue to use the historical implementation */
#define USE_CURL_DEFAULT 0
#endif

static int verbosity;
static int use_curl = USE_CURL_DEFAULT;

static const char * const imap_send_usage[] = { "git imap-send [-v] [-q] [--[no-]curl] < <mbox>", NULL };

static struct option imap_send_options[] = {
	OPT__VERBOSITY(&verbosity),
	OPT_BOOL(0, "curl", &use_curl, "use libcurl to communicate with the IMAP server"),
	OPT_END()
};

#undef DRV_OK
#define DRV_OK          0
#define DRV_MSG_BAD     -1
#define DRV_BOX_BAD     -2
#define DRV_STORE_BAD   -3

__attribute__((format (printf, 1, 2)))
static void imap_info(const char *, ...);
__attribute__((format (printf, 1, 2)))
static void imap_warn(const char *, ...);

static char *next_arg(char **);

__attribute__((format (printf, 3, 4)))
static int nfsnprintf(char *buf, int blen, const char *fmt, ...);

static int nfvasprintf(char **strp, const char *fmt, va_list ap)
{
	int len;
	char tmp[8192];

	len = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	if (len < 0)
		die("Fatal: Out of memory");
	if (len >= sizeof(tmp))
		die("imap command overflow!");
	*strp = xmemdupz(tmp, len);
	return len;
}

struct imap_server_conf {
	const char *name;
	const char *tunnel;
	const char *host;
	int port;
	const char *folder;
	const char *user;
	const char *pass;
	int use_ssl;
	int ssl_verify;
	int use_html;
	const char *auth_method;
};

static struct imap_server_conf server = {
	.ssl_verify = 1,
};

struct imap_socket {
	int fd[2];
	SSL *ssl;
};

struct imap_buffer {
	struct imap_socket sock;
	int bytes;
	int offset;
	char buf[1024];
};

struct imap_cmd;

struct imap {
	int uidnext; /* from SELECT responses */
	unsigned caps, rcaps; /* CAPABILITY results */
	/* command queue */
	int nexttag, num_in_progress, literal_pending;
	struct imap_cmd *in_progress, **in_progress_append;
	struct imap_buffer buf; /* this is BIG, so put it last */
};

struct imap_store {
	/* currently open mailbox */
	const char *name; /* foreign! maybe preset? */
	int uidvalidity;
	struct imap *imap;
	const char *prefix;
};

struct imap_cmd_cb {
	int (*cont)(struct imap_store *ctx, const char *prompt);
	void *ctx;
	char *data;
	int dlen;
};

struct imap_cmd {
	struct imap_cmd *next;
	struct imap_cmd_cb cb;
	char *cmd;
	int tag;
};

#define CAP(cap) (imap->caps & (1 << (cap)))

enum CAPABILITY {
	NOLOGIN = 0,
	UIDPLUS,
	LITERALPLUS,
	NAMESPACE,
	STARTTLS,
	AUTH_CRAM_MD5
};

static const char *cap_list[] = {
	"LOGINDISABLED",
	"UIDPLUS",
	"LITERAL+",
	"NAMESPACE",
	"STARTTLS",
	"AUTH=CRAM-MD5",
};

#define RESP_OK    0
#define RESP_NO    1
#define RESP_BAD   2

static int get_cmd_result(struct imap_store *ctx, struct imap_cmd *tcmd);


#ifndef NO_OPENSSL
static void ssl_socket_perror(const char *func)
{
	fprintf(stderr, "%s: %s\n", func, ERR_error_string(ERR_get_error(), NULL));
}
#endif

static void socket_perror(const char *func, struct imap_socket *sock, int ret)
{
#ifndef NO_OPENSSL
	if (sock->ssl) {
		int sslerr = SSL_get_error(sock->ssl, ret);
		switch (sslerr) {
		case SSL_ERROR_NONE:
			break;
		case SSL_ERROR_SYSCALL:
			perror("SSL_connect");
			break;
		default:
			ssl_socket_perror("SSL_connect");
			break;
		}
	} else
#endif
	{
		if (ret < 0)
			perror(func);
		else
			fprintf(stderr, "%s: unexpected EOF\n", func);
	}
	/* mark as used to appease -Wunused-parameter with NO_OPENSSL */
	(void)sock;
}

#ifdef NO_OPENSSL
static int ssl_socket_connect(struct imap_socket *sock UNUSED,
			      int use_tls_only UNUSED,
			      int verify UNUSED)
{
	fprintf(stderr, "SSL requested but SSL support not compiled in\n");
	return -1;
}

#else

static int host_matches(const char *host, const char *pattern)
{
	if (pattern[0] == '*' && pattern[1] == '.') {
		pattern += 2;
		if (!(host = strchr(host, '.')))
			return 0;
		host++;
	}

	return *host && *pattern && !strcasecmp(host, pattern);
}

static int verify_hostname(X509 *cert, const char *hostname)
{
	int len;
	X509_NAME *subj;
	char cname[1000];
	int i, found;
	STACK_OF(GENERAL_NAME) *subj_alt_names;

	/* try the DNS subjectAltNames */
	found = 0;
	if ((subj_alt_names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL))) {
		int num_subj_alt_names = sk_GENERAL_NAME_num(subj_alt_names);
		for (i = 0; !found && i < num_subj_alt_names; i++) {
			GENERAL_NAME *subj_alt_name = sk_GENERAL_NAME_value(subj_alt_names, i);
			if (subj_alt_name->type == GEN_DNS &&
			    strlen((const char *)subj_alt_name->d.ia5->data) == (size_t)subj_alt_name->d.ia5->length &&
			    host_matches(hostname, (const char *)(subj_alt_name->d.ia5->data)))
				found = 1;
		}
		sk_GENERAL_NAME_pop_free(subj_alt_names, GENERAL_NAME_free);
	}
	if (found)
		return 0;

	/* try the common name */
	if (!(subj = X509_get_subject_name(cert)))
		return error("cannot get certificate subject");
	if ((len = X509_NAME_get_text_by_NID(subj, NID_commonName, cname, sizeof(cname))) < 0)
		return error("cannot get certificate common name");
	if (strlen(cname) == (size_t)len && host_matches(hostname, cname))
		return 0;
	return error("certificate owner '%s' does not match hostname '%s'",
		     cname, hostname);
}

static int ssl_socket_connect(struct imap_socket *sock, int use_tls_only, int verify)
{
#if (OPENSSL_VERSION_NUMBER >= 0x10000000L)
	const SSL_METHOD *meth;
#else
	SSL_METHOD *meth;
#endif
	SSL_CTX *ctx;
	int ret;
	X509 *cert;

	SSL_library_init();
	SSL_load_error_strings();

	meth = SSLv23_method();
	if (!meth) {
		ssl_socket_perror("SSLv23_method");
		return -1;
	}

	ctx = SSL_CTX_new(meth);
	if (!ctx) {
		ssl_socket_perror("SSL_CTX_new");
		return -1;
	}

	if (use_tls_only)
		SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

	if (verify)
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

	if (!SSL_CTX_set_default_verify_paths(ctx)) {
		ssl_socket_perror("SSL_CTX_set_default_verify_paths");
		return -1;
	}
	sock->ssl = SSL_new(ctx);
	if (!sock->ssl) {
		ssl_socket_perror("SSL_new");
		return -1;
	}
	if (!SSL_set_rfd(sock->ssl, sock->fd[0])) {
		ssl_socket_perror("SSL_set_rfd");
		return -1;
	}
	if (!SSL_set_wfd(sock->ssl, sock->fd[1])) {
		ssl_socket_perror("SSL_set_wfd");
		return -1;
	}

#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
	/*
	 * SNI (RFC4366)
	 * OpenSSL does not document this function, but the implementation
	 * returns 1 on success, 0 on failure after calling SSLerr().
	 */
	ret = SSL_set_tlsext_host_name(sock->ssl, server.host);
	if (ret != 1)
		warning("SSL_set_tlsext_host_name(%s) failed.", server.host);
#endif

	ret = SSL_connect(sock->ssl);
	if (ret <= 0) {
		socket_perror("SSL_connect", sock, ret);
		return -1;
	}

	if (verify) {
		/* make sure the hostname matches that of the certificate */
		cert = SSL_get_peer_certificate(sock->ssl);
		if (!cert)
			return error("unable to get peer certificate.");
		if (verify_hostname(cert, server.host) < 0)
			return -1;
	}

	return 0;
}
#endif

static int socket_read(struct imap_socket *sock, char *buf, int len)
{
	ssize_t n;
#ifndef NO_OPENSSL
	if (sock->ssl)
		n = SSL_read(sock->ssl, buf, len);
	else
#endif
		n = xread(sock->fd[0], buf, len);
	if (n <= 0) {
		socket_perror("read", sock, n);
		close(sock->fd[0]);
		close(sock->fd[1]);
		sock->fd[0] = sock->fd[1] = -1;
	}
	return n;
}

static int socket_write(struct imap_socket *sock, const char *buf, int len)
{
	int n;
#ifndef NO_OPENSSL
	if (sock->ssl)
		n = SSL_write(sock->ssl, buf, len);
	else
#endif
		n = write_in_full(sock->fd[1], buf, len);
	if (n != len) {
		socket_perror("write", sock, n);
		close(sock->fd[0]);
		close(sock->fd[1]);
		sock->fd[0] = sock->fd[1] = -1;
	}
	return n;
}

static void socket_shutdown(struct imap_socket *sock)
{
#ifndef NO_OPENSSL
	if (sock->ssl) {
		SSL_shutdown(sock->ssl);
		SSL_free(sock->ssl);
	}
#endif
	close(sock->fd[0]);
	close(sock->fd[1]);
}

/* simple line buffering */
static int buffer_gets(struct imap_buffer *b, char **s)
{
	int n;
	int start = b->offset;

	*s = b->buf + start;

	for (;;) {
		/* make sure we have enough data to read the \r\n sequence */
		if (b->offset + 1 >= b->bytes) {
			if (start) {
				/* shift down used bytes */
				*s = b->buf;

				assert(start <= b->bytes);
				n = b->bytes - start;

				if (n)
					memmove(b->buf, b->buf + start, n);
				b->offset -= start;
				b->bytes = n;
				start = 0;
			}

			n = socket_read(&b->sock, b->buf + b->bytes,
					 sizeof(b->buf) - b->bytes);

			if (n <= 0)
				return -1;

			b->bytes += n;
		}

		if (b->buf[b->offset] == '\r') {
			assert(b->offset + 1 < b->bytes);
			if (b->buf[b->offset + 1] == '\n') {
				b->buf[b->offset] = 0;  /* terminate the string */
				b->offset += 2; /* next line */
				if (0 < verbosity)
					puts(*s);
				return 0;
			}
		}

		b->offset++;
	}
	/* not reached */
}

__attribute__((format (printf, 1, 2)))
static void imap_info(const char *msg, ...)
{
	va_list va;

	if (0 <= verbosity) {
		va_start(va, msg);
		vprintf(msg, va);
		va_end(va);
		fflush(stdout);
	}
}

__attribute__((format (printf, 1, 2)))
static void imap_warn(const char *msg, ...)
{
	va_list va;

	if (-2 < verbosity) {
		va_start(va, msg);
		vfprintf(stderr, msg, va);
		va_end(va);
	}
}

static char *next_arg(char **s)
{
	char *ret;

	if (!s || !*s)
		return NULL;
	while (isspace((unsigned char) **s))
		(*s)++;
	if (!**s) {
		*s = NULL;
		return NULL;
	}
	if (**s == '"') {
		++*s;
		ret = *s;
		*s = strchr(*s, '"');
	} else {
		ret = *s;
		while (**s && !isspace((unsigned char) **s))
			(*s)++;
	}
	if (*s) {
		if (**s)
			*(*s)++ = 0;
		if (!**s)
			*s = NULL;
	}
	return ret;
}

__attribute__((format (printf, 3, 4)))
static int nfsnprintf(char *buf, int blen, const char *fmt, ...)
{
	int ret;
	va_list va;

	va_start(va, fmt);
	if (blen <= 0 || (unsigned)(ret = vsnprintf(buf, blen, fmt, va)) >= (unsigned)blen)
		BUG("buffer too small. Please report a bug.");
	va_end(va);
	return ret;
}

static struct imap_cmd *issue_imap_cmd(struct imap_store *ctx,
				       struct imap_cmd_cb *cb,
				       const char *fmt, va_list ap)
{
	struct imap *imap = ctx->imap;
	struct imap_cmd *cmd;
	int n, bufl;
	char buf[1024];

	cmd = xmalloc(sizeof(struct imap_cmd));
	nfvasprintf(&cmd->cmd, fmt, ap);
	cmd->tag = ++imap->nexttag;

	if (cb)
		cmd->cb = *cb;
	else
		memset(&cmd->cb, 0, sizeof(cmd->cb));

	while (imap->literal_pending)
		get_cmd_result(ctx, NULL);

	if (!cmd->cb.data)
		bufl = nfsnprintf(buf, sizeof(buf), "%d %s\r\n", cmd->tag, cmd->cmd);
	else
		bufl = nfsnprintf(buf, sizeof(buf), "%d %s{%d%s}\r\n",
				  cmd->tag, cmd->cmd, cmd->cb.dlen,
				  CAP(LITERALPLUS) ? "+" : "");

	if (0 < verbosity) {
		if (imap->num_in_progress)
			printf("(%d in progress) ", imap->num_in_progress);
		if (!starts_with(cmd->cmd, "LOGIN"))
			printf(">>> %s", buf);
		else
			printf(">>> %d LOGIN <user> <pass>\n", cmd->tag);
	}
	if (socket_write(&imap->buf.sock, buf, bufl) != bufl) {
		free(cmd->cmd);
		free(cmd);
		if (cb)
			free(cb->data);
		return NULL;
	}
	if (cmd->cb.data) {
		if (CAP(LITERALPLUS)) {
			n = socket_write(&imap->buf.sock, cmd->cb.data, cmd->cb.dlen);
			free(cmd->cb.data);
			if (n != cmd->cb.dlen ||
			    socket_write(&imap->buf.sock, "\r\n", 2) != 2) {
				free(cmd->cmd);
				free(cmd);
				return NULL;
			}
			cmd->cb.data = NULL;
		} else
			imap->literal_pending = 1;
	} else if (cmd->cb.cont)
		imap->literal_pending = 1;
	cmd->next = NULL;
	*imap->in_progress_append = cmd;
	imap->in_progress_append = &cmd->next;
	imap->num_in_progress++;
	return cmd;
}

__attribute__((format (printf, 3, 4)))
static int imap_exec(struct imap_store *ctx, struct imap_cmd_cb *cb,
		     const char *fmt, ...)
{
	va_list ap;
	struct imap_cmd *cmdp;

	va_start(ap, fmt);
	cmdp = issue_imap_cmd(ctx, cb, fmt, ap);
	va_end(ap);
	if (!cmdp)
		return RESP_BAD;

	return get_cmd_result(ctx, cmdp);
}

__attribute__((format (printf, 3, 4)))
static int imap_exec_m(struct imap_store *ctx, struct imap_cmd_cb *cb,
		       const char *fmt, ...)
{
	va_list ap;
	struct imap_cmd *cmdp;

	va_start(ap, fmt);
	cmdp = issue_imap_cmd(ctx, cb, fmt, ap);
	va_end(ap);
	if (!cmdp)
		return DRV_STORE_BAD;

	switch (get_cmd_result(ctx, cmdp)) {
	case RESP_BAD: return DRV_STORE_BAD;
	case RESP_NO: return DRV_MSG_BAD;
	default: return DRV_OK;
	}
}

static int skip_imap_list_l(char **sp, int level)
{
	char *s = *sp;

	for (;;) {
		while (isspace((unsigned char)*s))
			s++;
		if (level && *s == ')') {
			s++;
			break;
		}
		if (*s == '(') {
			/* sublist */
			s++;
			if (skip_imap_list_l(&s, level + 1))
				goto bail;
		} else if (*s == '"') {
			/* quoted string */
			s++;
			for (; *s != '"'; s++)
				if (!*s)
					goto bail;
			s++;
		} else {
			/* atom */
			for (; *s && !isspace((unsigned char)*s); s++)
				if (level && *s == ')')
					break;
		}

		if (!level)
			break;
		if (!*s)
			goto bail;
	}
	*sp = s;
	return 0;

bail:
	return -1;
}

static void skip_list(char **sp)
{
	skip_imap_list_l(sp, 0);
}

static void parse_capability(struct imap *imap, char *cmd)
{
	char *arg;
	unsigned i;

	imap->caps = 0x80000000;
	while ((arg = next_arg(&cmd)))
		for (i = 0; i < ARRAY_SIZE(cap_list); i++)
			if (!strcmp(cap_list[i], arg))
				imap->caps |= 1 << i;
	imap->rcaps = imap->caps;
}

static int parse_response_code(struct imap_store *ctx, struct imap_cmd_cb *cb,
			       char *s)
{
	struct imap *imap = ctx->imap;
	char *arg, *p;

	if (!s || *s != '[')
		return RESP_OK;		/* no response code */
	s++;
	if (!(p = strchr(s, ']'))) {
		fprintf(stderr, "IMAP error: malformed response code\n");
		return RESP_BAD;
	}
	*p++ = 0;
	arg = next_arg(&s);
	if (!arg) {
		fprintf(stderr, "IMAP error: empty response code\n");
		return RESP_BAD;
	}
	if (!strcmp("UIDVALIDITY", arg)) {
		if (!(arg = next_arg(&s)) || !(ctx->uidvalidity = atoi(arg))) {
			fprintf(stderr, "IMAP error: malformed UIDVALIDITY status\n");
			return RESP_BAD;
		}
	} else if (!strcmp("UIDNEXT", arg)) {
		if (!(arg = next_arg(&s)) || !(imap->uidnext = atoi(arg))) {
			fprintf(stderr, "IMAP error: malformed NEXTUID status\n");
			return RESP_BAD;
		}
	} else if (!strcmp("CAPABILITY", arg)) {
		parse_capability(imap, s);
	} else if (!strcmp("ALERT", arg)) {
		/* RFC2060 says that these messages MUST be displayed
		 * to the user
		 */
		for (; isspace((unsigned char)*p); p++);
		fprintf(stderr, "*** IMAP ALERT *** %s\n", p);
	} else if (cb && cb->ctx && !strcmp("APPENDUID", arg)) {
		if (!(arg = next_arg(&s)) || !(ctx->uidvalidity = atoi(arg)) ||
		    !(arg = next_arg(&s)) || !(*(int *)cb->ctx = atoi(arg))) {
			fprintf(stderr, "IMAP error: malformed APPENDUID status\n");
			return RESP_BAD;
		}
	}
	return RESP_OK;
}

static int get_cmd_result(struct imap_store *ctx, struct imap_cmd *tcmd)
{
	struct imap *imap = ctx->imap;
	struct imap_cmd *cmdp, **pcmdp;
	char *cmd;
	const char *arg, *arg1;
	int n, resp, resp2, tag;

	for (;;) {
		if (buffer_gets(&imap->buf, &cmd))
			return RESP_BAD;

		arg = next_arg(&cmd);
		if (!arg) {
			fprintf(stderr, "IMAP error: empty response\n");
			return RESP_BAD;
		}
		if (*arg == '*') {
			arg = next_arg(&cmd);
			if (!arg) {
				fprintf(stderr, "IMAP error: unable to parse untagged response\n");
				return RESP_BAD;
			}

			if (!strcmp("NAMESPACE", arg)) {
				/* rfc2342 NAMESPACE response. */
				skip_list(&cmd); /* Personal mailboxes */
				skip_list(&cmd); /* Others' mailboxes */
				skip_list(&cmd); /* Shared mailboxes */
			} else if (!strcmp("OK", arg) || !strcmp("BAD", arg) ||
				   !strcmp("NO", arg) || !strcmp("BYE", arg)) {
				if ((resp = parse_response_code(ctx, NULL, cmd)) != RESP_OK)
					return resp;
			} else if (!strcmp("CAPABILITY", arg)) {
				parse_capability(imap, cmd);
			} else if ((arg1 = next_arg(&cmd))) {
				; /*
				   * Unhandled response-data with at least two words.
				   * Ignore it.
				   *
				   * NEEDSWORK: Previously this case handled '<num> EXISTS'
				   * and '<num> RECENT' but as a probably-unintended side
				   * effect it ignores other unrecognized two-word
				   * responses.  imap-send doesn't ever try to read
				   * messages or mailboxes these days, so consider
				   * eliminating this case.
				   */
			} else {
				fprintf(stderr, "IMAP error: unable to parse untagged response\n");
				return RESP_BAD;
			}
		} else if (!imap->in_progress) {
			fprintf(stderr, "IMAP error: unexpected reply: %s %s\n", arg, cmd ? cmd : "");
			return RESP_BAD;
		} else if (*arg == '+') {
			/* This can happen only with the last command underway, as
			   it enforces a round-trip. */
			cmdp = (struct imap_cmd *)((char *)imap->in_progress_append -
			       offsetof(struct imap_cmd, next));
			if (cmdp->cb.data) {
				n = socket_write(&imap->buf.sock, cmdp->cb.data, cmdp->cb.dlen);
				FREE_AND_NULL(cmdp->cb.data);
				if (n != (int)cmdp->cb.dlen)
					return RESP_BAD;
			} else if (cmdp->cb.cont) {
				if (cmdp->cb.cont(ctx, cmd))
					return RESP_BAD;
			} else {
				fprintf(stderr, "IMAP error: unexpected command continuation request\n");
				return RESP_BAD;
			}
			if (socket_write(&imap->buf.sock, "\r\n", 2) != 2)
				return RESP_BAD;
			if (!cmdp->cb.cont)
				imap->literal_pending = 0;
			if (!tcmd)
				return DRV_OK;
		} else {
			tag = atoi(arg);
			for (pcmdp = &imap->in_progress; (cmdp = *pcmdp); pcmdp = &cmdp->next)
				if (cmdp->tag == tag)
					goto gottag;
			fprintf(stderr, "IMAP error: unexpected tag %s\n", arg);
			return RESP_BAD;
		gottag:
			if (!(*pcmdp = cmdp->next))
				imap->in_progress_append = pcmdp;
			imap->num_in_progress--;
			if (cmdp->cb.cont || cmdp->cb.data)
				imap->literal_pending = 0;
			arg = next_arg(&cmd);
			if (!arg)
				arg = "";
			if (!strcmp("OK", arg))
				resp = DRV_OK;
			else {
				if (!strcmp("NO", arg))
					resp = RESP_NO;
				else /*if (!strcmp("BAD", arg))*/
					resp = RESP_BAD;
				fprintf(stderr, "IMAP command '%s' returned response (%s) - %s\n",
					!starts_with(cmdp->cmd, "LOGIN") ?
							cmdp->cmd : "LOGIN <user> <pass>",
							arg, cmd ? cmd : "");
			}
			if ((resp2 = parse_response_code(ctx, &cmdp->cb, cmd)) > resp)
				resp = resp2;
			free(cmdp->cb.data);
			free(cmdp->cmd);
			free(cmdp);
			if (!tcmd || tcmd == cmdp)
				return resp;
		}
	}
	/* not reached */
}

static void imap_close_server(struct imap_store *ictx)
{
	struct imap *imap = ictx->imap;

	if (imap->buf.sock.fd[0] != -1) {
		imap_exec(ictx, NULL, "LOGOUT");
		socket_shutdown(&imap->buf.sock);
	}
	free(imap);
}

static void imap_close_store(struct imap_store *ctx)
{
	imap_close_server(ctx);
	free(ctx);
}

#ifndef NO_OPENSSL

/*
 * hexchar() and cram() functions are based on the code from the isync
 * project (https://isync.sourceforge.io/).
 */
static char hexchar(unsigned int b)
{
	return b < 10 ? '0' + b : 'a' + (b - 10);
}

#define ENCODED_SIZE(n) (4 * DIV_ROUND_UP((n), 3))
static char *cram(const char *challenge_64, const char *user, const char *pass)
{
	int i, resp_len, encoded_len, decoded_len;
	unsigned char hash[16];
	char hex[33];
	char *response, *response_64, *challenge;

	/*
	 * length of challenge_64 (i.e. base-64 encoded string) is a good
	 * enough upper bound for challenge (decoded result).
	 */
	encoded_len = strlen(challenge_64);
	challenge = xmalloc(encoded_len);
	decoded_len = EVP_DecodeBlock((unsigned char *)challenge,
				      (unsigned char *)challenge_64, encoded_len);
	if (decoded_len < 0)
		die("invalid challenge %s", challenge_64);
	if (!HMAC(EVP_md5(), pass, strlen(pass), (unsigned char *)challenge, decoded_len, hash, NULL))
		die("HMAC error");

	hex[32] = 0;
	for (i = 0; i < 16; i++) {
		hex[2 * i] = hexchar((hash[i] >> 4) & 0xf);
		hex[2 * i + 1] = hexchar(hash[i] & 0xf);
	}

	/* response: "<user> <digest in hex>" */
	response = xstrfmt("%s %s", user, hex);
	resp_len = strlen(response);

	response_64 = xmallocz(ENCODED_SIZE(resp_len));
	encoded_len = EVP_EncodeBlock((unsigned char *)response_64,
				      (unsigned char *)response, resp_len);
	if (encoded_len < 0)
		die("EVP_EncodeBlock error");
	return (char *)response_64;
}

#else

static char *cram(const char *challenge_64 UNUSED,
		  const char *user UNUSED,
		  const char *pass UNUSED)
{
	die("If you want to use CRAM-MD5 authenticate method, "
	    "you have to build git-imap-send with OpenSSL library.");
}

#endif

static int auth_cram_md5(struct imap_store *ctx, const char *prompt)
{
	int ret;
	char *response;

	response = cram(prompt, server.user, server.pass);

	ret = socket_write(&ctx->imap->buf.sock, response, strlen(response));
	if (ret != strlen(response))
		return error("IMAP error: sending response failed");

	free(response);

	return 0;
}

static void server_fill_credential(struct imap_server_conf *srvc, struct credential *cred)
{
	if (srvc->user && srvc->pass)
		return;

	cred->protocol = xstrdup(srvc->use_ssl ? "imaps" : "imap");
	cred->host = xstrdup(srvc->host);

	cred->username = xstrdup_or_null(srvc->user);
	cred->password = xstrdup_or_null(srvc->pass);

	credential_fill(cred);

	if (!srvc->user)
		srvc->user = xstrdup(cred->username);
	if (!srvc->pass)
		srvc->pass = xstrdup(cred->password);
}

static struct imap_store *imap_open_store(struct imap_server_conf *srvc, const char *folder)
{
	struct credential cred = CREDENTIAL_INIT;
	struct imap_store *ctx;
	struct imap *imap;
	char *arg, *rsp;
	int s = -1, preauth;

	CALLOC_ARRAY(ctx, 1);

	ctx->imap = CALLOC_ARRAY(imap, 1);
	imap->buf.sock.fd[0] = imap->buf.sock.fd[1] = -1;
	imap->in_progress_append = &imap->in_progress;

	/* open connection to IMAP server */

	if (srvc->tunnel) {
		struct child_process tunnel = CHILD_PROCESS_INIT;

		imap_info("Starting tunnel '%s'... ", srvc->tunnel);

		strvec_push(&tunnel.args, srvc->tunnel);
		tunnel.use_shell = 1;
		tunnel.in = -1;
		tunnel.out = -1;
		if (start_command(&tunnel))
			die("cannot start proxy %s", srvc->tunnel);

		imap->buf.sock.fd[0] = tunnel.out;
		imap->buf.sock.fd[1] = tunnel.in;

		imap_info("ok\n");
	} else {
#ifndef NO_IPV6
		struct addrinfo hints, *ai0, *ai;
		int gai;
		char portstr[6];

		xsnprintf(portstr, sizeof(portstr), "%d", srvc->port);

		memset(&hints, 0, sizeof(hints));
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		imap_info("Resolving %s... ", srvc->host);
		gai = getaddrinfo(srvc->host, portstr, &hints, &ai);
		if (gai) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai));
			goto bail;
		}
		imap_info("ok\n");

		for (ai0 = ai; ai; ai = ai->ai_next) {
			char addr[NI_MAXHOST];

			s = socket(ai->ai_family, ai->ai_socktype,
				   ai->ai_protocol);
			if (s < 0)
				continue;

			getnameinfo(ai->ai_addr, ai->ai_addrlen, addr,
				    sizeof(addr), NULL, 0, NI_NUMERICHOST);
			imap_info("Connecting to [%s]:%s... ", addr, portstr);

			if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
				close(s);
				s = -1;
				perror("connect");
				continue;
			}

			break;
		}
		freeaddrinfo(ai0);
#else /* NO_IPV6 */
		struct hostent *he;
		struct sockaddr_in addr;

		memset(&addr, 0, sizeof(addr));
		addr.sin_port = htons(srvc->port);
		addr.sin_family = AF_INET;

		imap_info("Resolving %s... ", srvc->host);
		he = gethostbyname(srvc->host);
		if (!he) {
			perror("gethostbyname");
			goto bail;
		}
		imap_info("ok\n");

		addr.sin_addr.s_addr = *((int *) he->h_addr_list[0]);

		s = socket(PF_INET, SOCK_STREAM, 0);

		imap_info("Connecting to %s:%hu... ", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		if (connect(s, (struct sockaddr *)&addr, sizeof(addr))) {
			close(s);
			s = -1;
			perror("connect");
		}
#endif
		if (s < 0) {
			fputs("Error: unable to connect to server.\n", stderr);
			goto bail;
		}

		imap->buf.sock.fd[0] = s;
		imap->buf.sock.fd[1] = dup(s);

		if (srvc->use_ssl &&
		    ssl_socket_connect(&imap->buf.sock, 0, srvc->ssl_verify)) {
			close(s);
			goto bail;
		}
		imap_info("ok\n");
	}

	/* read the greeting string */
	if (buffer_gets(&imap->buf, &rsp)) {
		fprintf(stderr, "IMAP error: no greeting response\n");
		goto bail;
	}
	arg = next_arg(&rsp);
	if (!arg || *arg != '*' || (arg = next_arg(&rsp)) == NULL) {
		fprintf(stderr, "IMAP error: invalid greeting response\n");
		goto bail;
	}
	preauth = 0;
	if (!strcmp("PREAUTH", arg))
		preauth = 1;
	else if (strcmp("OK", arg) != 0) {
		fprintf(stderr, "IMAP error: unknown greeting response\n");
		goto bail;
	}
	parse_response_code(ctx, NULL, rsp);
	if (!imap->caps && imap_exec(ctx, NULL, "CAPABILITY") != RESP_OK)
		goto bail;

	if (!preauth) {
#ifndef NO_OPENSSL
		if (!srvc->use_ssl && CAP(STARTTLS)) {
			if (imap_exec(ctx, NULL, "STARTTLS") != RESP_OK)
				goto bail;
			if (ssl_socket_connect(&imap->buf.sock, 1,
					       srvc->ssl_verify))
				goto bail;
			/* capabilities may have changed, so get the new capabilities */
			if (imap_exec(ctx, NULL, "CAPABILITY") != RESP_OK)
				goto bail;
		}
#endif
		imap_info("Logging in...\n");
		server_fill_credential(srvc, &cred);

		if (srvc->auth_method) {
			struct imap_cmd_cb cb;

			if (!strcmp(srvc->auth_method, "CRAM-MD5")) {
				if (!CAP(AUTH_CRAM_MD5)) {
					fprintf(stderr, "You specified "
						"CRAM-MD5 as authentication method, "
						"but %s doesn't support it.\n", srvc->host);
					goto bail;
				}
				/* CRAM-MD5 */

				memset(&cb, 0, sizeof(cb));
				cb.cont = auth_cram_md5;
				if (imap_exec(ctx, &cb, "AUTHENTICATE CRAM-MD5") != RESP_OK) {
					fprintf(stderr, "IMAP error: AUTHENTICATE CRAM-MD5 failed\n");
					goto bail;
				}
			} else {
				fprintf(stderr, "Unknown authentication method:%s\n", srvc->host);
				goto bail;
			}
		} else {
			if (CAP(NOLOGIN)) {
				fprintf(stderr, "Skipping account %s@%s, server forbids LOGIN\n",
					srvc->user, srvc->host);
				goto bail;
			}
			if (!imap->buf.sock.ssl)
				imap_warn("*** IMAP Warning *** Password is being "
					  "sent in the clear\n");
			if (imap_exec(ctx, NULL, "LOGIN \"%s\" \"%s\"", srvc->user, srvc->pass) != RESP_OK) {
				fprintf(stderr, "IMAP error: LOGIN failed\n");
				goto bail;
			}
		}
	} /* !preauth */

	if (cred.username)
		credential_approve(&cred);
	credential_clear(&cred);

	/* check the target mailbox exists */
	ctx->name = folder;
	switch (imap_exec(ctx, NULL, "EXAMINE \"%s\"", ctx->name)) {
	case RESP_OK:
		/* ok */
		break;
	case RESP_BAD:
		fprintf(stderr, "IMAP error: could not check mailbox\n");
		goto out;
	case RESP_NO:
		if (imap_exec(ctx, NULL, "CREATE \"%s\"", ctx->name) == RESP_OK) {
			imap_info("Created missing mailbox\n");
		} else {
			fprintf(stderr, "IMAP error: could not create missing mailbox\n");
			goto out;
		}
		break;
	}

	ctx->prefix = "";
	return ctx;

bail:
	if (cred.username)
		credential_reject(&cred);
	credential_clear(&cred);

 out:
	imap_close_store(ctx);
	return NULL;
}

/*
 * Insert CR characters as necessary in *msg to ensure that every LF
 * character in *msg is preceded by a CR.
 */
static void lf_to_crlf(struct strbuf *msg)
{
	char *new_msg;
	size_t i, j;
	char lastc;

	/* First pass: tally, in j, the size of the new_msg string: */
	for (i = j = 0, lastc = '\0'; i < msg->len; i++) {
		if (msg->buf[i] == '\n' && lastc != '\r')
			j++; /* a CR will need to be added here */
		lastc = msg->buf[i];
		j++;
	}

	new_msg = xmallocz(j);

	/*
	 * Second pass: write the new_msg string.  Note that this loop is
	 * otherwise identical to the first pass.
	 */
	for (i = j = 0, lastc = '\0'; i < msg->len; i++) {
		if (msg->buf[i] == '\n' && lastc != '\r')
			new_msg[j++] = '\r';
		lastc = new_msg[j++] = msg->buf[i];
	}
	strbuf_attach(msg, new_msg, j, j + 1);
}

/*
 * Store msg to IMAP.  Also detach and free the data from msg->data,
 * leaving msg->data empty.
 */
static int imap_store_msg(struct imap_store *ctx, struct strbuf *msg)
{
	struct imap *imap = ctx->imap;
	struct imap_cmd_cb cb;
	const char *prefix, *box;
	int ret;

	lf_to_crlf(msg);
	memset(&cb, 0, sizeof(cb));

	cb.dlen = msg->len;
	cb.data = strbuf_detach(msg, NULL);

	box = ctx->name;
	prefix = !strcmp(box, "INBOX") ? "" : ctx->prefix;
	ret = imap_exec_m(ctx, &cb, "APPEND \"%s%s\" ", prefix, box);
	imap->caps = imap->rcaps;
	if (ret != DRV_OK)
		return ret;

	return DRV_OK;
}

static void wrap_in_html(struct strbuf *msg)
{
	struct strbuf buf = STRBUF_INIT;
	static char *content_type = "Content-Type: text/html;\n";
	static char *pre_open = "<pre>\n";
	static char *pre_close = "</pre>\n";
	const char *body = strstr(msg->buf, "\n\n");

	if (!body)
		return; /* Headers but no body; no wrapping needed */

	body += 2;

	strbuf_add(&buf, msg->buf, body - msg->buf - 1);
	strbuf_addstr(&buf, content_type);
	strbuf_addch(&buf, '\n');
	strbuf_addstr(&buf, pre_open);
	strbuf_addstr_xml_quoted(&buf, body);
	strbuf_addstr(&buf, pre_close);

	strbuf_release(msg);
	*msg = buf;
}

static int count_messages(struct strbuf *all_msgs)
{
	int count = 0;
	char *p = all_msgs->buf;

	while (1) {
		if (starts_with(p, "From ")) {
			p = strstr(p+5, "\nFrom: ");
			if (!p) break;
			p = strstr(p+7, "\nDate: ");
			if (!p) break;
			p = strstr(p+7, "\nSubject: ");
			if (!p) break;
			p += 10;
			count++;
		}
		p = strstr(p+5, "\nFrom ");
		if (!p)
			break;
		p++;
	}
	return count;
}

/*
 * Copy the next message from all_msgs, starting at offset *ofs, to
 * msg.  Update *ofs to the start of the following message.  Return
 * true iff a message was successfully copied.
 */
static int split_msg(struct strbuf *all_msgs, struct strbuf *msg, int *ofs)
{
	char *p, *data;
	size_t len;

	if (*ofs >= all_msgs->len)
		return 0;

	data = &all_msgs->buf[*ofs];
	len = all_msgs->len - *ofs;

	if (len < 5 || !starts_with(data, "From "))
		return 0;

	p = strchr(data, '\n');
	if (p) {
		p++;
		len -= p - data;
		*ofs += p - data;
		data = p;
	}

	p = strstr(data, "\nFrom ");
	if (p)
		len = &p[1] - data;

	strbuf_add(msg, data, len);
	*ofs += len;
	return 1;
}

static int git_imap_config(const char *var, const char *val,
			   const struct config_context *ctx, void *cb)
{

	if (!strcmp("imap.sslverify", var))
		server.ssl_verify = git_config_bool(var, val);
	else if (!strcmp("imap.preformattedhtml", var))
		server.use_html = git_config_bool(var, val);
	else if (!strcmp("imap.folder", var))
		return git_config_string(&server.folder, var, val);
	else if (!strcmp("imap.user", var))
		return git_config_string(&server.user, var, val);
	else if (!strcmp("imap.pass", var))
		return git_config_string(&server.pass, var, val);
	else if (!strcmp("imap.tunnel", var))
		return git_config_string(&server.tunnel, var, val);
	else if (!strcmp("imap.authmethod", var))
		return git_config_string(&server.auth_method, var, val);
	else if (!strcmp("imap.port", var))
		server.port = git_config_int(var, val, ctx->kvi);
	else if (!strcmp("imap.host", var)) {
		if (!val) {
			git_die_config("imap.host", "Missing value for 'imap.host'");
		} else {
			if (starts_with(val, "imap:"))
				val += 5;
			else if (starts_with(val, "imaps:")) {
				val += 6;
				server.use_ssl = 1;
			}
			if (starts_with(val, "//"))
				val += 2;
			server.host = xstrdup(val);
		}
	} else
		return git_default_config(var, val, ctx, cb);

	return 0;
}

static int append_msgs_to_imap(struct imap_server_conf *server,
			       struct strbuf* all_msgs, int total)
{
	struct strbuf msg = STRBUF_INIT;
	struct imap_store *ctx = NULL;
	int ofs = 0;
	int r;
	int n = 0;

	ctx = imap_open_store(server, server->folder);
	if (!ctx) {
		fprintf(stderr, "failed to open store\n");
		return 1;
	}
	ctx->name = server->folder;

	fprintf(stderr, "sending %d message%s\n", total, (total != 1) ? "s" : "");
	while (1) {
		unsigned percent = n * 100 / total;

		fprintf(stderr, "%4u%% (%d/%d) done\r", percent, n, total);

		if (!split_msg(all_msgs, &msg, &ofs))
			break;
		if (server->use_html)
			wrap_in_html(&msg);
		r = imap_store_msg(ctx, &msg);
		if (r != DRV_OK)
			break;
		n++;
	}
	fprintf(stderr, "\n");

	imap_close_store(ctx);

	return 0;
}

#ifdef USE_CURL_FOR_IMAP_SEND
static CURL *setup_curl(struct imap_server_conf *srvc, struct credential *cred)
{
	CURL *curl;
	struct strbuf path = STRBUF_INIT;
	char *uri_encoded_folder;

	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
		die("curl_global_init failed");

	curl = curl_easy_init();

	if (!curl)
		die("curl_easy_init failed");

	server_fill_credential(srvc, cred);
	curl_easy_setopt(curl, CURLOPT_USERNAME, srvc->user);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, srvc->pass);

	strbuf_addstr(&path, srvc->use_ssl ? "imaps://" : "imap://");
	strbuf_addstr(&path, srvc->host);
	if (!path.len || path.buf[path.len - 1] != '/')
		strbuf_addch(&path, '/');

	uri_encoded_folder = curl_easy_escape(curl, srvc->folder, 0);
	if (!uri_encoded_folder)
		die("failed to encode server folder");
	strbuf_addstr(&path, uri_encoded_folder);
	curl_free(uri_encoded_folder);

	curl_easy_setopt(curl, CURLOPT_URL, path.buf);
	strbuf_release(&path);
	curl_easy_setopt(curl, CURLOPT_PORT, srvc->port);

	if (srvc->auth_method) {
#ifndef GIT_CURL_HAVE_CURLOPT_LOGIN_OPTIONS
		warning("No LOGIN_OPTIONS support in this cURL version");
#else
		struct strbuf auth = STRBUF_INIT;
		strbuf_addstr(&auth, "AUTH=");
		strbuf_addstr(&auth, srvc->auth_method);
		curl_easy_setopt(curl, CURLOPT_LOGIN_OPTIONS, auth.buf);
		strbuf_release(&auth);
#endif
	}

	if (!srvc->use_ssl)
		curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_TRY);

	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, srvc->ssl_verify);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, srvc->ssl_verify);

	curl_easy_setopt(curl, CURLOPT_READFUNCTION, fread_buffer);

	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

	if (0 < verbosity || getenv("GIT_CURL_VERBOSE"))
		http_trace_curl_no_data();
	setup_curl_trace(curl);

	return curl;
}

static int curl_append_msgs_to_imap(struct imap_server_conf *server,
				    struct strbuf* all_msgs, int total)
{
	int ofs = 0;
	int n = 0;
	struct buffer msgbuf = { STRBUF_INIT, 0 };
	CURL *curl;
	CURLcode res = CURLE_OK;
	struct credential cred = CREDENTIAL_INIT;

	curl = setup_curl(server, &cred);
	curl_easy_setopt(curl, CURLOPT_READDATA, &msgbuf);

	fprintf(stderr, "sending %d message%s\n", total, (total != 1) ? "s" : "");
	while (1) {
		unsigned percent = n * 100 / total;
		int prev_len;

		fprintf(stderr, "%4u%% (%d/%d) done\r", percent, n, total);

		prev_len = msgbuf.buf.len;
		if (!split_msg(all_msgs, &msgbuf.buf, &ofs))
			break;
		if (server->use_html)
			wrap_in_html(&msgbuf.buf);
		lf_to_crlf(&msgbuf.buf);

		curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
				 (curl_off_t)(msgbuf.buf.len-prev_len));

		res = curl_easy_perform(curl);

		if(res != CURLE_OK) {
			fprintf(stderr, "curl_easy_perform() failed: %s\n",
					curl_easy_strerror(res));
			break;
		}

		n++;
	}
	fprintf(stderr, "\n");

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	if (cred.username) {
		if (res == CURLE_OK)
			credential_approve(&cred);
		else if (res == CURLE_LOGIN_DENIED)
			credential_reject(&cred);
	}

	credential_clear(&cred);

	return res != CURLE_OK;
}
#endif

int cmd_main(int argc, const char **argv)
{
	struct strbuf all_msgs = STRBUF_INIT;
	int total;
	int nongit_ok;

	setup_git_directory_gently(&nongit_ok);
	git_config(git_imap_config, NULL);

	argc = parse_options(argc, (const char **)argv, "", imap_send_options, imap_send_usage, 0);

	if (argc)
		usage_with_options(imap_send_usage, imap_send_options);

#ifndef USE_CURL_FOR_IMAP_SEND
	if (use_curl) {
		warning("--curl not supported in this build");
		use_curl = 0;
	}
#elif defined(NO_OPENSSL)
	if (!use_curl) {
		warning("--no-curl not supported in this build");
		use_curl = 1;
	}
#endif

	if (!server.port)
		server.port = server.use_ssl ? 993 : 143;

	if (!server.folder) {
		fprintf(stderr, "no imap store specified\n");
		return 1;
	}
	if (!server.host) {
		if (!server.tunnel) {
			fprintf(stderr, "no imap host specified\n");
			return 1;
		}
		server.host = "tunnel";
	}

	/* read the messages */
	if (strbuf_read(&all_msgs, 0, 0) < 0) {
		error_errno(_("could not read from stdin"));
		return 1;
	}

	if (all_msgs.len == 0) {
		fprintf(stderr, "nothing to send\n");
		return 1;
	}

	total = count_messages(&all_msgs);
	if (!total) {
		fprintf(stderr, "no messages to send\n");
		return 1;
	}

	/* write it to the imap server */

	if (server.tunnel)
		return append_msgs_to_imap(&server, &all_msgs, total);

#ifdef USE_CURL_FOR_IMAP_SEND
	if (use_curl)
		return curl_append_msgs_to_imap(&server, &all_msgs, total);
#endif

	return append_msgs_to_imap(&server, &all_msgs, total);
}
