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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "cache.h"

typedef struct store_conf {
	char *name;
	const char *path; /* should this be here? its interpretation is driver-specific */
	char *map_inbox;
	char *trash;
	unsigned max_size; /* off_t is overkill */
	unsigned trash_remote_new:1, trash_only_new:1;
} store_conf_t;

typedef struct string_list {
	struct string_list *next;
	char string[1];
} string_list_t;

typedef struct channel_conf {
	struct channel_conf *next;
	char *name;
	store_conf_t *master, *slave;
	char *master_name, *slave_name;
	char *sync_state;
	string_list_t *patterns;
	int mops, sops;
	unsigned max_messages; /* for slave only */
} channel_conf_t;

typedef struct group_conf {
	struct group_conf *next;
	char *name;
	string_list_t *channels;
} group_conf_t;

/* For message->status */
#define M_RECENT       (1<<0) /* unsyncable flag; maildir_* depend on this being 1<<0 */
#define M_DEAD         (1<<1) /* expunged */
#define M_FLAGS        (1<<2) /* flags fetched */

typedef struct message {
	struct message *next;
	/* string_list_t *keywords; */
	size_t size; /* zero implies "not fetched" */
	int uid;
	unsigned char flags, status;
} message_t;

typedef struct store {
	store_conf_t *conf; /* foreign */

	/* currently open mailbox */
	const char *name; /* foreign! maybe preset? */
	char *path; /* own */
	message_t *msgs; /* own */
	int uidvalidity;
	unsigned char opts; /* maybe preset? */
	/* note that the following do _not_ reflect stats from msgs, but mailbox totals */
	int count; /* # of messages */
	int recent; /* # of recent messages - don't trust this beyond the initial read */
} store_t;

typedef struct {
	char *data;
	int len;
	unsigned char flags;
	unsigned int crlf:1;
} msg_data_t;

#define DRV_OK          0
#define DRV_MSG_BAD     -1
#define DRV_BOX_BAD     -2
#define DRV_STORE_BAD   -3

static int Verbose, Quiet;

static void imap_info( const char *, ... );
static void imap_warn( const char *, ... );

static char *next_arg( char ** );

static void free_generic_messages( message_t * );

static int nfsnprintf( char *buf, int blen, const char *fmt, ... );


static void arc4_init( void );
static unsigned char arc4_getbyte( void );

typedef struct imap_server_conf {
	char *name;
	char *tunnel;
	char *host;
	int port;
	char *user;
	char *pass;
} imap_server_conf_t;

typedef struct imap_store_conf {
	store_conf_t gen;
	imap_server_conf_t *server;
	unsigned use_namespace:1;
} imap_store_conf_t;

#define NIL	(void*)0x1
#define LIST	(void*)0x2

typedef struct _list {
	struct _list *next, *child;
	char *val;
	int len;
} list_t;

typedef struct {
	int fd;
} Socket_t;

typedef struct {
	Socket_t sock;
	int bytes;
	int offset;
	char buf[1024];
} buffer_t;

struct imap_cmd;

typedef struct imap {
	int uidnext; /* from SELECT responses */
	list_t *ns_personal, *ns_other, *ns_shared; /* NAMESPACE info */
	unsigned caps, rcaps; /* CAPABILITY results */
	/* command queue */
	int nexttag, num_in_progress, literal_pending;
	struct imap_cmd *in_progress, **in_progress_append;
	buffer_t buf; /* this is BIG, so put it last */
} imap_t;

typedef struct imap_store {
	store_t gen;
	int uidvalidity;
	imap_t *imap;
	const char *prefix;
	unsigned /*currentnc:1,*/ trashnc:1;
} imap_store_t;

struct imap_cmd_cb {
	int (*cont)( imap_store_t *ctx, struct imap_cmd *cmd, const char *prompt );
	void (*done)( imap_store_t *ctx, struct imap_cmd *cmd, int response);
	void *ctx;
	char *data;
	int dlen;
	int uid;
	unsigned create:1, trycreate:1;
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
};

static const char *cap_list[] = {
	"LOGINDISABLED",
	"UIDPLUS",
	"LITERAL+",
	"NAMESPACE",
};

#define RESP_OK    0
#define RESP_NO    1
#define RESP_BAD   2

static int get_cmd_result( imap_store_t *ctx, struct imap_cmd *tcmd );


static const char *Flags[] = {
	"Draft",
	"Flagged",
	"Answered",
	"Seen",
	"Deleted",
};

static void
socket_perror( const char *func, Socket_t *sock, int ret )
{
	if (ret < 0)
		perror( func );
	else
		fprintf( stderr, "%s: unexpected EOF\n", func );
}

static int
socket_read( Socket_t *sock, char *buf, int len )
{
	int n = xread( sock->fd, buf, len );
	if (n <= 0) {
		socket_perror( "read", sock, n );
		close( sock->fd );
		sock->fd = -1;
	}
	return n;
}

static int
socket_write( Socket_t *sock, const char *buf, int len )
{
	int n = write_in_full( sock->fd, buf, len );
	if (n != len) {
		socket_perror( "write", sock, n );
		close( sock->fd );
		sock->fd = -1;
	}
	return n;
}

/* simple line buffering */
static int
buffer_gets( buffer_t * b, char **s )
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

				assert( start <= b->bytes );
				n = b->bytes - start;

				if (n)
					memmove(b->buf, b->buf + start, n);
				b->offset -= start;
				b->bytes = n;
				start = 0;
			}

			n = socket_read( &b->sock, b->buf + b->bytes,
					 sizeof(b->buf) - b->bytes );

			if (n <= 0)
				return -1;

			b->bytes += n;
		}

		if (b->buf[b->offset] == '\r') {
			assert( b->offset + 1 < b->bytes );
			if (b->buf[b->offset + 1] == '\n') {
				b->buf[b->offset] = 0;  /* terminate the string */
				b->offset += 2; /* next line */
				if (Verbose)
					puts( *s );
				return 0;
			}
		}

		b->offset++;
	}
	/* not reached */
}

static void
imap_info( const char *msg, ... )
{
	va_list va;

	if (!Quiet) {
		va_start( va, msg );
		vprintf( msg, va );
		va_end( va );
		fflush( stdout );
	}
}

static void
imap_warn( const char *msg, ... )
{
	va_list va;

	if (Quiet < 2) {
		va_start( va, msg );
		vfprintf( stderr, msg, va );
		va_end( va );
	}
}

static char *
next_arg( char **s )
{
	char *ret;

	if (!s || !*s)
		return NULL;
	while (isspace( (unsigned char) **s ))
		(*s)++;
	if (!**s) {
		*s = NULL;
		return NULL;
	}
	if (**s == '"') {
		++*s;
		ret = *s;
		*s = strchr( *s, '"' );
	} else {
		ret = *s;
		while (**s && !isspace( (unsigned char) **s ))
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

static void
free_generic_messages( message_t *msgs )
{
	message_t *tmsg;

	for (; msgs; msgs = tmsg) {
		tmsg = msgs->next;
		free( msgs );
	}
}

static int
nfsnprintf( char *buf, int blen, const char *fmt, ... )
{
	int ret;
	va_list va;

	va_start( va, fmt );
	if (blen <= 0 || (unsigned)(ret = vsnprintf( buf, blen, fmt, va )) >= (unsigned)blen)
		die( "Fatal: buffer too small. Please report a bug.\n");
	va_end( va );
	return ret;
}

static struct {
	unsigned char i, j, s[256];
} rs;

static void
arc4_init( void )
{
	int i, fd;
	unsigned char j, si, dat[128];

	if ((fd = open( "/dev/urandom", O_RDONLY )) < 0 && (fd = open( "/dev/random", O_RDONLY )) < 0) {
		fprintf( stderr, "Fatal: no random number source available.\n" );
		exit( 3 );
	}
	if (read_in_full( fd, dat, 128 ) != 128) {
		fprintf( stderr, "Fatal: cannot read random number source.\n" );
		exit( 3 );
	}
	close( fd );

	for (i = 0; i < 256; i++)
		rs.s[i] = i;
	for (i = j = 0; i < 256; i++) {
		si = rs.s[i];
		j += si + dat[i & 127];
		rs.s[i] = rs.s[j];
		rs.s[j] = si;
	}
	rs.i = rs.j = 0;

	for (i = 0; i < 256; i++)
		arc4_getbyte();
}

static unsigned char
arc4_getbyte( void )
{
	unsigned char si, sj;

	rs.i++;
	si = rs.s[rs.i];
	rs.j += si;
	sj = rs.s[rs.j];
	rs.s[rs.i] = sj;
	rs.s[rs.j] = si;
	return rs.s[(si + sj) & 0xff];
}

static struct imap_cmd *
v_issue_imap_cmd( imap_store_t *ctx, struct imap_cmd_cb *cb,
		  const char *fmt, va_list ap )
{
	imap_t *imap = ctx->imap;
	struct imap_cmd *cmd;
	int n, bufl;
	char buf[1024];

	cmd = xmalloc( sizeof(struct imap_cmd) );
	nfvasprintf( &cmd->cmd, fmt, ap );
	cmd->tag = ++imap->nexttag;

	if (cb)
		cmd->cb = *cb;
	else
		memset( &cmd->cb, 0, sizeof(cmd->cb) );

	while (imap->literal_pending)
		get_cmd_result( ctx, NULL );

	bufl = nfsnprintf( buf, sizeof(buf), cmd->cb.data ? CAP(LITERALPLUS) ?
			   "%d %s{%d+}\r\n" : "%d %s{%d}\r\n" : "%d %s\r\n",
			   cmd->tag, cmd->cmd, cmd->cb.dlen );
	if (Verbose) {
		if (imap->num_in_progress)
			printf( "(%d in progress) ", imap->num_in_progress );
		if (memcmp( cmd->cmd, "LOGIN", 5 ))
			printf( ">>> %s", buf );
		else
			printf( ">>> %d LOGIN <user> <pass>\n", cmd->tag );
	}
	if (socket_write( &imap->buf.sock, buf, bufl ) != bufl) {
		free( cmd->cmd );
		free( cmd );
		if (cb && cb->data)
			free( cb->data );
		return NULL;
	}
	if (cmd->cb.data) {
		if (CAP(LITERALPLUS)) {
			n = socket_write( &imap->buf.sock, cmd->cb.data, cmd->cb.dlen );
			free( cmd->cb.data );
			if (n != cmd->cb.dlen ||
			    (n = socket_write( &imap->buf.sock, "\r\n", 2 )) != 2)
			{
				free( cmd->cmd );
				free( cmd );
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

static struct imap_cmd *
issue_imap_cmd( imap_store_t *ctx, struct imap_cmd_cb *cb, const char *fmt, ... )
{
	struct imap_cmd *ret;
	va_list ap;

	va_start( ap, fmt );
	ret = v_issue_imap_cmd( ctx, cb, fmt, ap );
	va_end( ap );
	return ret;
}

static int
imap_exec( imap_store_t *ctx, struct imap_cmd_cb *cb, const char *fmt, ... )
{
	va_list ap;
	struct imap_cmd *cmdp;

	va_start( ap, fmt );
	cmdp = v_issue_imap_cmd( ctx, cb, fmt, ap );
	va_end( ap );
	if (!cmdp)
		return RESP_BAD;

	return get_cmd_result( ctx, cmdp );
}

static int
imap_exec_m( imap_store_t *ctx, struct imap_cmd_cb *cb, const char *fmt, ... )
{
	va_list ap;
	struct imap_cmd *cmdp;

	va_start( ap, fmt );
	cmdp = v_issue_imap_cmd( ctx, cb, fmt, ap );
	va_end( ap );
	if (!cmdp)
		return DRV_STORE_BAD;

	switch (get_cmd_result( ctx, cmdp )) {
	case RESP_BAD: return DRV_STORE_BAD;
	case RESP_NO: return DRV_MSG_BAD;
	default: return DRV_OK;
	}
}

static int
is_atom( list_t *list )
{
	return list && list->val && list->val != NIL && list->val != LIST;
}

static int
is_list( list_t *list )
{
	return list && list->val == LIST;
}

static void
free_list( list_t *list )
{
	list_t *tmp;

	for (; list; list = tmp) {
		tmp = list->next;
		if (is_list( list ))
			free_list( list->child );
		else if (is_atom( list ))
			free( list->val );
		free( list );
	}
}

static int
parse_imap_list_l( imap_t *imap, char **sp, list_t **curp, int level )
{
	list_t *cur;
	char *s = *sp, *p;
	int n, bytes;

	for (;;) {
		while (isspace( (unsigned char)*s ))
			s++;
		if (level && *s == ')') {
			s++;
			break;
		}
		*curp = cur = xmalloc( sizeof(*cur) );
		curp = &cur->next;
		cur->val = NULL; /* for clean bail */
		if (*s == '(') {
			/* sublist */
			s++;
			cur->val = LIST;
			if (parse_imap_list_l( imap, &s, &cur->child, level + 1 ))
				goto bail;
		} else if (imap && *s == '{') {
			/* literal */
			bytes = cur->len = strtol( s + 1, &s, 10 );
			if (*s != '}')
				goto bail;

			s = cur->val = xmalloc( cur->len );

			/* dump whats left over in the input buffer */
			n = imap->buf.bytes - imap->buf.offset;

			if (n > bytes)
				/* the entire message fit in the buffer */
				n = bytes;

			memcpy( s, imap->buf.buf + imap->buf.offset, n );
			s += n;
			bytes -= n;

			/* mark that we used part of the buffer */
			imap->buf.offset += n;

			/* now read the rest of the message */
			while (bytes > 0) {
				if ((n = socket_read (&imap->buf.sock, s, bytes)) <= 0)
					goto bail;
				s += n;
				bytes -= n;
			}

			if (buffer_gets( &imap->buf, &s ))
				goto bail;
		} else if (*s == '"') {
			/* quoted string */
			s++;
			p = s;
			for (; *s != '"'; s++)
				if (!*s)
					goto bail;
			cur->len = s - p;
			s++;
			cur->val = xmalloc( cur->len + 1 );
			memcpy( cur->val, p, cur->len );
			cur->val[cur->len] = 0;
		} else {
			/* atom */
			p = s;
			for (; *s && !isspace( (unsigned char)*s ); s++)
				if (level && *s == ')')
					break;
			cur->len = s - p;
			if (cur->len == 3 && !memcmp ("NIL", p, 3))
				cur->val = NIL;
			else {
				cur->val = xmalloc( cur->len + 1 );
				memcpy( cur->val, p, cur->len );
				cur->val[cur->len] = 0;
			}
		}

		if (!level)
			break;
		if (!*s)
			goto bail;
	}
	*sp = s;
	*curp = NULL;
	return 0;

  bail:
	*curp = NULL;
	return -1;
}

static list_t *
parse_imap_list( imap_t *imap, char **sp )
{
	list_t *head;

	if (!parse_imap_list_l( imap, sp, &head, 0 ))
		return head;
	free_list( head );
	return NULL;
}

static list_t *
parse_list( char **sp )
{
	return parse_imap_list( NULL, sp );
}

static void
parse_capability( imap_t *imap, char *cmd )
{
	char *arg;
	unsigned i;

	imap->caps = 0x80000000;
	while ((arg = next_arg( &cmd )))
		for (i = 0; i < ARRAY_SIZE(cap_list); i++)
			if (!strcmp( cap_list[i], arg ))
				imap->caps |= 1 << i;
	imap->rcaps = imap->caps;
}

static int
parse_response_code( imap_store_t *ctx, struct imap_cmd_cb *cb, char *s )
{
	imap_t *imap = ctx->imap;
	char *arg, *p;

	if (*s != '[')
		return RESP_OK;		/* no response code */
	s++;
	if (!(p = strchr( s, ']' ))) {
		fprintf( stderr, "IMAP error: malformed response code\n" );
		return RESP_BAD;
	}
	*p++ = 0;
	arg = next_arg( &s );
	if (!strcmp( "UIDVALIDITY", arg )) {
		if (!(arg = next_arg( &s )) || !(ctx->gen.uidvalidity = atoi( arg ))) {
			fprintf( stderr, "IMAP error: malformed UIDVALIDITY status\n" );
			return RESP_BAD;
		}
	} else if (!strcmp( "UIDNEXT", arg )) {
		if (!(arg = next_arg( &s )) || !(imap->uidnext = atoi( arg ))) {
			fprintf( stderr, "IMAP error: malformed NEXTUID status\n" );
			return RESP_BAD;
		}
	} else if (!strcmp( "CAPABILITY", arg )) {
		parse_capability( imap, s );
	} else if (!strcmp( "ALERT", arg )) {
		/* RFC2060 says that these messages MUST be displayed
		 * to the user
		 */
		for (; isspace( (unsigned char)*p ); p++);
		fprintf( stderr, "*** IMAP ALERT *** %s\n", p );
	} else if (cb && cb->ctx && !strcmp( "APPENDUID", arg )) {
		if (!(arg = next_arg( &s )) || !(ctx->gen.uidvalidity = atoi( arg )) ||
		    !(arg = next_arg( &s )) || !(*(int *)cb->ctx = atoi( arg )))
		{
			fprintf( stderr, "IMAP error: malformed APPENDUID status\n" );
			return RESP_BAD;
		}
	}
	return RESP_OK;
}

static int
get_cmd_result( imap_store_t *ctx, struct imap_cmd *tcmd )
{
	imap_t *imap = ctx->imap;
	struct imap_cmd *cmdp, **pcmdp, *ncmdp;
	char *cmd, *arg, *arg1, *p;
	int n, resp, resp2, tag;

	for (;;) {
		if (buffer_gets( &imap->buf, &cmd ))
			return RESP_BAD;

		arg = next_arg( &cmd );
		if (*arg == '*') {
			arg = next_arg( &cmd );
			if (!arg) {
				fprintf( stderr, "IMAP error: unable to parse untagged response\n" );
				return RESP_BAD;
			}

			if (!strcmp( "NAMESPACE", arg )) {
				imap->ns_personal = parse_list( &cmd );
				imap->ns_other = parse_list( &cmd );
				imap->ns_shared = parse_list( &cmd );
			} else if (!strcmp( "OK", arg ) || !strcmp( "BAD", arg ) ||
				   !strcmp( "NO", arg ) || !strcmp( "BYE", arg )) {
				if ((resp = parse_response_code( ctx, NULL, cmd )) != RESP_OK)
					return resp;
			} else if (!strcmp( "CAPABILITY", arg ))
				parse_capability( imap, cmd );
			else if ((arg1 = next_arg( &cmd ))) {
				if (!strcmp( "EXISTS", arg1 ))
					ctx->gen.count = atoi( arg );
				else if (!strcmp( "RECENT", arg1 ))
					ctx->gen.recent = atoi( arg );
			} else {
				fprintf( stderr, "IMAP error: unable to parse untagged response\n" );
				return RESP_BAD;
			}
		} else if (!imap->in_progress) {
			fprintf( stderr, "IMAP error: unexpected reply: %s %s\n", arg, cmd ? cmd : "" );
			return RESP_BAD;
		} else if (*arg == '+') {
			/* This can happen only with the last command underway, as
			   it enforces a round-trip. */
			cmdp = (struct imap_cmd *)((char *)imap->in_progress_append -
			       offsetof(struct imap_cmd, next));
			if (cmdp->cb.data) {
				n = socket_write( &imap->buf.sock, cmdp->cb.data, cmdp->cb.dlen );
				free( cmdp->cb.data );
				cmdp->cb.data = NULL;
				if (n != (int)cmdp->cb.dlen)
					return RESP_BAD;
			} else if (cmdp->cb.cont) {
				if (cmdp->cb.cont( ctx, cmdp, cmd ))
					return RESP_BAD;
			} else {
				fprintf( stderr, "IMAP error: unexpected command continuation request\n" );
				return RESP_BAD;
			}
			if (socket_write( &imap->buf.sock, "\r\n", 2 ) != 2)
				return RESP_BAD;
			if (!cmdp->cb.cont)
				imap->literal_pending = 0;
			if (!tcmd)
				return DRV_OK;
		} else {
			tag = atoi( arg );
			for (pcmdp = &imap->in_progress; (cmdp = *pcmdp); pcmdp = &cmdp->next)
				if (cmdp->tag == tag)
					goto gottag;
			fprintf( stderr, "IMAP error: unexpected tag %s\n", arg );
			return RESP_BAD;
		  gottag:
			if (!(*pcmdp = cmdp->next))
				imap->in_progress_append = pcmdp;
			imap->num_in_progress--;
			if (cmdp->cb.cont || cmdp->cb.data)
				imap->literal_pending = 0;
			arg = next_arg( &cmd );
			if (!strcmp( "OK", arg ))
				resp = DRV_OK;
			else {
				if (!strcmp( "NO", arg )) {
					if (cmdp->cb.create && cmd && (cmdp->cb.trycreate || !memcmp( cmd, "[TRYCREATE]", 11 ))) { /* SELECT, APPEND or UID COPY */
						p = strchr( cmdp->cmd, '"' );
						if (!issue_imap_cmd( ctx, NULL, "CREATE \"%.*s\"", strchr( p + 1, '"' ) - p + 1, p )) {
							resp = RESP_BAD;
							goto normal;
						}
						/* not waiting here violates the spec, but a server that does not
						   grok this nonetheless violates it too. */
						cmdp->cb.create = 0;
						if (!(ncmdp = issue_imap_cmd( ctx, &cmdp->cb, "%s", cmdp->cmd ))) {
							resp = RESP_BAD;
							goto normal;
						}
						free( cmdp->cmd );
						free( cmdp );
						if (!tcmd)
							return 0;	/* ignored */
						if (cmdp == tcmd)
							tcmd = ncmdp;
						continue;
					}
					resp = RESP_NO;
				} else /*if (!strcmp( "BAD", arg ))*/
					resp = RESP_BAD;
				fprintf( stderr, "IMAP command '%s' returned response (%s) - %s\n",
					 memcmp (cmdp->cmd, "LOGIN", 5) ?
							cmdp->cmd : "LOGIN <user> <pass>",
							arg, cmd ? cmd : "");
			}
			if ((resp2 = parse_response_code( ctx, &cmdp->cb, cmd )) > resp)
				resp = resp2;
		  normal:
			if (cmdp->cb.done)
				cmdp->cb.done( ctx, cmdp, resp );
			if (cmdp->cb.data)
				free( cmdp->cb.data );
			free( cmdp->cmd );
			free( cmdp );
			if (!tcmd || tcmd == cmdp)
				return resp;
		}
	}
	/* not reached */
}

static void
imap_close_server( imap_store_t *ictx )
{
	imap_t *imap = ictx->imap;

	if (imap->buf.sock.fd != -1) {
		imap_exec( ictx, NULL, "LOGOUT" );
		close( imap->buf.sock.fd );
	}
	free_list( imap->ns_personal );
	free_list( imap->ns_other );
	free_list( imap->ns_shared );
	free( imap );
}

static void
imap_close_store( store_t *ctx )
{
	imap_close_server( (imap_store_t *)ctx );
	free_generic_messages( ctx->msgs );
	free( ctx );
}

static store_t *
imap_open_store( imap_server_conf_t *srvc )
{
	imap_store_t *ctx;
	imap_t *imap;
	char *arg, *rsp;
	struct hostent *he;
	struct sockaddr_in addr;
	int s, a[2], preauth;
	pid_t pid;

	ctx = xcalloc( sizeof(*ctx), 1 );

	ctx->imap = imap = xcalloc( sizeof(*imap), 1 );
	imap->buf.sock.fd = -1;
	imap->in_progress_append = &imap->in_progress;

	/* open connection to IMAP server */

	if (srvc->tunnel) {
		imap_info( "Starting tunnel '%s'... ", srvc->tunnel );

		if (socketpair( PF_UNIX, SOCK_STREAM, 0, a )) {
			perror( "socketpair" );
			exit( 1 );
		}

		pid = fork();
		if (pid < 0)
			_exit( 127 );
		if (!pid) {
			if (dup2( a[0], 0 ) == -1 || dup2( a[0], 1 ) == -1)
				_exit( 127 );
			close( a[0] );
			close( a[1] );
			execl( "/bin/sh", "sh", "-c", srvc->tunnel, NULL );
			_exit( 127 );
		}

		close (a[0]);

		imap->buf.sock.fd = a[1];

		imap_info( "ok\n" );
	} else {
		memset( &addr, 0, sizeof(addr) );
		addr.sin_port = htons( srvc->port );
		addr.sin_family = AF_INET;

		imap_info( "Resolving %s... ", srvc->host );
		he = gethostbyname( srvc->host );
		if (!he) {
			perror( "gethostbyname" );
			goto bail;
		}
		imap_info( "ok\n" );

		addr.sin_addr.s_addr = *((int *) he->h_addr_list[0]);

		s = socket( PF_INET, SOCK_STREAM, 0 );

		imap_info( "Connecting to %s:%hu... ", inet_ntoa( addr.sin_addr ), ntohs( addr.sin_port ) );
		if (connect( s, (struct sockaddr *)&addr, sizeof(addr) )) {
			close( s );
			perror( "connect" );
			goto bail;
		}
		imap_info( "ok\n" );

		imap->buf.sock.fd = s;

	}

	/* read the greeting string */
	if (buffer_gets( &imap->buf, &rsp )) {
		fprintf( stderr, "IMAP error: no greeting response\n" );
		goto bail;
	}
	arg = next_arg( &rsp );
	if (!arg || *arg != '*' || (arg = next_arg( &rsp )) == NULL) {
		fprintf( stderr, "IMAP error: invalid greeting response\n" );
		goto bail;
	}
	preauth = 0;
	if (!strcmp( "PREAUTH", arg ))
		preauth = 1;
	else if (strcmp( "OK", arg ) != 0) {
		fprintf( stderr, "IMAP error: unknown greeting response\n" );
		goto bail;
	}
	parse_response_code( ctx, NULL, rsp );
	if (!imap->caps && imap_exec( ctx, NULL, "CAPABILITY" ) != RESP_OK)
		goto bail;

	if (!preauth) {

		imap_info ("Logging in...\n");
		if (!srvc->user) {
			fprintf( stderr, "Skipping server %s, no user\n", srvc->host );
			goto bail;
		}
		if (!srvc->pass) {
			char prompt[80];
			sprintf( prompt, "Password (%s@%s): ", srvc->user, srvc->host );
			arg = getpass( prompt );
			if (!arg) {
				perror( "getpass" );
				exit( 1 );
			}
			if (!*arg) {
				fprintf( stderr, "Skipping account %s@%s, no password\n", srvc->user, srvc->host );
				goto bail;
			}
			/*
			 * getpass() returns a pointer to a static buffer.  make a copy
			 * for long term storage.
			 */
			srvc->pass = xstrdup( arg );
		}
		if (CAP(NOLOGIN)) {
			fprintf( stderr, "Skipping account %s@%s, server forbids LOGIN\n", srvc->user, srvc->host );
			goto bail;
		}
		imap_warn( "*** IMAP Warning *** Password is being sent in the clear\n" );
		if (imap_exec( ctx, NULL, "LOGIN \"%s\" \"%s\"", srvc->user, srvc->pass ) != RESP_OK) {
			fprintf( stderr, "IMAP error: LOGIN failed\n" );
			goto bail;
		}
	} /* !preauth */

	ctx->prefix = "";
	ctx->trashnc = 1;
	return (store_t *)ctx;

  bail:
	imap_close_store( &ctx->gen );
	return NULL;
}

static int
imap_make_flags( int flags, char *buf )
{
	const char *s;
	unsigned i, d;

	for (i = d = 0; i < ARRAY_SIZE(Flags); i++)
		if (flags & (1 << i)) {
			buf[d++] = ' ';
			buf[d++] = '\\';
			for (s = Flags[i]; *s; s++)
				buf[d++] = *s;
		}
	buf[0] = '(';
	buf[d++] = ')';
	return d;
}

#define TUIDL 8

static int
imap_store_msg( store_t *gctx, msg_data_t *data, int *uid )
{
	imap_store_t *ctx = (imap_store_t *)gctx;
	imap_t *imap = ctx->imap;
	struct imap_cmd_cb cb;
	char *fmap, *buf;
	const char *prefix, *box;
	int ret, i, j, d, len, extra, nocr;
	int start, sbreak = 0, ebreak = 0;
	char flagstr[128], tuid[TUIDL * 2 + 1];

	memset( &cb, 0, sizeof(cb) );

	fmap = data->data;
	len = data->len;
	nocr = !data->crlf;
	extra = 0, i = 0;
	if (!CAP(UIDPLUS) && uid) {
	  nloop:
		start = i;
		while (i < len)
			if (fmap[i++] == '\n') {
				extra += nocr;
				if (i - 2 + nocr == start) {
					sbreak = ebreak = i - 2 + nocr;
					goto mktid;
				}
				if (!memcmp( fmap + start, "X-TUID: ", 8 )) {
					extra -= (ebreak = i) - (sbreak = start) + nocr;
					goto mktid;
				}
				goto nloop;
			}
		/* invalid message */
		free( fmap );
		return DRV_MSG_BAD;
	 mktid:
		for (j = 0; j < TUIDL; j++)
			sprintf( tuid + j * 2, "%02x", arc4_getbyte() );
		extra += 8 + TUIDL * 2 + 2;
	}
	if (nocr)
		for (; i < len; i++)
			if (fmap[i] == '\n')
				extra++;

	cb.dlen = len + extra;
	buf = cb.data = xmalloc( cb.dlen );
	i = 0;
	if (!CAP(UIDPLUS) && uid) {
		if (nocr) {
			for (; i < sbreak; i++)
				if (fmap[i] == '\n') {
					*buf++ = '\r';
					*buf++ = '\n';
				} else
					*buf++ = fmap[i];
		} else {
			memcpy( buf, fmap, sbreak );
			buf += sbreak;
		}
		memcpy( buf, "X-TUID: ", 8 );
		buf += 8;
		memcpy( buf, tuid, TUIDL * 2 );
		buf += TUIDL * 2;
		*buf++ = '\r';
		*buf++ = '\n';
		i = ebreak;
	}
	if (nocr) {
		for (; i < len; i++)
			if (fmap[i] == '\n') {
				*buf++ = '\r';
				*buf++ = '\n';
			} else
				*buf++ = fmap[i];
	} else
		memcpy( buf, fmap + i, len - i );

	free( fmap );

	d = 0;
	if (data->flags) {
		d = imap_make_flags( data->flags, flagstr );
		flagstr[d++] = ' ';
	}
	flagstr[d] = 0;

	if (!uid) {
		box = gctx->conf->trash;
		prefix = ctx->prefix;
		cb.create = 1;
		if (ctx->trashnc)
			imap->caps = imap->rcaps & ~(1 << LITERALPLUS);
	} else {
		box = gctx->name;
		prefix = !strcmp( box, "INBOX" ) ? "" : ctx->prefix;
		cb.create = 0;
	}
	cb.ctx = uid;
	ret = imap_exec_m( ctx, &cb, "APPEND \"%s%s\" %s", prefix, box, flagstr );
	imap->caps = imap->rcaps;
	if (ret != DRV_OK)
		return ret;
	if (!uid)
		ctx->trashnc = 0;
	else
		gctx->count++;

	return DRV_OK;
}

#define CHUNKSIZE 0x1000

static int
read_message( FILE *f, msg_data_t *msg )
{
	int len, r;

	memset( msg, 0, sizeof *msg );
	len = CHUNKSIZE;
	msg->data = xmalloc( len+1 );
	msg->data[0] = 0;

	while(!feof( f )) {
		if (msg->len >= len) {
			void *p;
			len += CHUNKSIZE;
			p = xrealloc(msg->data, len+1);
			if (!p)
				break;
			msg->data = p;
		}
		r = fread( &msg->data[msg->len], 1, len - msg->len, f );
		if (r <= 0)
			break;
		msg->len += r;
	}
	msg->data[msg->len] = 0;
	return msg->len;
}

static int
count_messages( msg_data_t *msg )
{
	int count = 0;
	char *p = msg->data;

	while (1) {
		if (!strncmp( "From ", p, 5 )) {
			count++;
			p += 5;
		}
		p = strstr( p+5, "\nFrom ");
		if (!p)
			break;
		p++;
	}
	return count;
}

static int
split_msg( msg_data_t *all_msgs, msg_data_t *msg, int *ofs )
{
	char *p, *data;

	memset( msg, 0, sizeof *msg );
	if (*ofs >= all_msgs->len)
		return 0;

	data = &all_msgs->data[ *ofs ];
	msg->len = all_msgs->len - *ofs;

	if (msg->len < 5 || strncmp( data, "From ", 5 ))
		return 0;

	p = strchr( data, '\n' );
	if (p) {
		p = &p[1];
		msg->len -= p-data;
		*ofs += p-data;
		data = p;
	}

	p = strstr( data, "\nFrom " );
	if (p)
		msg->len = &p[1] - data;

	msg->data = xmalloc( msg->len + 1 );
	if (!msg->data)
		return 0;

	memcpy( msg->data, data, msg->len );
	msg->data[ msg->len ] = 0;

	*ofs += msg->len;
 	return 1;
}

static imap_server_conf_t server =
{
	NULL,	/* name */
	NULL,	/* tunnel */
	NULL,	/* host */
	0,	/* port */
	NULL,	/* user */
	NULL,	/* pass */
};

static char *imap_folder;

static int
git_imap_config(const char *key, const char *val)
{
	char imap_key[] = "imap.";

	if (strncmp( key, imap_key, sizeof imap_key - 1 ))
		return 0;
	key += sizeof imap_key - 1;

	if (!strcmp( "folder", key )) {
		imap_folder = xstrdup( val );
	} else if (!strcmp( "host", key )) {
		{
			if (!strncmp( "imap:", val, 5 ))
				val += 5;
			if (!server.port)
				server.port = 143;
		}
		if (!strncmp( "//", val, 2 ))
			val += 2;
		server.host = xstrdup( val );
	}
	else if (!strcmp( "user", key ))
		server.user = xstrdup( val );
	else if (!strcmp( "pass", key ))
		server.pass = xstrdup( val );
	else if (!strcmp( "port", key ))
		server.port = git_config_int( key, val );
	else if (!strcmp( "tunnel", key ))
		server.tunnel = xstrdup( val );
	return 0;
}

int
main(int argc, char **argv)
{
	msg_data_t all_msgs, msg;
	store_t *ctx = NULL;
	int uid = 0;
	int ofs = 0;
	int r;
	int total, n = 0;

	/* init the random number generator */
	arc4_init();

	git_config( git_imap_config );

	if (!imap_folder) {
		fprintf( stderr, "no imap store specified\n" );
		return 1;
	}

	/* read the messages */
	if (!read_message( stdin, &all_msgs )) {
		fprintf(stderr,"nothing to send\n");
		return 1;
	}

	total = count_messages( &all_msgs );
	if (!total) {
		fprintf(stderr,"no messages to send\n");
		return 1;
	}

	/* write it to the imap server */
	ctx = imap_open_store( &server );
	if (!ctx) {
		fprintf( stderr,"failed to open store\n");
		return 1;
	}

	fprintf( stderr, "sending %d message%s\n", total, (total!=1)?"s":"" );
	ctx->name = imap_folder;
	while (1) {
		unsigned percent = n * 100 / total;
		fprintf( stderr, "%4u%% (%d/%d) done\r", percent, n, total );
		if (!split_msg( &all_msgs, &msg, &ofs ))
			break;
		r = imap_store_msg( ctx, &msg, &uid );
		if (r != DRV_OK) break;
		n++;
	}
	fprintf( stderr,"\n" );

	imap_close_store( ctx );

	return 0;
}
