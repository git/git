#include "cache.h"
#include "pkt-line.h"
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

static const char daemon_usage[] = "git-daemon [--inetd | --port=n]";

static int upload(char *dir, int dirlen)
{
	if (chdir(dir) < 0)
		return -1;
	chdir(".git");

	/*
	 * Security on the cheap.
	 *
	 * We want a readable HEAD, usable "objects" directory, and 
	 * a "git-daemon-export-ok" flag that says that the other side
	 * is ok with us doing this.
	 */
	if (access("git-daemon-export-ok", F_OK) ||
	    access("objects/00", X_OK) ||
	    access("HEAD", R_OK))
		return -1;

	/*
	 * We'll ignore SIGTERM from now on, we have a
	 * good client.
	 */
	signal(SIGTERM, SIG_IGN);

	/* git-upload-pack only ever reads stuff, so this is safe */
	execlp("git-upload-pack", "git-upload-pack", ".", NULL);
	return -1;
}

static int execute(void)
{
	static char line[1000];
	int len;

	len = packet_read_line(0, line, sizeof(line));

	if (len && line[len-1] == '\n')
		line[--len] = 0;

	if (!strncmp("git-upload-pack /", line, 17))
		return upload(line + 16, len - 16);

	fprintf(stderr, "got bad connection '%s'\n", line);
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
	socklen_t addrlen;
	struct sockaddr_storage address;
} live_child[MAX_CHILDREN];

static void add_child(int idx, pid_t pid, struct sockaddr *addr, socklen_t addrlen)
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

static void handle(int incoming, struct sockaddr *addr, socklen_t addrlen)
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
	exit(execute());
}

static void child_handler(int signo)
{
	for (;;) {
		pid_t pid = waitpid(-1, NULL, WNOHANG);

		if (pid > 0) {
			unsigned reaped = children_reaped;
			dead_child[reaped % MAX_CHILDREN] = pid;
			children_reaped = reaped + 1;
			continue;
		}
		break;
	}
}

static int serve(int port)
{
	struct addrinfo hints, *ai0, *ai;
	int gai;
	int socknum = 0, *socklist = NULL;
	int maxfd = -1;
	fd_set fds_init, fds;
	char pbuf[NI_MAXSERV];

	signal(SIGCHLD, child_handler);

	sprintf(pbuf, "%d", port);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	gai = getaddrinfo(NULL, pbuf, &hints, &ai0);
	if (gai)
		die("getaddrinfo() failed: %s\n", gai_strerror(gai));

	FD_ZERO(&fds_init);

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

		FD_SET(sockfd, &fds_init);
		if (maxfd < sockfd)
			maxfd = sockfd;
	}

	freeaddrinfo(ai0);

	if (socknum == 0)
		die("unable to allocate any listen sockets on port %u", port);

	for (;;) {
		int i;
		fds = fds_init;
		
		if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) {
			if (errno != EINTR) {
				error("select failed, resuming: %s",
				      strerror(errno));
				sleep(1);
			}
			continue;
		}

		for (i = 0; i < socknum; i++) {
			int sockfd = socklist[i];

			if (FD_ISSET(sockfd, &fds)) {
				struct sockaddr_storage ss;
				socklen_t sslen = sizeof(ss);
				int incoming = accept(sockfd, (struct sockaddr *)&ss, &sslen);
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

int main(int argc, char **argv)
{
	int port = DEFAULT_GIT_PORT;
	int inetd_mode = 0;
	int i;

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
			continue;
		}

		usage(daemon_usage);
	}

	if (inetd_mode)
		return execute();

	return serve(port);
}
