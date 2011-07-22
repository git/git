#include "git-compat-util.h"
#include "cache.h"
#include "pkt-line.h"
#include "quote.h"
#include "refs.h"
#include "run-command.h"
#include "remote.h"
#include "url.h"

static char *server_capabilities;

static int check_ref(const char *name, int len, unsigned int flags)
{
	if (!flags)
		return 1;

	if (len < 5 || memcmp(name, "refs/", 5))
		return 0;

	/* Skip the "refs/" part */
	name += 5;
	len -= 5;

	/* REF_NORMAL means that we don't want the magic fake tag refs */
	if ((flags & REF_NORMAL) && check_ref_format(name) < 0)
		return 0;

	/* REF_HEADS means that we want regular branch heads */
	if ((flags & REF_HEADS) && !memcmp(name, "heads/", 6))
		return 1;

	/* REF_TAGS means that we want tags */
	if ((flags & REF_TAGS) && !memcmp(name, "tags/", 5))
		return 1;

	/* All type bits clear means that we are ok with anything */
	return !(flags & ~REF_NORMAL);
}

int check_ref_type(const struct ref *ref, int flags)
{
	return check_ref(ref->name, strlen(ref->name), flags);
}

static void add_extra_have(struct extra_have_objects *extra, unsigned char *sha1)
{
	ALLOC_GROW(extra->array, extra->nr + 1, extra->alloc);
	hashcpy(&(extra->array[extra->nr][0]), sha1);
	extra->nr++;
}

/*
 * Read all the refs from the other end
 */
struct ref **get_remote_heads(int in, struct ref **list,
			      int nr_match, char **match,
			      unsigned int flags,
			      struct extra_have_objects *extra_have)
{
	*list = NULL;
	for (;;) {
		struct ref *ref;
		unsigned char old_sha1[20];
		static char buffer[1000];
		char *name;
		int len, name_len;

		len = packet_read_line(in, buffer, sizeof(buffer));
		if (!len)
			break;
		if (buffer[len-1] == '\n')
			buffer[--len] = 0;

		if (len > 4 && !prefixcmp(buffer, "ERR "))
			die("remote error: %s", buffer + 4);

		if (len < 42 || get_sha1_hex(buffer, old_sha1) || buffer[40] != ' ')
			die("protocol error: expected sha/ref, got '%s'", buffer);
		name = buffer + 41;

		name_len = strlen(name);
		if (len != name_len + 41) {
			free(server_capabilities);
			server_capabilities = xstrdup(name + name_len + 1);
		}

		if (extra_have &&
		    name_len == 5 && !memcmp(".have", name, 5)) {
			add_extra_have(extra_have, old_sha1);
			continue;
		}

		if (!check_ref(name, name_len, flags))
			continue;
		if (nr_match && !path_match(name, nr_match, match))
			continue;
		ref = alloc_ref(buffer + 41);
		hashcpy(ref->old_sha1, old_sha1);
		*list = ref;
		list = &ref->next;
	}
	return list;
}

int server_supports(const char *feature)
{
	return server_capabilities &&
		strstr(server_capabilities, feature) != NULL;
}

int path_match(const char *path, int nr, char **match)
{
	int i;
	int pathlen = strlen(path);

	for (i = 0; i < nr; i++) {
		char *s = match[i];
		int len = strlen(s);

		if (!len || len > pathlen)
			continue;
		if (memcmp(path + pathlen - len, s, len))
			continue;
		if (pathlen > len && path[pathlen - len - 1] != '/')
			continue;
		*s = 0;
		return (i + 1);
	}
	return 0;
}

enum protocol {
	PROTO_LOCAL = 1,
	PROTO_SSH,
	PROTO_GIT
};

static enum protocol get_protocol(const char *name)
{
	if (!strcmp(name, "ssh"))
		return PROTO_SSH;
	if (!strcmp(name, "git"))
		return PROTO_GIT;
	if (!strcmp(name, "git+ssh"))
		return PROTO_SSH;
	if (!strcmp(name, "ssh+git"))
		return PROTO_SSH;
	if (!strcmp(name, "file"))
		return PROTO_LOCAL;
	die("I don't handle protocol '%s'", name);
}

#define STR_(s)	# s
#define STR(s)	STR_(s)

static void get_host_and_port(char **host, const char **port)
{
	char *colon, *end;

	if (*host[0] == '[') {
		end = strchr(*host + 1, ']');
		if (end) {
			*end = 0;
			end++;
			(*host)++;
		} else
			end = *host;
	} else
		end = *host;
	colon = strchr(end, ':');

	if (colon) {
		*colon = 0;
		*port = colon + 1;
	}
}

#ifndef NO_IPV6

static const char *ai_name(const struct addrinfo *ai)
{
	static char addr[NI_MAXHOST];
	if (getnameinfo(ai->ai_addr, ai->ai_addrlen, addr, sizeof(addr), NULL, 0,
			NI_NUMERICHOST) != 0)
		strcpy(addr, "(unknown)");

	return addr;
}

/*
 * Returns a connected socket() fd, or else die()s.
 */
static int git_tcp_connect_sock(char *host, int flags)
{
	struct strbuf error_message = STRBUF_INIT;
	int sockfd = -1;
	const char *port = STR(DEFAULT_GIT_PORT);
	struct addrinfo hints, *ai0, *ai;
	int gai;
	int cnt = 0;

	get_host_and_port(&host, &port);
	if (!*port)
		port = "<none>";

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, "Looking up %s ... ", host);

	gai = getaddrinfo(host, port, &hints, &ai);
	if (gai)
		die("Unable to look up %s (port %s) (%s)", host, port, gai_strerror(gai));

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, "done.\nConnecting to %s (port %s) ... ", host, port);

	for (ai0 = ai; ai; ai = ai->ai_next) {
		sockfd = socket(ai->ai_family,
				ai->ai_socktype, ai->ai_protocol);
		if ((sockfd < 0) ||
		    (connect(sockfd, ai->ai_addr, ai->ai_addrlen) < 0)) {
			strbuf_addf(&error_message, "%s[%d: %s]: errno=%s\n",
				    host, cnt, ai_name(ai), strerror(errno));
			if (0 <= sockfd)
				close(sockfd);
			sockfd = -1;
			continue;
		}
		if (flags & CONNECT_VERBOSE)
			fprintf(stderr, "%s ", ai_name(ai));
		break;
	}

	freeaddrinfo(ai0);

	if (sockfd < 0)
		die("unable to connect to %s:\n%s", host, error_message.buf);

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, "done.\n");

	strbuf_release(&error_message);

	return sockfd;
}

#else /* NO_IPV6 */

/*
 * Returns a connected socket() fd, or else die()s.
 */
static int git_tcp_connect_sock(char *host, int flags)
{
	int sockfd = -1, saved_errno = 0;
	const char *port = STR(DEFAULT_GIT_PORT);
	char *ep;
	struct hostent *he;
	struct sockaddr_in sa;
	char **ap;
	unsigned int nport;
	int cnt;

	get_host_and_port(&host, &port);

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, "Looking up %s ... ", host);

	he = gethostbyname(host);
	if (!he)
		die("Unable to look up %s (%s)", host, hstrerror(h_errno));
	nport = strtoul(port, &ep, 10);
	if ( ep == port || *ep ) {
		/* Not numeric */
		struct servent *se = getservbyname(port,"tcp");
		if ( !se )
			die("Unknown port %s", port);
		nport = se->s_port;
	}

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, "done.\nConnecting to %s (port %s) ... ", host, port);

	for (cnt = 0, ap = he->h_addr_list; *ap; ap++, cnt++) {
		sockfd = socket(he->h_addrtype, SOCK_STREAM, 0);
		if (sockfd < 0) {
			saved_errno = errno;
			continue;
		}

		memset(&sa, 0, sizeof sa);
		sa.sin_family = he->h_addrtype;
		sa.sin_port = htons(nport);
		memcpy(&sa.sin_addr, *ap, he->h_length);

		if (connect(sockfd, (struct sockaddr *)&sa, sizeof sa) < 0) {
			saved_errno = errno;
			fprintf(stderr, "%s[%d: %s]: errno=%s\n",
				host,
				cnt,
				inet_ntoa(*(struct in_addr *)&sa.sin_addr),
				strerror(saved_errno));
			close(sockfd);
			sockfd = -1;
			continue;
		}
		if (flags & CONNECT_VERBOSE)
			fprintf(stderr, "%s ",
				inet_ntoa(*(struct in_addr *)&sa.sin_addr));
		break;
	}

	if (sockfd < 0)
		die("unable to connect a socket (%s)", strerror(saved_errno));

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, "done.\n");

	return sockfd;
}

#endif /* NO_IPV6 */


static void git_tcp_connect(int fd[2], char *host, int flags)
{
	int sockfd = git_tcp_connect_sock(host, flags);

	fd[0] = sockfd;
	fd[1] = dup(sockfd);
}


static char *git_proxy_command;

static int git_proxy_command_options(const char *var, const char *value,
		void *cb)
{
	if (!strcmp(var, "core.gitproxy")) {
		const char *for_pos;
		int matchlen = -1;
		int hostlen;
		const char *rhost_name = cb;
		int rhost_len = strlen(rhost_name);

		if (git_proxy_command)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		/* [core]
		 * ;# matches www.kernel.org as well
		 * gitproxy = netcatter-1 for kernel.org
		 * gitproxy = netcatter-2 for sample.xz
		 * gitproxy = netcatter-default
		 */
		for_pos = strstr(value, " for ");
		if (!for_pos)
			/* matches everybody */
			matchlen = strlen(value);
		else {
			hostlen = strlen(for_pos + 5);
			if (rhost_len < hostlen)
				matchlen = -1;
			else if (!strncmp(for_pos + 5,
					  rhost_name + rhost_len - hostlen,
					  hostlen) &&
				 ((rhost_len == hostlen) ||
				  rhost_name[rhost_len - hostlen -1] == '.'))
				matchlen = for_pos - value;
			else
				matchlen = -1;
		}
		if (0 <= matchlen) {
			/* core.gitproxy = none for kernel.org */
			if (matchlen == 4 &&
			    !memcmp(value, "none", 4))
				matchlen = 0;
			git_proxy_command = xmemdupz(value, matchlen);
		}
		return 0;
	}

	return git_default_config(var, value, cb);
}

static int git_use_proxy(const char *host)
{
	git_proxy_command = getenv("GIT_PROXY_COMMAND");
	git_config(git_proxy_command_options, (void*)host);
	return (git_proxy_command && *git_proxy_command);
}

static struct child_process *git_proxy_connect(int fd[2], char *host)
{
	const char *port = STR(DEFAULT_GIT_PORT);
	const char **argv;
	struct child_process *proxy;

	get_host_and_port(&host, &port);

	argv = xmalloc(sizeof(*argv) * 4);
	argv[0] = git_proxy_command;
	argv[1] = host;
	argv[2] = port;
	argv[3] = NULL;
	proxy = xcalloc(1, sizeof(*proxy));
	proxy->argv = argv;
	proxy->in = -1;
	proxy->out = -1;
	if (start_command(proxy))
		die("cannot start proxy %s", argv[0]);
	fd[0] = proxy->out; /* read from proxy stdout */
	fd[1] = proxy->in;  /* write to proxy stdin */
	return proxy;
}

#define MAX_CMD_LEN 1024

static char *get_port(char *host)
{
	char *end;
	char *p = strchr(host, ':');

	if (p) {
		long port = strtol(p + 1, &end, 10);
		if (end != p + 1 && *end == '\0' && 0 <= port && port < 65536) {
			*p = '\0';
			return p+1;
		}
	}

	return NULL;
}

static struct child_process no_fork;

/*
 * This returns a dummy child_process if the transport protocol does not
 * need fork(2), or a struct child_process object if it does.  Once done,
 * finish the connection with finish_connect() with the value returned from
 * this function (it is safe to call finish_connect() with NULL to support
 * the former case).
 *
 * If it returns, the connect is successful; it just dies on errors (this
 * will hopefully be changed in a libification effort, to return NULL when
 * the connection failed).
 */
struct child_process *git_connect(int fd[2], const char *url_orig,
				  const char *prog, int flags)
{
	char *url;
	char *host, *path;
	char *end;
	int c;
	struct child_process *conn = &no_fork;
	enum protocol protocol = PROTO_LOCAL;
	int free_path = 0;
	char *port = NULL;
	const char **arg;
	struct strbuf cmd;

	/* Without this we cannot rely on waitpid() to tell
	 * what happened to our children.
	 */
	signal(SIGCHLD, SIG_DFL);

	if (is_url(url_orig))
		url = url_decode(url_orig);
	else
		url = xstrdup(url_orig);

	host = strstr(url, "://");
	if (host) {
		*host = '\0';
		protocol = get_protocol(url);
		host += 3;
		c = '/';
	} else {
		host = url;
		c = ':';
	}

	/*
	 * Don't do destructive transforms with git:// as that
	 * protocol code does '[]' unwrapping of its own.
	 */
	if (host[0] == '[') {
		end = strchr(host + 1, ']');
		if (end) {
			if (protocol != PROTO_GIT) {
				*end = 0;
				host++;
			}
			end++;
		} else
			end = host;
	} else
		end = host;

	path = strchr(end, c);
	if (path && !has_dos_drive_prefix(end)) {
		if (c == ':') {
			protocol = PROTO_SSH;
			*path++ = '\0';
		}
	} else
		path = end;

	if (!path || !*path)
		die("No path specified. See 'man git-pull' for valid url syntax");

	/*
	 * null-terminate hostname and point path to ~ for URL's like this:
	 *    ssh://host.xz/~user/repo
	 */
	if (protocol != PROTO_LOCAL && host != url) {
		char *ptr = path;
		if (path[1] == '~')
			path++;
		else {
			path = xstrdup(ptr);
			free_path = 1;
		}

		*ptr = '\0';
	}

	/*
	 * Add support for ssh port: ssh://host.xy:<port>/...
	 */
	if (protocol == PROTO_SSH && host != url)
		port = get_port(host);

	if (protocol == PROTO_GIT) {
		/* These underlying connection commands die() if they
		 * cannot connect.
		 */
		char *target_host = xstrdup(host);
		if (git_use_proxy(host))
			conn = git_proxy_connect(fd, host);
		else
			git_tcp_connect(fd, host, flags);
		/*
		 * Separate original protocol components prog and path
		 * from extended host header with a NUL byte.
		 *
		 * Note: Do not add any other headers here!  Doing so
		 * will cause older git-daemon servers to crash.
		 */
		packet_write(fd[1],
			     "%s %s%chost=%s%c",
			     prog, path, 0,
			     target_host, 0);
		free(target_host);
		free(url);
		if (free_path)
			free(path);
		return conn;
	}

	conn = xcalloc(1, sizeof(*conn));

	strbuf_init(&cmd, MAX_CMD_LEN);
	strbuf_addstr(&cmd, prog);
	strbuf_addch(&cmd, ' ');
	sq_quote_buf(&cmd, path);
	if (cmd.len >= MAX_CMD_LEN)
		die("command line too long");

	conn->in = conn->out = -1;
	conn->argv = arg = xcalloc(7, sizeof(*arg));
	if (protocol == PROTO_SSH) {
		const char *ssh = getenv("GIT_SSH");
		int putty = ssh && strcasestr(ssh, "plink");
		if (!ssh) ssh = "ssh";

		*arg++ = ssh;
		if (putty && !strcasestr(ssh, "tortoiseplink"))
			*arg++ = "-batch";
		if (port) {
			/* P is for PuTTY, p is for OpenSSH */
			*arg++ = putty ? "-P" : "-p";
			*arg++ = port;
		}
		*arg++ = host;
	}
	else {
		/* remove repo-local variables from the environment */
		conn->env = local_repo_env;
		conn->use_shell = 1;
	}
	*arg++ = cmd.buf;
	*arg = NULL;

	if (start_command(conn))
		die("unable to fork");

	fd[0] = conn->out; /* read from child's stdout */
	fd[1] = conn->in;  /* write to child's stdin */
	strbuf_release(&cmd);
	free(url);
	if (free_path)
		free(path);
	return conn;
}

int git_connection_is_socket(struct child_process *conn)
{
	return conn == &no_fork;
}

int finish_connect(struct child_process *conn)
{
	int code;
	if (!conn || git_connection_is_socket(conn))
		return 0;

	code = finish_command(conn);
	free(conn->argv);
	free(conn);
	return code;
}

char *git_getpass(const char *prompt)
{
	const char *askpass;
	struct child_process pass;
	const char *args[3];
	static struct strbuf buffer = STRBUF_INIT;

	askpass = getenv("GIT_ASKPASS");
	if (!askpass)
		askpass = askpass_program;
	if (!askpass)
		askpass = getenv("SSH_ASKPASS");
	if (!askpass || !(*askpass)) {
		char *result = getpass(prompt);
		if (!result)
			die_errno("Could not read password");
		return result;
	}

	args[0] = askpass;
	args[1]	= prompt;
	args[2] = NULL;

	memset(&pass, 0, sizeof(pass));
	pass.argv = args;
	pass.out = -1;

	if (start_command(&pass))
		exit(1);

	strbuf_reset(&buffer);
	if (strbuf_read(&buffer, pass.out, 20) < 0)
		die("failed to read password from %s\n", askpass);

	close(pass.out);

	if (finish_command(&pass))
		exit(1);

	strbuf_setlen(&buffer, strcspn(buffer.buf, "\r\n"));

	return buffer.buf;
}
