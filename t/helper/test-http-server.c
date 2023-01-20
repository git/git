#include "daemon-utils.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"
#include "trace2.h"
#include "version.h"
#include "dir.h"
#include "date.h"

#define TR2_CAT "test-http-server"

static const char *pid_file;
static int verbose;
static int reuseaddr;

static const char test_http_auth_usage[] =
"http-server [--verbose]\n"
"           [--timeout=<n>] [--max-connections=<n>]\n"
"           [--reuseaddr] [--pid-file=<file>]\n"
"           [--listen=<host_or_ipaddr>]* [--port=<n>]\n"
;

static unsigned int timeout;

static void logreport(const char *label, const char *err, va_list params)
{
	struct strbuf msg = STRBUF_INIT;

	strbuf_addf(&msg, "[%"PRIuMAX"] %s: ", (uintmax_t)getpid(), label);
	strbuf_vaddf(&msg, err, params);
	strbuf_addch(&msg, '\n');

	fwrite(msg.buf, sizeof(char), msg.len, stderr);
	fflush(stderr);

	strbuf_release(&msg);
}

__attribute__((format (printf, 1, 2)))
static void logerror(const char *err, ...)
{
	va_list params;
	va_start(params, err);
	logreport("error", err, params);
	va_end(params);
}

__attribute__((format (printf, 1, 2)))
static void loginfo(const char *err, ...)
{
	va_list params;
	if (!verbose)
		return;
	va_start(params, err);
	logreport("info", err, params);
	va_end(params);
}

/*
 * The code in this section is used by "worker" instances to service
 * a single connection from a client. The worker talks to the client
 * on stdin and stdout.
 */

enum worker_result {
	/*
	 * Operation successful.
	 * Caller *might* keep the socket open and allow keep-alive.
	 */
	WR_OK = 0,

	/*
	 * Fatal error that is not recoverable.
	 * Close the socket and clean up.
	 * Exit child-process with non-zero status.
	 */
	WR_FATAL_ERROR = 1,
};

static enum worker_result worker(void)
{
	const char *response = "HTTP/1.1 501 Not Implemented\r\n";
	char *client_addr = getenv("REMOTE_ADDR");
	char *client_port = getenv("REMOTE_PORT");
	enum worker_result wr = WR_OK;

	if (client_addr)
		loginfo("Connection from %s:%s", client_addr, client_port);

	set_keep_alive(0, logerror);

	while (1) {
		if (write_in_full(STDOUT_FILENO, response, strlen(response)) < 0) {
			logerror("unable to write response");
			wr = WR_FATAL_ERROR;
		}

		if (wr != WR_OK)
			break;
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);

	/* Only WR_OK should result in a non-zero exit code */
	return wr != WR_OK;
}

static int max_connections = 32;

static unsigned int live_children;

static struct child *first_child;

static struct strvec cld_argv = STRVEC_INIT;
static void handle(int incoming, struct sockaddr *addr, socklen_t addrlen)
{
	struct child_process cld = CHILD_PROCESS_INIT;

	if (max_connections && live_children >= max_connections) {
		kill_some_child(first_child);
		sleep(1);  /* give it some time to die */
		check_dead_children(first_child, &live_children, loginfo);
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
		strvec_pushf(&cld.env, "REMOTE_ADDR=%s", buf);
		strvec_pushf(&cld.env, "REMOTE_PORT=%d",
				 ntohs(sin_addr->sin_port));
#ifndef NO_IPV6
	} else if (addr->sa_family == AF_INET6) {
		char buf[128] = "";
		struct sockaddr_in6 *sin6_addr = (void *) addr;
		inet_ntop(AF_INET6, &sin6_addr->sin6_addr, buf, sizeof(buf));
		strvec_pushf(&cld.env, "REMOTE_ADDR=[%s]", buf);
		strvec_pushf(&cld.env, "REMOTE_PORT=%d",
				 ntohs(sin6_addr->sin6_port));
#endif
	}

	strvec_pushv(&cld.args, cld_argv.v);
	cld.in = incoming;
	cld.out = dup(incoming);

	if (cld.out < 0)
		logerror("could not dup() `incoming`");
	else if (start_command(&cld))
		logerror("unable to fork");
	else
		add_child(&cld, addr, addrlen, first_child, &live_children);
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
		int nr_ready;
		int timeout = (pid_file ? 100 : -1);

		check_dead_children(first_child, &live_children, loginfo);

		nr_ready = poll(pfd, socklist->nr, timeout);
		if (nr_ready < 0) {
			if (errno != EINTR) {
				logerror("Poll failed, resuming: %s",
				      strerror(errno));
				sleep(1);
			}
			continue;
		}
		else if (nr_ready == 0) {
			/*
			 * If we have a pid_file, then we watch it.
			 * If someone deletes it, we shutdown the service.
			 * The shell scripts in the test suite will use this.
			 */
			if (!pid_file || file_exists(pid_file))
				continue;
			goto shutdown;
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

shutdown:
	loginfo("Starting graceful shutdown (pid-file gone)");
	for (i = 0; i < socklist->nr; i++)
		close(socklist->list[i]);

	return 0;
}

static int serve(struct string_list *listen_addr, int listen_port)
{
	struct socketlist socklist = { NULL, 0, 0 };

	socksetup(listen_addr, listen_port, &socklist, reuseaddr, logerror);
	if (socklist.nr == 0)
		die("unable to allocate any listen sockets on port %u",
		    listen_port);

	loginfo("Ready to rumble");

	/*
	 * Wait to create the pid-file until we've setup the sockets
	 * and are open for business.
	 */
	if (pid_file)
		write_file(pid_file, "%"PRIuMAX, (uintmax_t) getpid());

	return service_loop(&socklist);
}

/*
 * This section is executed by both the primary instance and all
 * worker instances.  So, yes, each child-process re-parses the
 * command line argument and re-discovers how it should behave.
 */

int cmd_main(int argc, const char **argv)
{
	int listen_port = 0;
	struct string_list listen_addr = STRING_LIST_INIT_NODUP;
	int worker_mode = 0;
	int i;

	trace2_cmd_name("test-http-server");
	trace2_cmd_list_config();
	trace2_cmd_list_env_vars();
	setup_git_directory_gently(NULL);

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
		if (!strcmp(arg, "--worker")) {
			worker_mode = 1;
			trace2_cmd_mode("worker");
			continue;
		}
		if (!strcmp(arg, "--verbose")) {
			verbose = 1;
			continue;
		}
		if (skip_prefix(arg, "--timeout=", &v)) {
			timeout = atoi(v);
			continue;
		}
		if (skip_prefix(arg, "--max-connections=", &v)) {
			max_connections = atoi(v);
			if (max_connections < 0)
				max_connections = 0; /* unlimited */
			continue;
		}
		if (!strcmp(arg, "--reuseaddr")) {
			reuseaddr = 1;
			continue;
		}
		if (skip_prefix(arg, "--pid-file=", &v)) {
			pid_file = v;
			continue;
		}

		fprintf(stderr, "error: unknown argument '%s'\n", arg);
		usage(test_http_auth_usage);
	}

	/* avoid splitting a message in the middle */
	setvbuf(stderr, NULL, _IOFBF, 4096);

	if (listen_port == 0)
		listen_port = DEFAULT_GIT_PORT;

	/*
	 * If no --listen=<addr> args are given, the setup_named_sock()
	 * code will use receive a NULL address and set INADDR_ANY.
	 * This exposes both internal and external interfaces on the
	 * port.
	 *
	 * Disallow that and default to the internal-use-only loopback
	 * address.
	 */
	if (!listen_addr.nr)
		string_list_append(&listen_addr, "127.0.0.1");

	/*
	 * worker_mode is set in our own child process instances
	 * (that are bound to a connected socket from a client).
	 */
	if (worker_mode)
		return worker();

	/*
	 * `cld_argv` is a bit of a clever hack. The top-level instance
	 * of test-http-server does the normal bind/listen/accept stuff.
	 * For each incoming socket, the top-level process spawns
	 * a child instance of test-http-server *WITH* the additional
	 * `--worker` argument. This causes the child to set `worker_mode`
	 * and immediately call `worker()` using the connected socket (and
	 * without the usual need for fork() or threads).
	 *
	 * The magic here is made possible because `cld_argv` is static
	 * and handle() (called by service_loop()) knows about it.
	 */
	strvec_push(&cld_argv, argv[0]);
	strvec_push(&cld_argv, "--worker");
	for (i = 1; i < argc; ++i)
		strvec_push(&cld_argv, argv[i]);

	/*
	 * Setup primary instance to listen for connections.
	 */
	return serve(&listen_addr, listen_port);
}
