#include "cache.h"
#include "config.h"
#include "pkt-line.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

#ifdef NO_INITGROUPS
#define initgroups(x, y) (0) /* nothing */
#endif

static enum log_destination {
	LOG_DESTINATION_UNSET = -1,
	LOG_DESTINATION_NONE = 0,
	LOG_DESTINATION_STDERR = 1,
	LOG_DESTINATION_SYSLOG = 2,
} log_destination = LOG_DESTINATION_UNSET;
static int verbose;
static int reuseaddr;
static int informative_errors;

static const char daemon_usage[] =
"git daemon [--verbose] [--syslog] [--export-all]\n"
"           [--timeout=<n>] [--init-timeout=<n>] [--max-connections=<n>]\n"
"           [--strict-paths] [--base-path=<path>] [--base-path-relaxed]\n"
"           [--user-path | --user-path=<path>]\n"
"           [--interpolated-path=<path>]\n"
"           [--reuseaddr] [--pid-file=<file>]\n"
"           [--(enable|disable|allow-override|forbid-override)=<service>]\n"
"           [--access-hook=<path>]\n"
"           [--inetd | [--listen=<host_or_ipaddr>] [--port=<n>]\n"
"                      [--detach] [--user=<user> [--group=<group>]]\n"
"           [--log-destination=(stderr|syslog|none)]\n"
"           [<directory>...]";

/* List of acceptable pathname prefixes */
static const char **ok_paths;
static int strict_paths;

/* If this is set, git-daemon-export-ok is not required */
static int export_all_trees;

/* Take all paths relative to this one if non-NULL */
static const char *base_path;
static const char *interpolated_path;
static int base_path_relaxed;

/* If defined, ~user notation is allowed and the string is inserted
 * after ~user/.  E.g. a request to git://host/~alice/frotz would
 * go to /home/alice/pub_git/frotz with --user-path=pub_git.
 */
static const char *user_path;

/* Timeout, and initial timeout */
static unsigned int timeout;
static unsigned int init_timeout;

struct hostinfo {
	struct strbuf hostname;
	struct strbuf canon_hostname;
	struct strbuf ip_address;
	struct strbuf tcp_port;
	unsigned int hostname_lookup_done:1;
	unsigned int saw_extended_args:1;
};

static void lookup_hostname(struct hostinfo *hi);

static const char *get_canon_hostname(struct hostinfo *hi)
{
	lookup_hostname(hi);
	return hi->canon_hostname.buf;
}

static const char *get_ip_address(struct hostinfo *hi)
{
	lookup_hostname(hi);
	return hi->ip_address.buf;
}

static void logreport(int priority, const char *err, va_list params)
{
	switch (log_destination) {
	case LOG_DESTINATION_SYSLOG: {
		char buf[1024];
		vsnprintf(buf, sizeof(buf), err, params);
		syslog(priority, "%s", buf);
		break;
	}
	case LOG_DESTINATION_STDERR:
		/*
		 * Since stderr is set to buffered mode, the
		 * logging of different processes will not overlap
		 * unless they overflow the (rather big) buffers.
		 */
		fprintf(stderr, "[%"PRIuMAX"] ", (uintmax_t)getpid());
		vfprintf(stderr, err, params);
		fputc('\n', stderr);
		fflush(stderr);
		break;
	case LOG_DESTINATION_NONE:
		break;
	case LOG_DESTINATION_UNSET:
		BUG("log destination not initialized correctly");
	}
}

__attribute__((format (printf, 1, 2)))
static void logerror(const char *err, ...)
{
	va_list params;
	va_start(params, err);
	logreport(LOG_ERR, err, params);
	va_end(params);
}

__attribute__((format (printf, 1, 2)))
static void loginfo(const char *err, ...)
{
	va_list params;
	if (!verbose)
		return;
	va_start(params, err);
	logreport(LOG_INFO, err, params);
	va_end(params);
}

static void NORETURN daemon_die(const char *err, va_list params)
{
	logreport(LOG_ERR, err, params);
	exit(1);
}

struct expand_path_context {
	const char *directory;
	struct hostinfo *hostinfo;
};

static size_t expand_path(struct strbuf *sb, const char *placeholder, void *ctx)
{
	struct expand_path_context *context = ctx;
	struct hostinfo *hi = context->hostinfo;

	switch (placeholder[0]) {
	case 'H':
		strbuf_addbuf(sb, &hi->hostname);
		return 1;
	case 'C':
		if (placeholder[1] == 'H') {
			strbuf_addstr(sb, get_canon_hostname(hi));
			return 2;
		}
		break;
	case 'I':
		if (placeholder[1] == 'P') {
			strbuf_addstr(sb, get_ip_address(hi));
			return 2;
		}
		break;
	case 'P':
		strbuf_addbuf(sb, &hi->tcp_port);
		return 1;
	case 'D':
		strbuf_addstr(sb, context->directory);
		return 1;
	}
	return 0;
}

static const char *path_ok(const char *directory, struct hostinfo *hi)
{
	static char rpath[PATH_MAX];
	static char interp_path[PATH_MAX];
	size_t rlen;
	const char *path;
	const char *dir;

	dir = directory;

	if (daemon_avoid_alias(dir)) {
		logerror("'%s': aliased", dir);
		return NULL;
	}

	if (*dir == '~') {
		if (!user_path) {
			logerror("'%s': User-path not allowed", dir);
			return NULL;
		}
		if (*user_path) {
			/* Got either "~alice" or "~alice/foo";
			 * rewrite them to "~alice/%s" or
			 * "~alice/%s/foo".
			 */
			int namlen, restlen = strlen(dir);
			const char *slash = strchr(dir, '/');
			if (!slash)
				slash = dir + restlen;
			namlen = slash - dir;
			restlen -= namlen;
			loginfo("userpath <%s>, request <%s>, namlen %d, restlen %d, slash <%s>", user_path, dir, namlen, restlen, slash);
			rlen = snprintf(rpath, sizeof(rpath), "%.*s/%s%.*s",
					namlen, dir, user_path, restlen, slash);
			if (rlen >= sizeof(rpath)) {
				logerror("user-path too large: %s", rpath);
				return NULL;
			}
			dir = rpath;
		}
	}
	else if (interpolated_path && hi->saw_extended_args) {
		struct strbuf expanded_path = STRBUF_INIT;
		struct expand_path_context context;

		context.directory = directory;
		context.hostinfo = hi;

		if (*dir != '/') {
			/* Allow only absolute */
			logerror("'%s': Non-absolute path denied (interpolated-path active)", dir);
			return NULL;
		}

		strbuf_expand(&expanded_path, interpolated_path,
			      expand_path, &context);

		rlen = strlcpy(interp_path, expanded_path.buf,
			       sizeof(interp_path));
		if (rlen >= sizeof(interp_path)) {
			logerror("interpolated path too large: %s",
				 interp_path);
			return NULL;
		}

		strbuf_release(&expanded_path);
		loginfo("Interpolated dir '%s'", interp_path);

		dir = interp_path;
	}
	else if (base_path) {
		if (*dir != '/') {
			/* Allow only absolute */
			logerror("'%s': Non-absolute path denied (base-path active)", dir);
			return NULL;
		}
		rlen = snprintf(rpath, sizeof(rpath), "%s%s", base_path, dir);
		if (rlen >= sizeof(rpath)) {
			logerror("base-path too large: %s", rpath);
			return NULL;
		}
		dir = rpath;
	}

	path = enter_repo(dir, strict_paths);
	if (!path && base_path && base_path_relaxed) {
		/*
		 * if we fail and base_path_relaxed is enabled, try without
		 * prefixing the base path
		 */
		dir = directory;
		path = enter_repo(dir, strict_paths);
	}

	if (!path) {
		logerror("'%s' does not appear to be a git repository", dir);
		return NULL;
	}

	if ( ok_paths && *ok_paths ) {
		const char **pp;
		int pathlen = strlen(path);

		/* The validation is done on the paths after enter_repo
		 * appends optional {.git,.git/.git} and friends, but
		 * it does not use getcwd().  So if your /pub is
		 * a symlink to /mnt/pub, you can whitelist /pub and
		 * do not have to say /mnt/pub.
		 * Do not say /pub/.
		 */
		for ( pp = ok_paths ; *pp ; pp++ ) {
			int len = strlen(*pp);
			if (len <= pathlen &&
			    !memcmp(*pp, path, len) &&
			    (path[len] == '\0' ||
			     (!strict_paths && path[len] == '/')))
				return path;
		}
	}
	else {
		/* be backwards compatible */
		if (!strict_paths)
			return path;
	}

	logerror("'%s': not in whitelist", path);
	return NULL;		/* Fallthrough. Deny by default */
}

typedef int (*daemon_service_fn)(const struct strvec *env);
struct daemon_service {
	const char *name;
	const char *config_name;
	daemon_service_fn fn;
	int enabled;
	int overridable;
};

static int daemon_error(const char *dir, const char *msg)
{
	if (!informative_errors)
		msg = "access denied or repository not exported";
	packet_write_fmt(1, "ERR %s: %s", msg, dir);
	return -1;
}

static const char *access_hook;

static int run_access_hook(struct daemon_service *service, const char *dir,
			   const char *path, struct hostinfo *hi)
{
	struct child_process child = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	const char *argv[8];
	const char **arg = argv;
	char *eol;
	int seen_errors = 0;

	*arg++ = access_hook;
	*arg++ = service->name;
	*arg++ = path;
	*arg++ = hi->hostname.buf;
	*arg++ = get_canon_hostname(hi);
	*arg++ = get_ip_address(hi);
	*arg++ = hi->tcp_port.buf;
	*arg = NULL;

	child.use_shell = 1;
	child.argv = argv;
	child.no_stdin = 1;
	child.no_stderr = 1;
	child.out = -1;
	if (start_command(&child)) {
		logerror("daemon access hook '%s' failed to start",
			 access_hook);
		goto error_return;
	}
	if (strbuf_read(&buf, child.out, 0) < 0) {
		logerror("failed to read from pipe to daemon access hook '%s'",
			 access_hook);
		strbuf_reset(&buf);
		seen_errors = 1;
	}
	if (close(child.out) < 0) {
		logerror("failed to close pipe to daemon access hook '%s'",
			 access_hook);
		seen_errors = 1;
	}
	if (finish_command(&child))
		seen_errors = 1;

	if (!seen_errors) {
		strbuf_release(&buf);
		return 0;
	}

error_return:
	strbuf_ltrim(&buf);
	if (!buf.len)
		strbuf_addstr(&buf, "service rejected");
	eol = strchr(buf.buf, '\n');
	if (eol)
		*eol = '\0';
	errno = EACCES;
	daemon_error(dir, buf.buf);
	strbuf_release(&buf);
	return -1;
}

static int run_service(const char *dir, struct daemon_service *service,
		       struct hostinfo *hi, const struct strvec *env)
{
	const char *path;
	int enabled = service->enabled;
	struct strbuf var = STRBUF_INIT;

	loginfo("Request %s for '%s'", service->name, dir);

	if (!enabled && !service->overridable) {
		logerror("'%s': service not enabled.", service->name);
		errno = EACCES;
		return daemon_error(dir, "service not enabled");
	}

	if (!(path = path_ok(dir, hi)))
		return daemon_error(dir, "no such repository");

	/*
	 * Security on the cheap.
	 *
	 * We want a readable HEAD, usable "objects" directory, and
	 * a "git-daemon-export-ok" flag that says that the other side
	 * is ok with us doing this.
	 *
	 * path_ok() uses enter_repo() and does whitelist checking.
	 * We only need to make sure the repository is exported.
	 */

	if (!export_all_trees && access("git-daemon-export-ok", F_OK)) {
		logerror("'%s': repository not exported.", path);
		errno = EACCES;
		return daemon_error(dir, "repository not exported");
	}

	if (service->overridable) {
		strbuf_addf(&var, "daemon.%s", service->config_name);
		git_config_get_bool(var.buf, &enabled);
		strbuf_release(&var);
	}
	if (!enabled) {
		logerror("'%s': service not enabled for '%s'",
			 service->name, path);
		errno = EACCES;
		return daemon_error(dir, "service not enabled");
	}

	/*
	 * Optionally, a hook can choose to deny access to the
	 * repository depending on the phase of the moon.
	 */
	if (access_hook && run_access_hook(service, dir, path, hi))
		return -1;

	/*
	 * We'll ignore SIGTERM from now on, we have a
	 * good client.
	 */
	signal(SIGTERM, SIG_IGN);

	return service->fn(env);
}

static void copy_to_log(int fd)
{
	struct strbuf line = STRBUF_INIT;
	FILE *fp;

	fp = fdopen(fd, "r");
	if (fp == NULL) {
		logerror("fdopen of error channel failed");
		close(fd);
		return;
	}

	while (strbuf_getline_lf(&line, fp) != EOF) {
		logerror("%s", line.buf);
		strbuf_setlen(&line, 0);
	}

	strbuf_release(&line);
	fclose(fp);
}

static int run_service_command(struct child_process *cld)
{
	strvec_push(&cld->args, ".");
	cld->git_cmd = 1;
	cld->err = -1;
	if (start_command(cld))
		return -1;

	close(0);
	close(1);

	copy_to_log(cld->err);

	return finish_command(cld);
}

static int upload_pack(const struct strvec *env)
{
	struct child_process cld = CHILD_PROCESS_INIT;
	strvec_pushl(&cld.args, "upload-pack", "--strict", NULL);
	strvec_pushf(&cld.args, "--timeout=%u", timeout);

	strvec_pushv(&cld.env_array, env->v);

	return run_service_command(&cld);
}

static int upload_archive(const struct strvec *env)
{
	struct child_process cld = CHILD_PROCESS_INIT;
	strvec_push(&cld.args, "upload-archive");

	strvec_pushv(&cld.env_array, env->v);

	return run_service_command(&cld);
}

static int receive_pack(const struct strvec *env)
{
	struct child_process cld = CHILD_PROCESS_INIT;
	strvec_push(&cld.args, "receive-pack");

	strvec_pushv(&cld.env_array, env->v);

	return run_service_command(&cld);
}

static struct daemon_service daemon_service[] = {
	{ "upload-archive", "uploadarch", upload_archive, 0, 1 },
	{ "upload-pack", "uploadpack", upload_pack, 1, 1 },
	{ "receive-pack", "receivepack", receive_pack, 0, 1 },
};

static void enable_service(const char *name, int ena)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(daemon_service); i++) {
		if (!strcmp(daemon_service[i].name, name)) {
			daemon_service[i].enabled = ena;
			return;
		}
	}
	die("No such service %s", name);
}

static void make_service_overridable(const char *name, int ena)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(daemon_service); i++) {
		if (!strcmp(daemon_service[i].name, name)) {
			daemon_service[i].overridable = ena;
			return;
		}
	}
	die("No such service %s", name);
}

static void parse_host_and_port(char *hostport, char **host,
	char **port)
{
	if (*hostport == '[') {
		char *end;

		end = strchr(hostport, ']');
		if (!end)
			die("Invalid request ('[' without ']')");
		*end = '\0';
		*host = hostport + 1;
		if (!end[1])
			*port = NULL;
		else if (end[1] == ':')
			*port = end + 2;
		else
			die("Garbage after end of host part");
	} else {
		*host = hostport;
		*port = strrchr(hostport, ':');
		if (*port) {
			**port = '\0';
			++*port;
		}
	}
}

/*
 * Sanitize a string from the client so that it's OK to be inserted into a
 * filesystem path. Specifically, we disallow directory separators, runs
 * of "..", and trailing and leading dots, which means that the client
 * cannot escape our base path via ".." traversal.
 */
static void sanitize_client(struct strbuf *out, const char *in)
{
	for (; *in; in++) {
		if (is_dir_sep(*in))
			continue;
		if (*in == '.' && (!out->len || out->buf[out->len - 1] == '.'))
			continue;
		strbuf_addch(out, *in);
	}

	while (out->len && out->buf[out->len - 1] == '.')
		strbuf_setlen(out, out->len - 1);
}

/*
 * Like sanitize_client, but we also perform any canonicalization
 * to make life easier on the admin.
 */
static void canonicalize_client(struct strbuf *out, const char *in)
{
	sanitize_client(out, in);
	strbuf_tolower(out);
}

/*
 * Read the host as supplied by the client connection.
 *
 * Returns a pointer to the character after the NUL byte terminating the host
 * argument, or 'extra_args' if there is no host argument.
 */
static char *parse_host_arg(struct hostinfo *hi, char *extra_args, int buflen)
{
	char *val;
	int vallen;
	char *end = extra_args + buflen;

	if (extra_args < end && *extra_args) {
		hi->saw_extended_args = 1;
		if (strncasecmp("host=", extra_args, 5) == 0) {
			val = extra_args + 5;
			vallen = strlen(val) + 1;
			loginfo("Extended attribute \"host\": %s", val);
			if (*val) {
				/* Split <host>:<port> at colon. */
				char *host;
				char *port;
				parse_host_and_port(val, &host, &port);
				if (port)
					sanitize_client(&hi->tcp_port, port);
				canonicalize_client(&hi->hostname, host);
				hi->hostname_lookup_done = 0;
			}

			/* On to the next one */
			extra_args = val + vallen;
		}
		if (extra_args < end && *extra_args)
			die("Invalid request");
	}

	return extra_args;
}

static void parse_extra_args(struct hostinfo *hi, struct strvec *env,
			     char *extra_args, int buflen)
{
	const char *end = extra_args + buflen;
	struct strbuf git_protocol = STRBUF_INIT;

	/* First look for the host argument */
	extra_args = parse_host_arg(hi, extra_args, buflen);

	/* Look for additional arguments places after a second NUL byte */
	for (; extra_args < end; extra_args += strlen(extra_args) + 1) {
		const char *arg = extra_args;

		/*
		 * Parse the extra arguments, adding most to 'git_protocol'
		 * which will be used to set the 'GIT_PROTOCOL' envvar in the
		 * service that will be run.
		 *
		 * If there ends up being a particular arg in the future that
		 * git-daemon needs to parse specifically (like the 'host' arg)
		 * then it can be parsed here and not added to 'git_protocol'.
		 */
		if (*arg) {
			if (git_protocol.len > 0)
				strbuf_addch(&git_protocol, ':');
			strbuf_addstr(&git_protocol, arg);
		}
	}

	if (git_protocol.len > 0) {
		loginfo("Extended attribute \"protocol\": %s", git_protocol.buf);
		strvec_pushf(env, GIT_PROTOCOL_ENVIRONMENT "=%s",
			     git_protocol.buf);
	}
	strbuf_release(&git_protocol);
}

/*
 * Locate canonical hostname and its IP address.
 */
static void lookup_hostname(struct hostinfo *hi)
{
	if (!hi->hostname_lookup_done && hi->hostname.len) {
#ifndef NO_IPV6
		struct addrinfo hints;
		struct addrinfo *ai;
		int gai;
		static char addrbuf[HOST_NAME_MAX + 1];

		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME;

		gai = getaddrinfo(hi->hostname.buf, NULL, &hints, &ai);
		if (!gai) {
			struct sockaddr_in *sin_addr = (void *)ai->ai_addr;

			inet_ntop(AF_INET, &sin_addr->sin_addr,
				  addrbuf, sizeof(addrbuf));
			strbuf_addstr(&hi->ip_address, addrbuf);

			if (ai->ai_canonname)
				sanitize_client(&hi->canon_hostname,
						ai->ai_canonname);
			else
				strbuf_addbuf(&hi->canon_hostname,
					      &hi->ip_address);

			freeaddrinfo(ai);
		}
#else
		struct hostent *hent;
		struct sockaddr_in sa;
		char **ap;
		static char addrbuf[HOST_NAME_MAX + 1];

		hent = gethostbyname(hi->hostname.buf);
		if (hent) {
			ap = hent->h_addr_list;
			memset(&sa, 0, sizeof sa);
			sa.sin_family = hent->h_addrtype;
			sa.sin_port = htons(0);
			memcpy(&sa.sin_addr, *ap, hent->h_length);

			inet_ntop(hent->h_addrtype, &sa.sin_addr,
				  addrbuf, sizeof(addrbuf));

			sanitize_client(&hi->canon_hostname, hent->h_name);
			strbuf_addstr(&hi->ip_address, addrbuf);
		}
#endif
		hi->hostname_lookup_done = 1;
	}
}

static void hostinfo_init(struct hostinfo *hi)
{
	memset(hi, 0, sizeof(*hi));
	strbuf_init(&hi->hostname, 0);
	strbuf_init(&hi->canon_hostname, 0);
	strbuf_init(&hi->ip_address, 0);
	strbuf_init(&hi->tcp_port, 0);
}

static void hostinfo_clear(struct hostinfo *hi)
{
	strbuf_release(&hi->hostname);
	strbuf_release(&hi->canon_hostname);
	strbuf_release(&hi->ip_address);
	strbuf_release(&hi->tcp_port);
}

static void set_keep_alive(int sockfd)
{
	int ka = 1;

	if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka)) < 0) {
		if (errno != ENOTSOCK)
			logerror("unable to set SO_KEEPALIVE on socket: %s",
				strerror(errno));
	}
}

static int execute(void)
{
	char *line = packet_buffer;
	int pktlen, len, i;
	char *addr = getenv("REMOTE_ADDR"), *port = getenv("REMOTE_PORT");
	struct hostinfo hi;
	struct strvec env = STRVEC_INIT;

	hostinfo_init(&hi);

	if (addr)
		loginfo("Connection from %s:%s", addr, port);

	set_keep_alive(0);
	alarm(init_timeout ? init_timeout : timeout);
	pktlen = packet_read(0, NULL, NULL, packet_buffer, sizeof(packet_buffer), 0);
	alarm(0);

	len = strlen(line);
	if (len && line[len-1] == '\n')
		line[len-1] = 0;

	/* parse additional args hidden behind a NUL byte */
	if (len != pktlen)
		parse_extra_args(&hi, &env, line + len + 1, pktlen - len - 1);

	for (i = 0; i < ARRAY_SIZE(daemon_service); i++) {
		struct daemon_service *s = &(daemon_service[i]);
		const char *arg;

		if (skip_prefix(line, "git-", &arg) &&
		    skip_prefix(arg, s->name, &arg) &&
		    *arg++ == ' ') {
			/*
			 * Note: The directory here is probably context sensitive,
			 * and might depend on the actual service being performed.
			 */
			int rc = run_service(arg, s, &hi, &env);
			hostinfo_clear(&hi);
			strvec_clear(&env);
			return rc;
		}
	}

	hostinfo_clear(&hi);
	strvec_clear(&env);
	logerror("Protocol error: '%s'", line);
	return -1;
}

static int addrcmp(const struct sockaddr_storage *s1,
    const struct sockaddr_storage *s2)
{
	const struct sockaddr *sa1 = (const struct sockaddr*) s1;
	const struct sockaddr *sa2 = (const struct sockaddr*) s2;

	if (sa1->sa_family != sa2->sa_family)
		return sa1->sa_family - sa2->sa_family;
	if (sa1->sa_family == AF_INET)
		return memcmp(&((struct sockaddr_in *)s1)->sin_addr,
		    &((struct sockaddr_in *)s2)->sin_addr,
		    sizeof(struct in_addr));
#ifndef NO_IPV6
	if (sa1->sa_family == AF_INET6)
		return memcmp(&((struct sockaddr_in6 *)s1)->sin6_addr,
		    &((struct sockaddr_in6 *)s2)->sin6_addr,
		    sizeof(struct in6_addr));
#endif
	return 0;
}

static int max_connections = 32;

static unsigned int live_children;

static struct child {
	struct child *next;
	struct child_process cld;
	struct sockaddr_storage address;
} *firstborn;

static void add_child(struct child_process *cld, struct sockaddr *addr, socklen_t addrlen)
{
	struct child *newborn, **cradle;

	CALLOC_ARRAY(newborn, 1);
	live_children++;
	memcpy(&newborn->cld, cld, sizeof(*cld));
	memcpy(&newborn->address, addr, addrlen);
	for (cradle = &firstborn; *cradle; cradle = &(*cradle)->next)
		if (!addrcmp(&(*cradle)->address, &newborn->address))
			break;
	newborn->next = *cradle;
	*cradle = newborn;
}

/*
 * This gets called if the number of connections grows
 * past "max_connections".
 *
 * We kill the newest connection from a duplicate IP.
 */
static void kill_some_child(void)
{
	const struct child *blanket, *next;

	if (!(blanket = firstborn))
		return;

	for (; (next = blanket->next); blanket = next)
		if (!addrcmp(&blanket->address, &next->address)) {
			kill(blanket->cld.pid, SIGTERM);
			break;
		}
}

static void check_dead_children(void)
{
	int status;
	pid_t pid;

	struct child **cradle, *blanket;
	for (cradle = &firstborn; (blanket = *cradle);)
		if ((pid = waitpid(blanket->cld.pid, &status, WNOHANG)) > 1) {
			const char *dead = "";
			if (status)
				dead = " (with error)";
			loginfo("[%"PRIuMAX"] Disconnected%s", (uintmax_t)pid, dead);

			/* remove the child */
			*cradle = blanket->next;
			live_children--;
			child_process_clear(&blanket->cld);
			free(blanket);
		} else
			cradle = &blanket->next;
}

static struct strvec cld_argv = STRVEC_INIT;
static void handle(int incoming, struct sockaddr *addr, socklen_t addrlen)
{
	struct child_process cld = CHILD_PROCESS_INIT;

	if (max_connections && live_children >= max_connections) {
		kill_some_child();
		sleep(1);  /* give it some time to die */
		check_dead_children();
		if (live_children >= max_connections) {
			close(incoming);
			logerror("Too many children, dropping connection");
			return;
		}
	}

	if (addr->sa_family == AF_INET) {
		char buf[128] = "";
		struct sockaddr_in *sin_addr = (void *) addr;
		inet_ntop(addr->sa_family, &sin_addr->sin_addr, buf, sizeof(buf));
		strvec_pushf(&cld.env_array, "REMOTE_ADDR=%s", buf);
		strvec_pushf(&cld.env_array, "REMOTE_PORT=%d",
			     ntohs(sin_addr->sin_port));
#ifndef NO_IPV6
	} else if (addr->sa_family == AF_INET6) {
		char buf[128] = "";
		struct sockaddr_in6 *sin6_addr = (void *) addr;
		inet_ntop(AF_INET6, &sin6_addr->sin6_addr, buf, sizeof(buf));
		strvec_pushf(&cld.env_array, "REMOTE_ADDR=[%s]", buf);
		strvec_pushf(&cld.env_array, "REMOTE_PORT=%d",
			     ntohs(sin6_addr->sin6_port));
#endif
	}

	cld.argv = cld_argv.v;
	cld.in = incoming;
	cld.out = dup(incoming);

	if (start_command(&cld))
		logerror("unable to fork");
	else
		add_child(&cld, addr, addrlen);
}

static void child_handler(int signo)
{
	/*
	 * Otherwise empty handler because systemcalls will get interrupted
	 * upon signal receipt
	 * SysV needs the handler to be rearmed
	 */
	signal(SIGCHLD, child_handler);
}

static int set_reuse_addr(int sockfd)
{
	int on = 1;

	if (!reuseaddr)
		return 0;
	return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
			  &on, sizeof(on));
}

struct socketlist {
	int *list;
	size_t nr;
	size_t alloc;
};

static const char *ip2str(int family, struct sockaddr *sin, socklen_t len)
{
#ifdef NO_IPV6
	static char ip[INET_ADDRSTRLEN];
#else
	static char ip[INET6_ADDRSTRLEN];
#endif

	switch (family) {
#ifndef NO_IPV6
	case AF_INET6:
		inet_ntop(family, &((struct sockaddr_in6*)sin)->sin6_addr, ip, len);
		break;
#endif
	case AF_INET:
		inet_ntop(family, &((struct sockaddr_in*)sin)->sin_addr, ip, len);
		break;
	default:
		xsnprintf(ip, sizeof(ip), "<unknown>");
	}
	return ip;
}

#ifndef NO_IPV6

static int setup_named_sock(char *listen_addr, int listen_port, struct socketlist *socklist)
{
	int socknum = 0;
	char pbuf[NI_MAXSERV];
	struct addrinfo hints, *ai0, *ai;
	int gai;
	long flags;

	xsnprintf(pbuf, sizeof(pbuf), "%d", listen_port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	gai = getaddrinfo(listen_addr, pbuf, &hints, &ai0);
	if (gai) {
		logerror("getaddrinfo() for %s failed: %s", listen_addr, gai_strerror(gai));
		return 0;
	}

	for (ai = ai0; ai; ai = ai->ai_next) {
		int sockfd;

		sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sockfd < 0)
			continue;
		if (sockfd >= FD_SETSIZE) {
			logerror("Socket descriptor too large");
			close(sockfd);
			continue;
		}

#ifdef IPV6_V6ONLY
		if (ai->ai_family == AF_INET6) {
			int on = 1;
			setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY,
				   &on, sizeof(on));
			/* Note: error is not fatal */
		}
#endif

		if (set_reuse_addr(sockfd)) {
			logerror("Could not set SO_REUSEADDR: %s", strerror(errno));
			close(sockfd);
			continue;
		}

		set_keep_alive(sockfd);

		if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
			logerror("Could not bind to %s: %s",
				 ip2str(ai->ai_family, ai->ai_addr, ai->ai_addrlen),
				 strerror(errno));
			close(sockfd);
			continue;	/* not fatal */
		}
		if (listen(sockfd, 5) < 0) {
			logerror("Could not listen to %s: %s",
				 ip2str(ai->ai_family, ai->ai_addr, ai->ai_addrlen),
				 strerror(errno));
			close(sockfd);
			continue;	/* not fatal */
		}

		flags = fcntl(sockfd, F_GETFD, 0);
		if (flags >= 0)
			fcntl(sockfd, F_SETFD, flags | FD_CLOEXEC);

		ALLOC_GROW(socklist->list, socklist->nr + 1, socklist->alloc);
		socklist->list[socklist->nr++] = sockfd;
		socknum++;
	}

	freeaddrinfo(ai0);

	return socknum;
}

#else /* NO_IPV6 */

static int setup_named_sock(char *listen_addr, int listen_port, struct socketlist *socklist)
{
	struct sockaddr_in sin;
	int sockfd;
	long flags;

	memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(listen_port);

	if (listen_addr) {
		/* Well, host better be an IP address here. */
		if (inet_pton(AF_INET, listen_addr, &sin.sin_addr.s_addr) <= 0)
			return 0;
	} else {
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		return 0;

	if (set_reuse_addr(sockfd)) {
		logerror("Could not set SO_REUSEADDR: %s", strerror(errno));
		close(sockfd);
		return 0;
	}

	set_keep_alive(sockfd);

	if ( bind(sockfd, (struct sockaddr *)&sin, sizeof sin) < 0 ) {
		logerror("Could not bind to %s: %s",
			 ip2str(AF_INET, (struct sockaddr *)&sin, sizeof(sin)),
			 strerror(errno));
		close(sockfd);
		return 0;
	}

	if (listen(sockfd, 5) < 0) {
		logerror("Could not listen to %s: %s",
			 ip2str(AF_INET, (struct sockaddr *)&sin, sizeof(sin)),
			 strerror(errno));
		close(sockfd);
		return 0;
	}

	flags = fcntl(sockfd, F_GETFD, 0);
	if (flags >= 0)
		fcntl(sockfd, F_SETFD, flags | FD_CLOEXEC);

	ALLOC_GROW(socklist->list, socklist->nr + 1, socklist->alloc);
	socklist->list[socklist->nr++] = sockfd;
	return 1;
}

#endif

static void socksetup(struct string_list *listen_addr, int listen_port, struct socketlist *socklist)
{
	if (!listen_addr->nr)
		setup_named_sock(NULL, listen_port, socklist);
	else {
		int i, socknum;
		for (i = 0; i < listen_addr->nr; i++) {
			socknum = setup_named_sock(listen_addr->items[i].string,
						   listen_port, socklist);

			if (socknum == 0)
				logerror("unable to allocate any listen sockets for host %s on port %u",
					 listen_addr->items[i].string, listen_port);
		}
	}
}

static int service_loop(struct socketlist *socklist)
{
	struct pollfd *pfd;
	int i;

	CALLOC_ARRAY(pfd, socklist->nr);

	for (i = 0; i < socklist->nr; i++) {
		pfd[i].fd = socklist->list[i];
		pfd[i].events = POLLIN;
	}

	signal(SIGCHLD, child_handler);

	for (;;) {
		int i;

		check_dead_children();

		if (poll(pfd, socklist->nr, -1) < 0) {
			if (errno != EINTR) {
				logerror("Poll failed, resuming: %s",
				      strerror(errno));
				sleep(1);
			}
			continue;
		}

		for (i = 0; i < socklist->nr; i++) {
			if (pfd[i].revents & POLLIN) {
				union {
					struct sockaddr sa;
					struct sockaddr_in sai;
#ifndef NO_IPV6
					struct sockaddr_in6 sai6;
#endif
				} ss;
				socklen_t sslen = sizeof(ss);
				int incoming = accept(pfd[i].fd, &ss.sa, &sslen);
				if (incoming < 0) {
					switch (errno) {
					case EAGAIN:
					case EINTR:
					case ECONNABORTED:
						continue;
					default:
						die_errno("accept returned");
					}
				}
				handle(incoming, &ss.sa, sslen);
			}
		}
	}
}

#ifdef NO_POSIX_GOODIES

struct credentials;

static void drop_privileges(struct credentials *cred)
{
	/* nothing */
}

static struct credentials *prepare_credentials(const char *user_name,
    const char *group_name)
{
	die("--user not supported on this platform");
}

#else

struct credentials {
	struct passwd *pass;
	gid_t gid;
};

static void drop_privileges(struct credentials *cred)
{
	if (cred && (initgroups(cred->pass->pw_name, cred->gid) ||
	    setgid (cred->gid) || setuid(cred->pass->pw_uid)))
		die("cannot drop privileges");
}

static struct credentials *prepare_credentials(const char *user_name,
    const char *group_name)
{
	static struct credentials c;

	c.pass = getpwnam(user_name);
	if (!c.pass)
		die("user not found - %s", user_name);

	if (!group_name)
		c.gid = c.pass->pw_gid;
	else {
		struct group *group = getgrnam(group_name);
		if (!group)
			die("group not found - %s", group_name);

		c.gid = group->gr_gid;
	}

	return &c;
}
#endif

static int serve(struct string_list *listen_addr, int listen_port,
    struct credentials *cred)
{
	struct socketlist socklist = { NULL, 0, 0 };

	socksetup(listen_addr, listen_port, &socklist);
	if (socklist.nr == 0)
		die("unable to allocate any listen sockets on port %u",
		    listen_port);

	drop_privileges(cred);

	loginfo("Ready to rumble");

	return service_loop(&socklist);
}

int cmd_main(int argc, const char **argv)
{
	int listen_port = 0;
	struct string_list listen_addr = STRING_LIST_INIT_NODUP;
	int serve_mode = 0, inetd_mode = 0;
	const char *pid_file = NULL, *user_name = NULL, *group_name = NULL;
	int detach = 0;
	struct credentials *cred = NULL;
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		const char *v;

		if (skip_prefix(arg, "--listen=", &v)) {
			string_list_append(&listen_addr, xstrdup_tolower(v));
			continue;
		}
		if (skip_prefix(arg, "--port=", &v)) {
			char *end;
			unsigned long n;
			n = strtoul(v, &end, 0);
			if (*v && !*end) {
				listen_port = n;
				continue;
			}
		}
		if (!strcmp(arg, "--serve")) {
			serve_mode = 1;
			continue;
		}
		if (!strcmp(arg, "--inetd")) {
			inetd_mode = 1;
			continue;
		}
		if (!strcmp(arg, "--verbose")) {
			verbose = 1;
			continue;
		}
		if (!strcmp(arg, "--syslog")) {
			log_destination = LOG_DESTINATION_SYSLOG;
			continue;
		}
		if (skip_prefix(arg, "--log-destination=", &v)) {
			if (!strcmp(v, "syslog")) {
				log_destination = LOG_DESTINATION_SYSLOG;
				continue;
			} else if (!strcmp(v, "stderr")) {
				log_destination = LOG_DESTINATION_STDERR;
				continue;
			} else if (!strcmp(v, "none")) {
				log_destination = LOG_DESTINATION_NONE;
				continue;
			} else
				die("unknown log destination '%s'", v);
		}
		if (!strcmp(arg, "--export-all")) {
			export_all_trees = 1;
			continue;
		}
		if (skip_prefix(arg, "--access-hook=", &v)) {
			access_hook = v;
			continue;
		}
		if (skip_prefix(arg, "--timeout=", &v)) {
			timeout = atoi(v);
			continue;
		}
		if (skip_prefix(arg, "--init-timeout=", &v)) {
			init_timeout = atoi(v);
			continue;
		}
		if (skip_prefix(arg, "--max-connections=", &v)) {
			max_connections = atoi(v);
			if (max_connections < 0)
				max_connections = 0;	        /* unlimited */
			continue;
		}
		if (!strcmp(arg, "--strict-paths")) {
			strict_paths = 1;
			continue;
		}
		if (skip_prefix(arg, "--base-path=", &v)) {
			base_path = v;
			continue;
		}
		if (!strcmp(arg, "--base-path-relaxed")) {
			base_path_relaxed = 1;
			continue;
		}
		if (skip_prefix(arg, "--interpolated-path=", &v)) {
			interpolated_path = v;
			continue;
		}
		if (!strcmp(arg, "--reuseaddr")) {
			reuseaddr = 1;
			continue;
		}
		if (!strcmp(arg, "--user-path")) {
			user_path = "";
			continue;
		}
		if (skip_prefix(arg, "--user-path=", &v)) {
			user_path = v;
			continue;
		}
		if (skip_prefix(arg, "--pid-file=", &v)) {
			pid_file = v;
			continue;
		}
		if (!strcmp(arg, "--detach")) {
			detach = 1;
			continue;
		}
		if (skip_prefix(arg, "--user=", &v)) {
			user_name = v;
			continue;
		}
		if (skip_prefix(arg, "--group=", &v)) {
			group_name = v;
			continue;
		}
		if (skip_prefix(arg, "--enable=", &v)) {
			enable_service(v, 1);
			continue;
		}
		if (skip_prefix(arg, "--disable=", &v)) {
			enable_service(v, 0);
			continue;
		}
		if (skip_prefix(arg, "--allow-override=", &v)) {
			make_service_overridable(v, 1);
			continue;
		}
		if (skip_prefix(arg, "--forbid-override=", &v)) {
			make_service_overridable(v, 0);
			continue;
		}
		if (!strcmp(arg, "--informative-errors")) {
			informative_errors = 1;
			continue;
		}
		if (!strcmp(arg, "--no-informative-errors")) {
			informative_errors = 0;
			continue;
		}
		if (!strcmp(arg, "--")) {
			ok_paths = &argv[i+1];
			break;
		} else if (arg[0] != '-') {
			ok_paths = &argv[i];
			break;
		}

		usage(daemon_usage);
	}

	if (log_destination == LOG_DESTINATION_UNSET) {
		if (inetd_mode || detach)
			log_destination = LOG_DESTINATION_SYSLOG;
		else
			log_destination = LOG_DESTINATION_STDERR;
	}

	if (log_destination == LOG_DESTINATION_SYSLOG) {
		openlog("git-daemon", LOG_PID, LOG_DAEMON);
		set_die_routine(daemon_die);
	} else
		/* avoid splitting a message in the middle */
		setvbuf(stderr, NULL, _IOFBF, 4096);

	if (inetd_mode && (detach || group_name || user_name))
		die("--detach, --user and --group are incompatible with --inetd");

	if (inetd_mode && (listen_port || (listen_addr.nr > 0)))
		die("--listen= and --port= are incompatible with --inetd");
	else if (listen_port == 0)
		listen_port = DEFAULT_GIT_PORT;

	if (group_name && !user_name)
		die("--group supplied without --user");

	if (user_name)
		cred = prepare_credentials(user_name, group_name);

	if (strict_paths && (!ok_paths || !*ok_paths))
		die("option --strict-paths requires a whitelist");

	if (base_path && !is_directory(base_path))
		die("base-path '%s' does not exist or is not a directory",
		    base_path);

	if (log_destination != LOG_DESTINATION_STDERR) {
		if (!freopen("/dev/null", "w", stderr))
			die_errno("failed to redirect stderr to /dev/null");
	}

	if (inetd_mode || serve_mode)
		return execute();

	if (detach) {
		if (daemonize())
			die("--detach not supported on this platform");
	}

	if (pid_file)
		write_file(pid_file, "%"PRIuMAX, (uintmax_t) getpid());

	/* prepare argv for serving-processes */
	strvec_push(&cld_argv, argv[0]); /* git-daemon */
	strvec_push(&cld_argv, "--serve");
	for (i = 1; i < argc; ++i)
		strvec_push(&cld_argv, argv[i]);

	return serve(&listen_addr, listen_port, cred);
}
