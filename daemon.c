#include "cache.h"
#include "pkt-line.h"
#include "exec_cmd.h"
#include "run-command.h"
#include "strbuf.h"

#include <syslog.h>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

static int log_syslog;
static int verbose;
static int reuseaddr;

static const char daemon_usage[] =
"git daemon [--verbose] [--syslog] [--export-all]\n"
"           [--timeout=n] [--init-timeout=n] [--max-connections=n]\n"
"           [--strict-paths] [--base-path=path] [--base-path-relaxed]\n"
"           [--user-path | --user-path=path]\n"
"           [--interpolated-path=path]\n"
"           [--reuseaddr] [--detach] [--pid-file=file]\n"
"           [--[enable|disable|allow-override|forbid-override]=service]\n"
"           [--inetd | [--listen=host_or_ipaddr] [--port=n]\n"
"                      [--user=user [--group=group]]\n"
"           [directory...]";

/* List of acceptable pathname prefixes */
static char **ok_paths;
static int strict_paths;

/* If this is set, git-daemon-export-ok is not required */
static int export_all_trees;

/* Take all paths relative to this one if non-NULL */
static char *base_path;
static char *interpolated_path;
static int base_path_relaxed;

/* Flag indicating client sent extra args. */
static int saw_extended_args;

/* If defined, ~user notation is allowed and the string is inserted
 * after ~user/.  E.g. a request to git://host/~alice/frotz would
 * go to /home/alice/pub_git/frotz with --user-path=pub_git.
 */
static const char *user_path;

/* Timeout, and initial timeout */
static unsigned int timeout;
static unsigned int init_timeout;

static char *hostname;
static char *canon_hostname;
static char *ip_address;
static char *tcp_port;

static void logreport(int priority, const char *err, va_list params)
{
	if (log_syslog) {
		char buf[1024];
		vsnprintf(buf, sizeof(buf), err, params);
		syslog(priority, "%s", buf);
	} else {
		/*
		 * Since stderr is set to linebuffered mode, the
		 * logging of different processes will not overlap
		 */
		fprintf(stderr, "[%"PRIuMAX"] ", (uintmax_t)getpid());
		vfprintf(stderr, err, params);
		fputc('\n', stderr);
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

static char *path_ok(char *directory)
{
	static char rpath[PATH_MAX];
	static char interp_path[PATH_MAX];
	char *path;
	char *dir;

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
			char *slash = strchr(dir, '/');
			if (!slash)
				slash = dir + restlen;
			namlen = slash - dir;
			restlen -= namlen;
			loginfo("userpath <%s>, request <%s>, namlen %d, restlen %d, slash <%s>", user_path, dir, namlen, restlen, slash);
			snprintf(rpath, PATH_MAX, "%.*s/%s%.*s",
				 namlen, dir, user_path, restlen, slash);
			dir = rpath;
		}
	}
	else if (interpolated_path && saw_extended_args) {
		struct strbuf expanded_path = STRBUF_INIT;
		struct strbuf_expand_dict_entry dict[6];

		dict[0].placeholder = "H"; dict[0].value = hostname;
		dict[1].placeholder = "CH"; dict[1].value = canon_hostname;
		dict[2].placeholder = "IP"; dict[2].value = ip_address;
		dict[3].placeholder = "P"; dict[3].value = tcp_port;
		dict[4].placeholder = "D"; dict[4].value = directory;
		dict[5].placeholder = NULL; dict[5].value = NULL;
		if (*dir != '/') {
			/* Allow only absolute */
			logerror("'%s': Non-absolute path denied (interpolated-path active)", dir);
			return NULL;
		}

		strbuf_expand(&expanded_path, interpolated_path,
				strbuf_expand_dict_cb, &dict);
		strlcpy(interp_path, expanded_path.buf, PATH_MAX);
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
		snprintf(rpath, PATH_MAX, "%s%s", base_path, dir);
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
		char **pp;
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

typedef int (*daemon_service_fn)(void);
struct daemon_service {
	const char *name;
	const char *config_name;
	daemon_service_fn fn;
	int enabled;
	int overridable;
};

static struct daemon_service *service_looking_at;
static int service_enabled;

static int git_daemon_config(const char *var, const char *value, void *cb)
{
	if (!prefixcmp(var, "daemon.") &&
	    !strcmp(var + 7, service_looking_at->config_name)) {
		service_enabled = git_config_bool(var, value);
		return 0;
	}

	/* we are not interested in parsing any other configuration here */
	return 0;
}

static int run_service(char *dir, struct daemon_service *service)
{
	const char *path;
	int enabled = service->enabled;

	loginfo("Request %s for '%s'", service->name, dir);

	if (!enabled && !service->overridable) {
		logerror("'%s': service not enabled.", service->name);
		errno = EACCES;
		return -1;
	}

	if (!(path = path_ok(dir)))
		return -1;

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
		return -1;
	}

	if (service->overridable) {
		service_looking_at = service;
		service_enabled = -1;
		git_config(git_daemon_config, NULL);
		if (0 <= service_enabled)
			enabled = service_enabled;
	}
	if (!enabled) {
		logerror("'%s': service not enabled for '%s'",
			 service->name, path);
		errno = EACCES;
		return -1;
	}

	/*
	 * We'll ignore SIGTERM from now on, we have a
	 * good client.
	 */
	signal(SIGTERM, SIG_IGN);

	return service->fn();
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

	while (strbuf_getline(&line, fp, '\n') != EOF) {
		logerror("%s", line.buf);
		strbuf_setlen(&line, 0);
	}

	strbuf_release(&line);
	fclose(fp);
}

static int run_service_command(const char **argv)
{
	struct child_process cld;

	memset(&cld, 0, sizeof(cld));
	cld.argv = argv;
	cld.git_cmd = 1;
	cld.err = -1;
	if (start_command(&cld))
		return -1;

	close(0);
	close(1);

	copy_to_log(cld.err);

	return finish_command(&cld);
}

static int upload_pack(void)
{
	/* Timeout as string */
	char timeout_buf[64];
	const char *argv[] = { "upload-pack", "--strict", NULL, ".", NULL };

	argv[2] = timeout_buf;

	snprintf(timeout_buf, sizeof timeout_buf, "--timeout=%u", timeout);
	return run_service_command(argv);
}

static int upload_archive(void)
{
	static const char *argv[] = { "upload-archive", ".", NULL };
	return run_service_command(argv);
}

static int receive_pack(void)
{
	static const char *argv[] = { "receive-pack", ".", NULL };
	return run_service_command(argv);
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

static char *xstrdup_tolower(const char *str)
{
	char *p, *dup = xstrdup(str);
	for (p = dup; *p; p++)
		*p = tolower(*p);
	return dup;
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
 * Read the host as supplied by the client connection.
 */
static void parse_host_arg(char *extra_args, int buflen)
{
	char *val;
	int vallen;
	char *end = extra_args + buflen;

	if (extra_args < end && *extra_args) {
		saw_extended_args = 1;
		if (strncasecmp("host=", extra_args, 5) == 0) {
			val = extra_args + 5;
			vallen = strlen(val) + 1;
			if (*val) {
				/* Split <host>:<port> at colon. */
				char *host;
				char *port;
				parse_host_and_port(val, &host, &port);
				if (port) {
					free(tcp_port);
					tcp_port = xstrdup(port);
				}
				free(hostname);
				hostname = xstrdup_tolower(host);
			}

			/* On to the next one */
			extra_args = val + vallen;
		}
		if (extra_args < end && *extra_args)
			die("Invalid request");
	}

	/*
	 * Locate canonical hostname and its IP address.
	 */
	if (hostname) {
#ifndef NO_IPV6
		struct addrinfo hints;
		struct addrinfo *ai;
		int gai;
		static char addrbuf[HOST_NAME_MAX + 1];

		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME;

		gai = getaddrinfo(hostname, NULL, &hints, &ai);
		if (!gai) {
			struct sockaddr_in *sin_addr = (void *)ai->ai_addr;

			inet_ntop(AF_INET, &sin_addr->sin_addr,
				  addrbuf, sizeof(addrbuf));
			free(ip_address);
			ip_address = xstrdup(addrbuf);

			free(canon_hostname);
			canon_hostname = xstrdup(ai->ai_canonname ?
						 ai->ai_canonname : ip_address);

			freeaddrinfo(ai);
		}
#else
		struct hostent *hent;
		struct sockaddr_in sa;
		char **ap;
		static char addrbuf[HOST_NAME_MAX + 1];

		hent = gethostbyname(hostname);

		ap = hent->h_addr_list;
		memset(&sa, 0, sizeof sa);
		sa.sin_family = hent->h_addrtype;
		sa.sin_port = htons(0);
		memcpy(&sa.sin_addr, *ap, hent->h_length);

		inet_ntop(hent->h_addrtype, &sa.sin_addr,
			  addrbuf, sizeof(addrbuf));

		free(canon_hostname);
		canon_hostname = xstrdup(hent->h_name);
		free(ip_address);
		ip_address = xstrdup(addrbuf);
#endif
	}
}


static int execute(struct sockaddr *addr)
{
	static char line[1000];
	int pktlen, len, i;

	if (addr) {
		char addrbuf[256] = "";
		int port = -1;

		if (addr->sa_family == AF_INET) {
			struct sockaddr_in *sin_addr = (void *) addr;
			inet_ntop(addr->sa_family, &sin_addr->sin_addr, addrbuf, sizeof(addrbuf));
			port = ntohs(sin_addr->sin_port);
#ifndef NO_IPV6
		} else if (addr && addr->sa_family == AF_INET6) {
			struct sockaddr_in6 *sin6_addr = (void *) addr;

			char *buf = addrbuf;
			*buf++ = '['; *buf = '\0'; /* stpcpy() is cool */
			inet_ntop(AF_INET6, &sin6_addr->sin6_addr, buf, sizeof(addrbuf) - 1);
			strcat(buf, "]");

			port = ntohs(sin6_addr->sin6_port);
#endif
		}
		loginfo("Connection from %s:%d", addrbuf, port);
		setenv("REMOTE_ADDR", addrbuf, 1);
	}
	else {
		unsetenv("REMOTE_ADDR");
	}

	alarm(init_timeout ? init_timeout : timeout);
	pktlen = packet_read_line(0, line, sizeof(line));
	alarm(0);

	len = strlen(line);
	if (pktlen != len)
		loginfo("Extended attributes (%d bytes) exist <%.*s>",
			(int) pktlen - len,
			(int) pktlen - len, line + len + 1);
	if (len && line[len-1] == '\n') {
		line[--len] = 0;
		pktlen--;
	}

	free(hostname);
	free(canon_hostname);
	free(ip_address);
	free(tcp_port);
	hostname = canon_hostname = ip_address = tcp_port = NULL;

	if (len != pktlen)
		parse_host_arg(line + len + 1, pktlen - len - 1);

	for (i = 0; i < ARRAY_SIZE(daemon_service); i++) {
		struct daemon_service *s = &(daemon_service[i]);
		int namelen = strlen(s->name);
		if (!prefixcmp(line, "git-") &&
		    !strncmp(s->name, line + 4, namelen) &&
		    line[namelen + 4] == ' ') {
			/*
			 * Note: The directory here is probably context sensitive,
			 * and might depend on the actual service being performed.
			 */
			return run_service(line + namelen + 5, s);
		}
	}

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
	pid_t pid;
	struct sockaddr_storage address;
} *firstborn;

static void add_child(pid_t pid, struct sockaddr *addr, int addrlen)
{
	struct child *newborn, **cradle;

	newborn = xcalloc(1, sizeof(*newborn));
	live_children++;
	newborn->pid = pid;
	memcpy(&newborn->address, addr, addrlen);
	for (cradle = &firstborn; *cradle; cradle = &(*cradle)->next)
		if (!addrcmp(&(*cradle)->address, &newborn->address))
			break;
	newborn->next = *cradle;
	*cradle = newborn;
}

static void remove_child(pid_t pid)
{
	struct child **cradle, *blanket;

	for (cradle = &firstborn; (blanket = *cradle); cradle = &blanket->next)
		if (blanket->pid == pid) {
			*cradle = blanket->next;
			live_children--;
			free(blanket);
			break;
		}
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
			kill(blanket->pid, SIGTERM);
			break;
		}
}

static void check_dead_children(void)
{
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		const char *dead = "";
		remove_child(pid);
		if (!WIFEXITED(status) || (WEXITSTATUS(status) > 0))
			dead = " (with error)";
		loginfo("[%"PRIuMAX"] Disconnected%s", (uintmax_t)pid, dead);
	}
}

static void handle(int incoming, struct sockaddr *addr, int addrlen)
{
	pid_t pid;

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

	if ((pid = fork())) {
		close(incoming);
		if (pid < 0) {
			logerror("Couldn't fork %s", strerror(errno));
			return;
		}

		add_child(pid, addr, addrlen);
		return;
	}

	dup2(incoming, 0);
	dup2(incoming, 1);
	close(incoming);

	exit(execute(addr));
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

#ifndef NO_IPV6

static int socksetup(char *listen_addr, int listen_port, int **socklist_p)
{
	int socknum = 0, *socklist = NULL;
	int maxfd = -1;
	char pbuf[NI_MAXSERV];
	struct addrinfo hints, *ai0, *ai;
	int gai;
	long flags;

	sprintf(pbuf, "%d", listen_port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	gai = getaddrinfo(listen_addr, pbuf, &hints, &ai0);
	if (gai)
		die("getaddrinfo() failed: %s", gai_strerror(gai));

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
			close(sockfd);
			continue;
		}

		if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) < 0) {
			close(sockfd);
			continue;	/* not fatal */
		}
		if (listen(sockfd, 5) < 0) {
			close(sockfd);
			continue;	/* not fatal */
		}

		flags = fcntl(sockfd, F_GETFD, 0);
		if (flags >= 0)
			fcntl(sockfd, F_SETFD, flags | FD_CLOEXEC);

		socklist = xrealloc(socklist, sizeof(int) * (socknum + 1));
		socklist[socknum++] = sockfd;

		if (maxfd < sockfd)
			maxfd = sockfd;
	}

	freeaddrinfo(ai0);

	*socklist_p = socklist;
	return socknum;
}

#else /* NO_IPV6 */

static int socksetup(char *listen_addr, int listen_port, int **socklist_p)
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
		close(sockfd);
		return 0;
	}

	if ( bind(sockfd, (struct sockaddr *)&sin, sizeof sin) < 0 ) {
		close(sockfd);
		return 0;
	}

	if (listen(sockfd, 5) < 0) {
		close(sockfd);
		return 0;
	}

	flags = fcntl(sockfd, F_GETFD, 0);
	if (flags >= 0)
		fcntl(sockfd, F_SETFD, flags | FD_CLOEXEC);

	*socklist_p = xmalloc(sizeof(int));
	**socklist_p = sockfd;
	return 1;
}

#endif

static int service_loop(int socknum, int *socklist)
{
	struct pollfd *pfd;
	int i;

	pfd = xcalloc(socknum, sizeof(struct pollfd));

	for (i = 0; i < socknum; i++) {
		pfd[i].fd = socklist[i];
		pfd[i].events = POLLIN;
	}

	signal(SIGCHLD, child_handler);

	for (;;) {
		int i;

		check_dead_children();

		if (poll(pfd, socknum, -1) < 0) {
			if (errno != EINTR) {
				logerror("Poll failed, resuming: %s",
				      strerror(errno));
				sleep(1);
			}
			continue;
		}

		for (i = 0; i < socknum; i++) {
			if (pfd[i].revents & POLLIN) {
				struct sockaddr_storage ss;
				unsigned int sslen = sizeof(ss);
				int incoming = accept(pfd[i].fd, (struct sockaddr *)&ss, &sslen);
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
				handle(incoming, (struct sockaddr *)&ss, sslen);
			}
		}
	}
}

/* if any standard file descriptor is missing open it to /dev/null */
static void sanitize_stdfds(void)
{
	int fd = open("/dev/null", O_RDWR, 0);
	while (fd != -1 && fd < 2)
		fd = dup(fd);
	if (fd == -1)
		die_errno("open /dev/null or dup failed");
	if (fd > 2)
		close(fd);
}

static void daemonize(void)
{
	switch (fork()) {
		case 0:
			break;
		case -1:
			die_errno("fork failed");
		default:
			exit(0);
	}
	if (setsid() == -1)
		die_errno("setsid failed");
	close(0);
	close(1);
	close(2);
	sanitize_stdfds();
}

static void store_pid(const char *path)
{
	FILE *f = fopen(path, "w");
	if (!f)
		die_errno("cannot open pid file '%s'", path);
	if (fprintf(f, "%"PRIuMAX"\n", (uintmax_t) getpid()) < 0 || fclose(f) != 0)
		die_errno("failed to write pid file '%s'", path);
}

static int serve(char *listen_addr, int listen_port, struct passwd *pass, gid_t gid)
{
	int socknum, *socklist;

	socknum = socksetup(listen_addr, listen_port, &socklist);
	if (socknum == 0)
		die("unable to allocate any listen sockets on host %s port %u",
		    listen_addr, listen_port);

	if (pass && gid &&
	    (initgroups(pass->pw_name, gid) || setgid (gid) ||
	     setuid(pass->pw_uid)))
		die("cannot drop privileges");

	return service_loop(socknum, socklist);
}

int main(int argc, char **argv)
{
	int listen_port = 0;
	char *listen_addr = NULL;
	int inetd_mode = 0;
	const char *pid_file = NULL, *user_name = NULL, *group_name = NULL;
	int detach = 0;
	struct passwd *pass = NULL;
	struct group *group;
	gid_t gid = 0;
	int i;

	git_extract_argv0_path(argv[0]);

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (!prefixcmp(arg, "--listen=")) {
			listen_addr = xstrdup_tolower(arg + 9);
			continue;
		}
		if (!prefixcmp(arg, "--port=")) {
			char *end;
			unsigned long n;
			n = strtoul(arg+7, &end, 0);
			if (arg[7] && !*end) {
				listen_port = n;
				continue;
			}
		}
		if (!strcmp(arg, "--inetd")) {
			inetd_mode = 1;
			log_syslog = 1;
			continue;
		}
		if (!strcmp(arg, "--verbose")) {
			verbose = 1;
			continue;
		}
		if (!strcmp(arg, "--syslog")) {
			log_syslog = 1;
			continue;
		}
		if (!strcmp(arg, "--export-all")) {
			export_all_trees = 1;
			continue;
		}
		if (!prefixcmp(arg, "--timeout=")) {
			timeout = atoi(arg+10);
			continue;
		}
		if (!prefixcmp(arg, "--init-timeout=")) {
			init_timeout = atoi(arg+15);
			continue;
		}
		if (!prefixcmp(arg, "--max-connections=")) {
			max_connections = atoi(arg+18);
			if (max_connections < 0)
				max_connections = 0;	        /* unlimited */
			continue;
		}
		if (!strcmp(arg, "--strict-paths")) {
			strict_paths = 1;
			continue;
		}
		if (!prefixcmp(arg, "--base-path=")) {
			base_path = arg+12;
			continue;
		}
		if (!strcmp(arg, "--base-path-relaxed")) {
			base_path_relaxed = 1;
			continue;
		}
		if (!prefixcmp(arg, "--interpolated-path=")) {
			interpolated_path = arg+20;
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
		if (!prefixcmp(arg, "--user-path=")) {
			user_path = arg + 12;
			continue;
		}
		if (!prefixcmp(arg, "--pid-file=")) {
			pid_file = arg + 11;
			continue;
		}
		if (!strcmp(arg, "--detach")) {
			detach = 1;
			log_syslog = 1;
			continue;
		}
		if (!prefixcmp(arg, "--user=")) {
			user_name = arg + 7;
			continue;
		}
		if (!prefixcmp(arg, "--group=")) {
			group_name = arg + 8;
			continue;
		}
		if (!prefixcmp(arg, "--enable=")) {
			enable_service(arg + 9, 1);
			continue;
		}
		if (!prefixcmp(arg, "--disable=")) {
			enable_service(arg + 10, 0);
			continue;
		}
		if (!prefixcmp(arg, "--allow-override=")) {
			make_service_overridable(arg + 17, 1);
			continue;
		}
		if (!prefixcmp(arg, "--forbid-override=")) {
			make_service_overridable(arg + 18, 0);
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

	if (log_syslog) {
		openlog("git-daemon", LOG_PID, LOG_DAEMON);
		set_die_routine(daemon_die);
	} else
		/* avoid splitting a message in the middle */
		setvbuf(stderr, NULL, _IOLBF, 0);

	if (inetd_mode && (group_name || user_name))
		die("--user and --group are incompatible with --inetd");

	if (inetd_mode && (listen_port || listen_addr))
		die("--listen= and --port= are incompatible with --inetd");
	else if (listen_port == 0)
		listen_port = DEFAULT_GIT_PORT;

	if (group_name && !user_name)
		die("--group supplied without --user");

	if (user_name) {
		pass = getpwnam(user_name);
		if (!pass)
			die("user not found - %s", user_name);

		if (!group_name)
			gid = pass->pw_gid;
		else {
			group = getgrnam(group_name);
			if (!group)
				die("group not found - %s", group_name);

			gid = group->gr_gid;
		}
	}

	if (strict_paths && (!ok_paths || !*ok_paths))
		die("option --strict-paths requires a whitelist");

	if (base_path && !is_directory(base_path))
		die("base-path '%s' does not exist or is not a directory",
		    base_path);

	if (inetd_mode) {
		struct sockaddr_storage ss;
		struct sockaddr *peer = (struct sockaddr *)&ss;
		socklen_t slen = sizeof(ss);

		if (!freopen("/dev/null", "w", stderr))
			die_errno("failed to redirect stderr to /dev/null");

		if (getpeername(0, peer, &slen))
			peer = NULL;

		return execute(peer);
	}

	if (detach) {
		daemonize();
		loginfo("Ready to rumble");
	}
	else
		sanitize_stdfds();

	if (pid_file)
		store_pid(pid_file);

	return serve(listen_addr, listen_port, pass, gid);
}
