#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include "pkt-line.h"
#include "cache.h"
#include "exec_cmd.h"

static int log_syslog;
static int verbose;
static int reuseaddr;

static const char daemon_usage[] =
"git-daemon [--verbose] [--syslog] [--inetd | --port=n] [--export-all]\n"
"           [--timeout=n] [--init-timeout=n] [--strict-paths]\n"
"           [--base-path=path] [--user-path | --user-path=path]\n"
"           [--reuseaddr] [directory...]";

/* List of acceptable pathname prefixes */
static char **ok_paths = NULL;
static int strict_paths = 0;

/* If this is set, git-daemon-export-ok is not required */
static int export_all_trees = 0;

/* Take all paths relative to this one if non-NULL */
static char *base_path = NULL;

/* If defined, ~user notation is allowed and the string is inserted
 * after ~user/.  E.g. a request to git://host/~alice/frotz would
 * go to /home/alice/pub_git/frotz with --user-path=pub_git.
 */
static const char *user_path = NULL;

/* Timeout, and initial timeout */
static unsigned int timeout = 0;
static unsigned int init_timeout = 0;

static void logreport(int priority, const char *err, va_list params)
{
	/* We should do a single write so that it is atomic and output
	 * of several processes do not get intermingled. */
	char buf[1024];
	int buflen;
	int maxlen, msglen;

	/* sizeof(buf) should be big enough for "[pid] \n" */
	buflen = snprintf(buf, sizeof(buf), "[%ld] ", (long) getpid());

	maxlen = sizeof(buf) - buflen - 1; /* -1 for our own LF */
	msglen = vsnprintf(buf + buflen, maxlen, err, params);

	if (log_syslog) {
		syslog(priority, "%s", buf);
		return;
	}

	/* maxlen counted our own LF but also counts space given to
	 * vsnprintf for the terminating NUL.  We want to make sure that
	 * we have space for our own LF and NUL after the "meat" of the
	 * message, so truncate it at maxlen - 1.
	 */
	if (msglen > maxlen - 1)
		msglen = maxlen - 1;
	else if (msglen < 0)
		msglen = 0; /* Protect against weird return values. */
	buflen += msglen;

	buf[buflen++] = '\n';
	buf[buflen] = '\0';

	write(2, buf, buflen);
}

static void logerror(const char *err, ...)
{
	va_list params;
	va_start(params, err);
	logreport(LOG_ERR, err, params);
	va_end(params);
}

static void loginfo(const char *err, ...)
{
	va_list params;
	if (!verbose)
		return;
	va_start(params, err);
	logreport(LOG_INFO, err, params);
	va_end(params);
}

static int avoid_alias(char *p)
{
	int sl, ndot;

	/* 
	 * This resurrects the belts and suspenders paranoia check by HPA
	 * done in <435560F7.4080006@zytor.com> thread, now enter_repo()
	 * does not do getcwd() based path canonicalizations.
	 *
	 * sl becomes true immediately after seeing '/' and continues to
	 * be true as long as dots continue after that without intervening
	 * non-dot character.
	 */
	if (!p || (*p != '/' && *p != '~'))
		return -1;
	sl = 1; ndot = 0;
	p++;

	while (1) {
		char ch = *p++;
		if (sl) {
			if (ch == '.')
				ndot++;
			else if (ch == '/') {
				if (ndot < 3)
					/* reject //, /./ and /../ */
					return -1;
				ndot = 0;
			}
			else if (ch == 0) {
				if (0 < ndot && ndot < 3)
					/* reject /.$ and /..$ */
					return -1;
				return 0;
			}
			else
				sl = ndot = 0;
		}
		else if (ch == 0)
			return 0;
		else if (ch == '/') {
			sl = 1;
			ndot = 0;
		}
	}
}

static char *path_ok(char *dir)
{
	static char rpath[PATH_MAX];
	char *path;

	if (avoid_alias(dir)) {
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
	else if (base_path) {
		if (*dir != '/') {
			/* Allow only absolute */
			logerror("'%s': Non-absolute path denied (base-path active)", dir);
			return NULL;
		}
		else {
			snprintf(rpath, PATH_MAX, "%s%s", base_path, dir);
			dir = rpath;
		}
	}

	path = enter_repo(dir, strict_paths);

	if (!path) {
		logerror("'%s': unable to chdir or not a git archive", dir);
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

static int upload(char *dir)
{
	/* Timeout as string */
	char timeout_buf[64];
	const char *path;

	loginfo("Request for '%s'", dir);

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

	/*
	 * We'll ignore SIGTERM from now on, we have a
	 * good client.
	 */
	signal(SIGTERM, SIG_IGN);

	snprintf(timeout_buf, sizeof timeout_buf, "--timeout=%u", timeout);

	/* git-upload-pack only ever reads stuff, so this is safe */
	execl_git_cmd("upload-pack", "--strict", timeout_buf, ".", NULL);
	return -1;
}

static int execute(struct sockaddr *addr)
{
	static char line[1000];
	int pktlen, len;

	if (addr) {
		char addrbuf[256] = "";
		int port = -1;

		if (addr->sa_family == AF_INET) {
			struct sockaddr_in *sin_addr = (void *) addr;
			inet_ntop(addr->sa_family, &sin_addr->sin_addr, addrbuf, sizeof(addrbuf));
			port = sin_addr->sin_port;
#ifndef NO_IPV6
		} else if (addr && addr->sa_family == AF_INET6) {
			struct sockaddr_in6 *sin6_addr = (void *) addr;

			char *buf = addrbuf;
			*buf++ = '['; *buf = '\0'; /* stpcpy() is cool */
			inet_ntop(AF_INET6, &sin6_addr->sin6_addr, buf, sizeof(addrbuf) - 1);
			strcat(buf, "]");

			port = sin6_addr->sin6_port;
#endif
		}
		loginfo("Connection from %s:%d", addrbuf, port);
	}

	alarm(init_timeout ? init_timeout : timeout);
	pktlen = packet_read_line(0, line, sizeof(line));
	alarm(0);

	len = strlen(line);
	if (pktlen != len)
		loginfo("Extended attributes (%d bytes) exist <%.*s>",
			(int) pktlen - len,
			(int) pktlen - len, line + len + 1);
	if (len && line[len-1] == '\n')
		line[--len] = 0;

	if (!strncmp("git-upload-pack ", line, 16))
		return upload(line+16);

	logerror("Protocol error: '%s'", line);
	return -1;
}


/*
 * We count spawned/reaped separately, just to avoid any
 * races when updating them from signals. The SIGCHLD handler
 * will only update children_reaped, and the fork logic will
 * only update children_spawned.
 *
 * MAX_CHILDREN should be a power-of-two to make the modulus
 * operation cheap. It should also be at least twice
 * the maximum number of connections we will ever allow.
 */
#define MAX_CHILDREN 128

static int max_connections = 25;

/* These are updated by the signal handler */
static volatile unsigned int children_reaped = 0;
static pid_t dead_child[MAX_CHILDREN];

/* These are updated by the main loop */
static unsigned int children_spawned = 0;
static unsigned int children_deleted = 0;

static struct child {
	pid_t pid;
	int addrlen;
	struct sockaddr_storage address;
} live_child[MAX_CHILDREN];

static void add_child(int idx, pid_t pid, struct sockaddr *addr, int addrlen)
{
	live_child[idx].pid = pid;
	live_child[idx].addrlen = addrlen;
	memcpy(&live_child[idx].address, addr, addrlen);
}

/*
 * Walk from "deleted" to "spawned", and remove child "pid".
 *
 * We move everything up by one, since the new "deleted" will
 * be one higher.
 */
static void remove_child(pid_t pid, unsigned deleted, unsigned spawned)
{
	struct child n;

	deleted %= MAX_CHILDREN;
	spawned %= MAX_CHILDREN;
	if (live_child[deleted].pid == pid) {
		live_child[deleted].pid = -1;
		return;
	}
	n = live_child[deleted];
	for (;;) {
		struct child m;
		deleted = (deleted + 1) % MAX_CHILDREN;
		if (deleted == spawned)
			die("could not find dead child %d\n", pid);
		m = live_child[deleted];
		live_child[deleted] = n;
		if (m.pid == pid)
			return;
		n = m;
	}
}

/*
 * This gets called if the number of connections grows
 * past "max_connections".
 *
 * We _should_ start off by searching for connections
 * from the same IP, and if there is some address wth
 * multiple connections, we should kill that first.
 *
 * As it is, we just "randomly" kill 25% of the connections,
 * and our pseudo-random generator sucks too. I have no
 * shame.
 *
 * Really, this is just a place-holder for a _real_ algorithm.
 */
static void kill_some_children(int signo, unsigned start, unsigned stop)
{
	start %= MAX_CHILDREN;
	stop %= MAX_CHILDREN;
	while (start != stop) {
		if (!(start & 3))
			kill(live_child[start].pid, signo);
		start = (start + 1) % MAX_CHILDREN;
	}
}

static void check_max_connections(void)
{
	for (;;) {
		int active;
		unsigned spawned, reaped, deleted;

		spawned = children_spawned;
		reaped = children_reaped;
		deleted = children_deleted;

		while (deleted < reaped) {
			pid_t pid = dead_child[deleted % MAX_CHILDREN];
			remove_child(pid, deleted, spawned);
			deleted++;
		}
		children_deleted = deleted;

		active = spawned - deleted;
		if (active <= max_connections)
			break;

		/* Kill some unstarted connections with SIGTERM */
		kill_some_children(SIGTERM, deleted, spawned);
		if (active <= max_connections << 1)
			break;

		/* If the SIGTERM thing isn't helping use SIGKILL */
		kill_some_children(SIGKILL, deleted, spawned);
		sleep(1);
	}
}

static void handle(int incoming, struct sockaddr *addr, int addrlen)
{
	pid_t pid = fork();

	if (pid) {
		unsigned idx;

		close(incoming);
		if (pid < 0)
			return;

		idx = children_spawned % MAX_CHILDREN;
		children_spawned++;
		add_child(idx, pid, addr, addrlen);

		check_max_connections();
		return;
	}

	dup2(incoming, 0);
	dup2(incoming, 1);
	close(incoming);

	exit(execute(addr));
}

static void child_handler(int signo)
{
	for (;;) {
		int status;
		pid_t pid = waitpid(-1, &status, WNOHANG);

		if (pid > 0) {
			unsigned reaped = children_reaped;
			dead_child[reaped % MAX_CHILDREN] = pid;
			children_reaped = reaped + 1;
			/* XXX: Custom logging, since we don't wanna getpid() */
			if (verbose) {
				const char *dead = "";
				if (!WIFEXITED(status) || WEXITSTATUS(status) > 0)
					dead = " (with error)";
				if (log_syslog)
					syslog(LOG_INFO, "[%d] Disconnected%s", pid, dead);
				else
					fprintf(stderr, "[%d] Disconnected%s\n", pid, dead);
			}
			continue;
		}
		break;
	}
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

static int socksetup(int port, int **socklist_p)
{
	int socknum = 0, *socklist = NULL;
	int maxfd = -1;
	char pbuf[NI_MAXSERV];

	struct addrinfo hints, *ai0, *ai;
	int gai;

	sprintf(pbuf, "%d", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	gai = getaddrinfo(NULL, pbuf, &hints, &ai0);
	if (gai)
		die("getaddrinfo() failed: %s\n", gai_strerror(gai));

	for (ai = ai0; ai; ai = ai->ai_next) {
		int sockfd;
		int *newlist;

		sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sockfd < 0)
			continue;
		if (sockfd >= FD_SETSIZE) {
			error("too large socket descriptor.");
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

		newlist = realloc(socklist, sizeof(int) * (socknum + 1));
		if (!newlist)
			die("memory allocation failed: %s", strerror(errno));

		socklist = newlist;
		socklist[socknum++] = sockfd;

		if (maxfd < sockfd)
			maxfd = sockfd;
	}

	freeaddrinfo(ai0);

	*socklist_p = socklist;
	return socknum;
}

#else /* NO_IPV6 */

static int socksetup(int port, int **socklist_p)
{
	struct sockaddr_in sin;
	int sockfd;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		return 0;

	memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(port);

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

		if (poll(pfd, socknum, -1) < 0) {
			if (errno != EINTR) {
				error("poll failed, resuming: %s",
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
						die("accept returned %s", strerror(errno));
					}
				}
				handle(incoming, (struct sockaddr *)&ss, sslen);
			}
		}
	}
}

static int serve(int port)
{
	int socknum, *socklist;

	socknum = socksetup(port, &socklist);
	if (socknum == 0)
		die("unable to allocate any listen sockets on port %u", port);

	return service_loop(socknum, socklist);
}

int main(int argc, char **argv)
{
	int port = DEFAULT_GIT_PORT;
	int inetd_mode = 0;
	int i;

	/* Without this we cannot rely on waitpid() to tell
	 * what happened to our children.
	 */
	signal(SIGCHLD, SIG_DFL);

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (!strncmp(arg, "--port=", 7)) {
			char *end;
			unsigned long n;
			n = strtoul(arg+7, &end, 0);
			if (arg[7] && !*end) {
				port = n;
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
		if (!strncmp(arg, "--timeout=", 10)) {
			timeout = atoi(arg+10);
			continue;
		}
		if (!strncmp(arg, "--init-timeout=", 15)) {
			init_timeout = atoi(arg+15);
			continue;
		}
		if (!strcmp(arg, "--strict-paths")) {
			strict_paths = 1;
			continue;
		}
		if (!strncmp(arg, "--base-path=", 12)) {
			base_path = arg+12;
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
		if (!strncmp(arg, "--user-path=", 12)) {
			user_path = arg + 12;
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

	if (log_syslog)
		openlog("git-daemon", 0, LOG_DAEMON);

	if (strict_paths && (!ok_paths || !*ok_paths)) {
		if (!inetd_mode)
			die("git-daemon: option --strict-paths requires a whitelist");

		logerror("option --strict-paths requires a whitelist");
		exit (1);
	}

	if (inetd_mode) {
		struct sockaddr_storage ss;
		struct sockaddr *peer = (struct sockaddr *)&ss;
		socklen_t slen = sizeof(ss);

		freopen("/dev/null", "w", stderr);

		if (getpeername(0, peer, &slen))
			peer = NULL;

		return execute(peer);
	}

	return serve(port);
}
