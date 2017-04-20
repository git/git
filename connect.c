#include "git-compat-util.h"
#include "cache.h"
#include "pkt-line.h"
#include "quote.h"
#include "refs.h"
#include "run-command.h"
#include "remote.h"
#include "connect.h"
#include "url.h"
#include "string-list.h"
#include "sha1-array.h"
#include "transport.h"

static char *server_capabilities;
static const char *parse_feature_value(const char *, const char *, int *);

static int check_ref(const char *name, unsigned int flags)
{
	if (!flags)
		return 1;

	if (!skip_prefix(name, "refs/", &name))
		return 0;

	/* REF_NORMAL means that we don't want the magic fake tag refs */
	if ((flags & REF_NORMAL) && check_refname_format(name, 0))
		return 0;

	/* REF_HEADS means that we want regular branch heads */
	if ((flags & REF_HEADS) && starts_with(name, "heads/"))
		return 1;

	/* REF_TAGS means that we want tags */
	if ((flags & REF_TAGS) && starts_with(name, "tags/"))
		return 1;

	/* All type bits clear means that we are ok with anything */
	return !(flags & ~REF_NORMAL);
}

int check_ref_type(const struct ref *ref, int flags)
{
	return check_ref(ref->name, flags);
}

static void die_initial_contact(int unexpected)
{
	if (unexpected)
		die(_("The remote end hung up upon initial contact"));
	else
		die(_("Could not read from remote repository.\n\n"
		      "Please make sure you have the correct access rights\n"
		      "and the repository exists."));
}

static void parse_one_symref_info(struct string_list *symref, const char *val, int len)
{
	char *sym, *target;
	struct string_list_item *item;

	if (!len)
		return; /* just "symref" */
	/* e.g. "symref=HEAD:refs/heads/master" */
	sym = xmemdupz(val, len);
	target = strchr(sym, ':');
	if (!target)
		/* just "symref=something" */
		goto reject;
	*(target++) = '\0';
	if (check_refname_format(sym, REFNAME_ALLOW_ONELEVEL) ||
	    check_refname_format(target, REFNAME_ALLOW_ONELEVEL))
		/* "symref=bogus:pair */
		goto reject;
	item = string_list_append(symref, sym);
	item->util = target;
	return;
reject:
	free(sym);
	return;
}

static void annotate_refs_with_symref_info(struct ref *ref)
{
	struct string_list symref = STRING_LIST_INIT_DUP;
	const char *feature_list = server_capabilities;

	while (feature_list) {
		int len;
		const char *val;

		val = parse_feature_value(feature_list, "symref", &len);
		if (!val)
			break;
		parse_one_symref_info(&symref, val, len);
		feature_list = val + 1;
	}
	string_list_sort(&symref);

	for (; ref; ref = ref->next) {
		struct string_list_item *item;
		item = string_list_lookup(&symref, ref->name);
		if (!item)
			continue;
		ref->symref = xstrdup((char *)item->util);
	}
	string_list_clear(&symref, 0);
}

/*
 * Read all the refs from the other end
 */
struct ref **get_remote_heads(int in, char *src_buf, size_t src_len,
			      struct ref **list, unsigned int flags,
			      struct oid_array *extra_have,
			      struct oid_array *shallow_points)
{
	struct ref **orig_list = list;

	/*
	 * A hang-up after seeing some response from the other end
	 * means that it is unexpected, as we know the other end is
	 * willing to talk to us.  A hang-up before seeing any
	 * response does not necessarily mean an ACL problem, though.
	 */
	int saw_response;
	int got_dummy_ref_with_capabilities_declaration = 0;

	*list = NULL;
	for (saw_response = 0; ; saw_response = 1) {
		struct ref *ref;
		struct object_id old_oid;
		char *name;
		int len, name_len;
		char *buffer = packet_buffer;
		const char *arg;

		len = packet_read(in, &src_buf, &src_len,
				  packet_buffer, sizeof(packet_buffer),
				  PACKET_READ_GENTLE_ON_EOF |
				  PACKET_READ_CHOMP_NEWLINE);
		if (len < 0)
			die_initial_contact(saw_response);

		if (!len)
			break;

		if (len > 4 && skip_prefix(buffer, "ERR ", &arg))
			die("remote error: %s", arg);

		if (len == GIT_SHA1_HEXSZ + strlen("shallow ") &&
			skip_prefix(buffer, "shallow ", &arg)) {
			if (get_oid_hex(arg, &old_oid))
				die("protocol error: expected shallow sha-1, got '%s'", arg);
			if (!shallow_points)
				die("repository on the other end cannot be shallow");
			oid_array_append(shallow_points, &old_oid);
			continue;
		}

		if (len < GIT_SHA1_HEXSZ + 2 || get_oid_hex(buffer, &old_oid) ||
			buffer[GIT_SHA1_HEXSZ] != ' ')
			die("protocol error: expected sha/ref, got '%s'", buffer);
		name = buffer + GIT_SHA1_HEXSZ + 1;

		name_len = strlen(name);
		if (len != name_len + GIT_SHA1_HEXSZ + 1) {
			free(server_capabilities);
			server_capabilities = xstrdup(name + name_len + 1);
		}

		if (extra_have && !strcmp(name, ".have")) {
			oid_array_append(extra_have, &old_oid);
			continue;
		}

		if (!strcmp(name, "capabilities^{}")) {
			if (saw_response)
				die("protocol error: unexpected capabilities^{}");
			if (got_dummy_ref_with_capabilities_declaration)
				die("protocol error: multiple capabilities^{}");
			got_dummy_ref_with_capabilities_declaration = 1;
			continue;
		}

		if (!check_ref(name, flags))
			continue;

		if (got_dummy_ref_with_capabilities_declaration)
			die("protocol error: unexpected ref after capabilities^{}");

		ref = alloc_ref(buffer + GIT_SHA1_HEXSZ + 1);
		oidcpy(&ref->old_oid, &old_oid);
		*list = ref;
		list = &ref->next;
	}

	annotate_refs_with_symref_info(*orig_list);

	return list;
}

static const char *parse_feature_value(const char *feature_list, const char *feature, int *lenp)
{
	int len;

	if (!feature_list)
		return NULL;

	len = strlen(feature);
	while (*feature_list) {
		const char *found = strstr(feature_list, feature);
		if (!found)
			return NULL;
		if (feature_list == found || isspace(found[-1])) {
			const char *value = found + len;
			/* feature with no value (e.g., "thin-pack") */
			if (!*value || isspace(*value)) {
				if (lenp)
					*lenp = 0;
				return value;
			}
			/* feature with a value (e.g., "agent=git/1.2.3") */
			else if (*value == '=') {
				value++;
				if (lenp)
					*lenp = strcspn(value, " \t\n");
				return value;
			}
			/*
			 * otherwise we matched a substring of another feature;
			 * keep looking
			 */
		}
		feature_list = found + 1;
	}
	return NULL;
}

int parse_feature_request(const char *feature_list, const char *feature)
{
	return !!parse_feature_value(feature_list, feature, NULL);
}

const char *server_feature_value(const char *feature, int *len)
{
	return parse_feature_value(server_capabilities, feature, len);
}

int server_supports(const char *feature)
{
	return !!server_feature_value(feature, NULL);
}

enum protocol {
	PROTO_LOCAL = 1,
	PROTO_FILE,
	PROTO_SSH,
	PROTO_GIT
};

int url_is_local_not_ssh(const char *url)
{
	const char *colon = strchr(url, ':');
	const char *slash = strchr(url, '/');
	return !colon || (slash && slash < colon) ||
		has_dos_drive_prefix(url);
}

static const char *prot_name(enum protocol protocol)
{
	switch (protocol) {
		case PROTO_LOCAL:
		case PROTO_FILE:
			return "file";
		case PROTO_SSH:
			return "ssh";
		case PROTO_GIT:
			return "git";
		default:
			return "unknown protocol";
	}
}

static enum protocol get_protocol(const char *name)
{
	if (!strcmp(name, "ssh"))
		return PROTO_SSH;
	if (!strcmp(name, "git"))
		return PROTO_GIT;
	if (!strcmp(name, "git+ssh")) /* deprecated - do not use */
		return PROTO_SSH;
	if (!strcmp(name, "ssh+git")) /* deprecated - do not use */
		return PROTO_SSH;
	if (!strcmp(name, "file"))
		return PROTO_FILE;
	die("I don't handle protocol '%s'", name);
}

static char *host_end(char **hoststart, int removebrackets)
{
	char *host = *hoststart;
	char *end;
	char *start = strstr(host, "@[");
	if (start)
		start++; /* Jump over '@' */
	else
		start = host;
	if (start[0] == '[') {
		end = strchr(start + 1, ']');
		if (end) {
			if (removebrackets) {
				*end = 0;
				memmove(start, start + 1, end - start);
				end++;
			}
		} else
			end = host;
	} else
		end = host;
	return end;
}

#define STR_(s)	# s
#define STR(s)	STR_(s)

static void get_host_and_port(char **host, const char **port)
{
	char *colon, *end;
	end = host_end(host, 1);
	colon = strchr(end, ':');
	if (colon) {
		long portnr = strtol(colon + 1, &end, 10);
		if (end != colon + 1 && *end == '\0' && 0 <= portnr && portnr < 65536) {
			*colon = 0;
			*port = colon + 1;
		} else if (!colon[1]) {
			*colon = 0;
		}
	}
}

static void enable_keepalive(int sockfd)
{
	int ka = 1;

	if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka)) < 0)
		fprintf(stderr, "unable to set SO_KEEPALIVE on socket: %s\n",
			strerror(errno));
}

#ifndef NO_IPV6

static const char *ai_name(const struct addrinfo *ai)
{
	static char addr[NI_MAXHOST];
	if (getnameinfo(ai->ai_addr, ai->ai_addrlen, addr, sizeof(addr), NULL, 0,
			NI_NUMERICHOST) != 0)
		xsnprintf(addr, sizeof(addr), "(unknown)");

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
	if (flags & CONNECT_IPV4)
		hints.ai_family = AF_INET;
	else if (flags & CONNECT_IPV6)
		hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, "Looking up %s ... ", host);

	gai = getaddrinfo(host, port, &hints, &ai);
	if (gai)
		die("Unable to look up %s (port %s) (%s)", host, port, gai_strerror(gai));

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, "done.\nConnecting to %s (port %s) ... ", host, port);

	for (ai0 = ai; ai; ai = ai->ai_next, cnt++) {
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

	enable_keepalive(sockfd);

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
	struct strbuf error_message = STRBUF_INIT;
	int sockfd = -1;
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
		memset(&sa, 0, sizeof sa);
		sa.sin_family = he->h_addrtype;
		sa.sin_port = htons(nport);
		memcpy(&sa.sin_addr, *ap, he->h_length);

		sockfd = socket(he->h_addrtype, SOCK_STREAM, 0);
		if ((sockfd < 0) ||
		    connect(sockfd, (struct sockaddr *)&sa, sizeof sa) < 0) {
			strbuf_addf(&error_message, "%s[%d: %s]: errno=%s\n",
				host,
				cnt,
				inet_ntoa(*(struct in_addr *)&sa.sin_addr),
				strerror(errno));
			if (0 <= sockfd)
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
		die("unable to connect to %s:\n%s", host, error_message.buf);

	enable_keepalive(sockfd);

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
	struct child_process *proxy;

	get_host_and_port(&host, &port);

	proxy = xmalloc(sizeof(*proxy));
	child_process_init(proxy);
	argv_array_push(&proxy->args, git_proxy_command);
	argv_array_push(&proxy->args, host);
	argv_array_push(&proxy->args, port);
	proxy->in = -1;
	proxy->out = -1;
	if (start_command(proxy))
		die("cannot start proxy %s", git_proxy_command);
	fd[0] = proxy->out; /* read from proxy stdout */
	fd[1] = proxy->in;  /* write to proxy stdin */
	return proxy;
}

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

/*
 * Extract protocol and relevant parts from the specified connection URL.
 * The caller must free() the returned strings.
 */
static enum protocol parse_connect_url(const char *url_orig, char **ret_host,
				       char **ret_path)
{
	char *url;
	char *host, *path;
	char *end;
	int separator = '/';
	enum protocol protocol = PROTO_LOCAL;

	if (is_url(url_orig))
		url = url_decode(url_orig);
	else
		url = xstrdup(url_orig);

	host = strstr(url, "://");
	if (host) {
		*host = '\0';
		protocol = get_protocol(url);
		host += 3;
	} else {
		host = url;
		if (!url_is_local_not_ssh(url)) {
			protocol = PROTO_SSH;
			separator = ':';
		}
	}

	/*
	 * Don't do destructive transforms as protocol code does
	 * '[]' unwrapping in get_host_and_port()
	 */
	end = host_end(&host, 0);

	if (protocol == PROTO_LOCAL)
		path = end;
	else if (protocol == PROTO_FILE && has_dos_drive_prefix(end))
		path = end; /* "file://$(pwd)" may be "file://C:/projects/repo" */
	else
		path = strchr(end, separator);

	if (!path || !*path)
		die("No path specified. See 'man git-pull' for valid url syntax");

	/*
	 * null-terminate hostname and point path to ~ for URL's like this:
	 *    ssh://host.xz/~user/repo
	 */

	end = path; /* Need to \0 terminate host here */
	if (separator == ':')
		path++; /* path starts after ':' */
	if (protocol == PROTO_GIT || protocol == PROTO_SSH) {
		if (path[1] == '~')
			path++;
	}

	path = xstrdup(path);
	*end = '\0';

	*ret_host = xstrdup(host);
	*ret_path = path;
	free(url);
	return protocol;
}

static struct child_process no_fork = CHILD_PROCESS_INIT;

static const char *get_ssh_command(void)
{
	const char *ssh;

	if ((ssh = getenv("GIT_SSH_COMMAND")))
		return ssh;

	if (!git_config_get_string_const("core.sshcommand", &ssh))
		return ssh;

	return NULL;
}

static int override_ssh_variant(int *port_option, int *needs_batch)
{
	char *variant;

	variant = xstrdup_or_null(getenv("GIT_SSH_VARIANT"));
	if (!variant &&
	    git_config_get_string("ssh.variant", &variant))
		return 0;

	if (!strcmp(variant, "plink") || !strcmp(variant, "putty")) {
		*port_option = 'P';
		*needs_batch = 0;
	} else if (!strcmp(variant, "tortoiseplink")) {
		*port_option = 'P';
		*needs_batch = 1;
	} else {
		*port_option = 'p';
		*needs_batch = 0;
	}
	free(variant);
	return 1;
}

static void handle_ssh_variant(const char *ssh_command, int is_cmdline,
			       int *port_option, int *needs_batch)
{
	const char *variant;
	char *p = NULL;

	if (override_ssh_variant(port_option, needs_batch))
		return;

	if (!is_cmdline) {
		p = xstrdup(ssh_command);
		variant = basename(p);
	} else {
		const char **ssh_argv;

		p = xstrdup(ssh_command);
		if (split_cmdline(p, &ssh_argv)) {
			variant = basename((char *)ssh_argv[0]);
			/*
			 * At this point, variant points into the buffer
			 * referenced by p, hence we do not need ssh_argv
			 * any longer.
			 */
			free(ssh_argv);
		} else
			return;
	}

	if (!strcasecmp(variant, "plink") ||
	    !strcasecmp(variant, "plink.exe"))
		*port_option = 'P';
	else if (!strcasecmp(variant, "tortoiseplink") ||
		 !strcasecmp(variant, "tortoiseplink.exe")) {
		*port_option = 'P';
		*needs_batch = 1;
	}
	free(p);
}

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
struct child_process *git_connect(int fd[2], const char *url,
				  const char *prog, int flags)
{
	char *hostandport, *path;
	struct child_process *conn = &no_fork;
	enum protocol protocol;
	struct strbuf cmd = STRBUF_INIT;

	/* Without this we cannot rely on waitpid() to tell
	 * what happened to our children.
	 */
	signal(SIGCHLD, SIG_DFL);

	protocol = parse_connect_url(url, &hostandport, &path);
	if ((flags & CONNECT_DIAG_URL) && (protocol != PROTO_SSH)) {
		printf("Diag: url=%s\n", url ? url : "NULL");
		printf("Diag: protocol=%s\n", prot_name(protocol));
		printf("Diag: hostandport=%s\n", hostandport ? hostandport : "NULL");
		printf("Diag: path=%s\n", path ? path : "NULL");
		conn = NULL;
	} else if (protocol == PROTO_GIT) {
		/*
		 * Set up virtual host information based on where we will
		 * connect, unless the user has overridden us in
		 * the environment.
		 */
		char *target_host = getenv("GIT_OVERRIDE_VIRTUAL_HOST");
		if (target_host)
			target_host = xstrdup(target_host);
		else
			target_host = xstrdup(hostandport);

		transport_check_allowed("git");

		/* These underlying connection commands die() if they
		 * cannot connect.
		 */
		if (git_use_proxy(hostandport))
			conn = git_proxy_connect(fd, hostandport);
		else
			git_tcp_connect(fd, hostandport, flags);
		/*
		 * Separate original protocol components prog and path
		 * from extended host header with a NUL byte.
		 *
		 * Note: Do not add any other headers here!  Doing so
		 * will cause older git-daemon servers to crash.
		 */
		packet_write_fmt(fd[1],
			     "%s %s%chost=%s%c",
			     prog, path, 0,
			     target_host, 0);
		free(target_host);
	} else {
		conn = xmalloc(sizeof(*conn));
		child_process_init(conn);

		strbuf_addstr(&cmd, prog);
		strbuf_addch(&cmd, ' ');
		sq_quote_buf(&cmd, path);

		/* remove repo-local variables from the environment */
		conn->env = local_repo_env;
		conn->use_shell = 1;
		conn->in = conn->out = -1;
		if (protocol == PROTO_SSH) {
			const char *ssh;
			int needs_batch = 0;
			int port_option = 'p';
			char *ssh_host = hostandport;
			const char *port = NULL;
			transport_check_allowed("ssh");
			get_host_and_port(&ssh_host, &port);

			if (!port)
				port = get_port(ssh_host);

			if (flags & CONNECT_DIAG_URL) {
				printf("Diag: url=%s\n", url ? url : "NULL");
				printf("Diag: protocol=%s\n", prot_name(protocol));
				printf("Diag: userandhost=%s\n", ssh_host ? ssh_host : "NULL");
				printf("Diag: port=%s\n", port ? port : "NONE");
				printf("Diag: path=%s\n", path ? path : "NULL");

				free(hostandport);
				free(path);
				free(conn);
				return NULL;
			}

			ssh = get_ssh_command();
			if (ssh)
				handle_ssh_variant(ssh, 1, &port_option,
						   &needs_batch);
			else {
				/*
				 * GIT_SSH is the no-shell version of
				 * GIT_SSH_COMMAND (and must remain so for
				 * historical compatibility).
				 */
				conn->use_shell = 0;

				ssh = getenv("GIT_SSH");
				if (!ssh)
					ssh = "ssh";
				else
					handle_ssh_variant(ssh, 0,
							   &port_option,
							   &needs_batch);
			}

			argv_array_push(&conn->args, ssh);
			if (flags & CONNECT_IPV4)
				argv_array_push(&conn->args, "-4");
			else if (flags & CONNECT_IPV6)
				argv_array_push(&conn->args, "-6");
			if (needs_batch)
				argv_array_push(&conn->args, "-batch");
			if (port) {
				argv_array_pushf(&conn->args,
						 "-%c", port_option);
				argv_array_push(&conn->args, port);
			}
			argv_array_push(&conn->args, ssh_host);
		} else {
			transport_check_allowed("file");
		}
		argv_array_push(&conn->args, cmd.buf);

		if (start_command(conn))
			die("unable to fork");

		fd[0] = conn->out; /* read from child's stdout */
		fd[1] = conn->in;  /* write to child's stdin */
		strbuf_release(&cmd);
	}
	free(hostandport);
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
	free(conn);
	return code;
}
