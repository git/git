#include "daemon-utils.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"
#include "trace2.h"
#include "version.h"
#include "dir.h"
#include "date.h"
#include "config.h"

#define TR2_CAT "test-http-server"

static const char *pid_file;
static int verbose;
static int reuseaddr;

static const char test_http_auth_usage[] =
"http-server [--verbose]\n"
"           [--timeout=<n>] [--max-connections=<n>]\n"
"           [--reuseaddr] [--pid-file=<file>]\n"
"           [--listen=<host_or_ipaddr>]* [--port=<n>]\n"
"           [--auth-config=<file>]\n"
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

	/*
	 * Close the socket and clean up. Does not imply an error.
	 */
	WR_HANGUP = 2,

	/*
	 * Unexpected request message or error in request parsing.
	 * Respond with an 400 error. Close the socket and cleanup.
	 * Exit child-process with a non-zero status.
	 */
	WR_CLIENT_ERROR = 3,
};

/*
 * Fields from a parsed HTTP request.
 */
struct req {
	struct strbuf start_line;

	const char *method;
	const char *http_version;

	struct strbuf uri_path;
	struct strbuf query_args;

	struct string_list header_list;
	const char *content_type;
	uintmax_t content_length;
	unsigned has_content_length:1;
};

#define REQ__INIT { \
	.start_line = STRBUF_INIT, \
	.uri_path = STRBUF_INIT, \
	.query_args = STRBUF_INIT, \
	.header_list = STRING_LIST_INIT_NODUP, \
	.content_type = NULL, \
	.content_length = 0, \
	.has_content_length = 0, \
}

static void req__release(struct req *req)
{
	strbuf_release(&req->start_line);

	strbuf_release(&req->uri_path);
	strbuf_release(&req->query_args);

	string_list_clear(&req->header_list, 0);
}

static enum worker_result send_http_error(int fd, int http_code,
					  const char *http_code_name,
					  int retry_after_seconds,
					  struct string_list *response_headers,
					  enum worker_result wr_in)
{
	struct strbuf response_header = STRBUF_INIT;
	struct strbuf response_content = STRBUF_INIT;
	struct string_list_item *h;
	enum worker_result wr;

	strbuf_addf(&response_content, "Error: %d %s\r\n", http_code,
		    http_code_name);

	if (retry_after_seconds > 0)
		strbuf_addf(&response_content, "Retry-After: %d\r\n",
			    retry_after_seconds);

	strbuf_addf(&response_header, "HTTP/1.1 %d %s\r\n", http_code,
		    http_code_name);
	strbuf_addstr(&response_header, "Cache-Control: private\r\n");
	strbuf_addstr(&response_header, "Content-Type: text/plain\r\n");
	strbuf_addf(&response_header, "Content-Length: %"PRIuMAX"\r\n",
		    (uintmax_t)response_content.len);

	if (retry_after_seconds > 0)
		strbuf_addf(&response_header, "Retry-After: %d\r\n",
			    retry_after_seconds);

	strbuf_addf(&response_header, "Server: test-http-server/%s\r\n",
		    git_version_string);
	strbuf_addf(&response_header, "Date: %s\r\n", show_date(time(NULL), 0,
		    DATE_MODE(RFC2822)));

	if (response_headers)
		for_each_string_list_item(h, response_headers)
			strbuf_addf(&response_header, "%s\r\n", h->string);
	strbuf_addstr(&response_header, "\r\n");

	if (write_in_full(fd, response_header.buf, response_header.len) < 0) {
		logerror("unable to write response header");
		wr = WR_FATAL_ERROR;
		goto done;
	}

	if (write_in_full(fd, response_content.buf, response_content.len) < 0) {
		logerror("unable to write response content body");
		wr = WR_FATAL_ERROR;
		goto done;
	}

	wr = wr_in;

done:
	strbuf_release(&response_header);
	strbuf_release(&response_content);

	return wr;
}

/*
 * Read the HTTP request up to the start of the optional message-body.
 * We do this byte-by-byte because we have keep-alive turned on and
 * cannot rely on an EOF.
 *
 * https://tools.ietf.org/html/rfc7230
 *
 * We cannot call die() here because our caller needs to properly
 * respond to the client and/or close the socket before this
 * child exits so that the client doesn't get a connection reset
 * by peer error.
 */
static enum worker_result req__read(struct req *req, int fd)
{
	struct strbuf h = STRBUF_INIT;
	struct string_list start_line_fields = STRING_LIST_INIT_DUP;
	int nr_start_line_fields;
	const char *uri_target;
	const char *query;
	char *hp;
	const char *hv;

	enum worker_result result = WR_OK;

	/*
	 * Read line 0 of the request and split it into component parts:
	 *
	 *    <method> SP <uri-target> SP <HTTP-version> CRLF
	 *
	 */
	if (strbuf_getwholeline_fd(&req->start_line, fd, '\n') == EOF) {
		result = WR_HANGUP;
		goto done;
	}

	strbuf_trim_trailing_newline(&req->start_line);

	nr_start_line_fields = string_list_split(&start_line_fields,
						 req->start_line.buf,
						 ' ', -1);
	if (nr_start_line_fields != 3) {
		logerror("could not parse request start-line '%s'",
			 req->start_line.buf);
		result = WR_CLIENT_ERROR;
		goto done;
	}

	req->method = xstrdup(start_line_fields.items[0].string);
	req->http_version = xstrdup(start_line_fields.items[2].string);

	uri_target = start_line_fields.items[1].string;

	if (strcmp(req->http_version, "HTTP/1.1")) {
		logerror("unsupported version '%s' (expecting HTTP/1.1)",
			 req->http_version);
		result = WR_CLIENT_ERROR;
		goto done;
	}

	query = strchr(uri_target, '?');

	if (query) {
		strbuf_add(&req->uri_path, uri_target, (query - uri_target));
		strbuf_trim_trailing_dir_sep(&req->uri_path);
		strbuf_addstr(&req->query_args, query + 1);
	} else {
		strbuf_addstr(&req->uri_path, uri_target);
		strbuf_trim_trailing_dir_sep(&req->uri_path);
	}

	/*
	 * Read the set of HTTP headers into a string-list.
	 */
	while (1) {
		if (strbuf_getwholeline_fd(&h, fd, '\n') == EOF)
			goto done;
		strbuf_trim_trailing_newline(&h);

		if (!h.len)
			goto done; /* a blank line ends the header */

		hp = strbuf_detach(&h, NULL);
		string_list_append(&req->header_list, hp);

		/* also store common request headers as struct req members */
		if (skip_iprefix(hp, "Content-Type: ", &hv)) {
			req->content_type = hv;
		} else if (skip_iprefix(hp, "Content-Length: ", &hv)) {
			/*
			 * Content-Length is always non-negative, but has no
			 * upper bound according to RFC 7230 (§3.3.2).
			 */
			intmax_t len = 0;
			if (sscanf(hv, "%"PRIdMAX, &len) != 1 || len < 0 ||
			    len == INTMAX_MAX) {
				logerror("invalid content-length: '%s'", hv);
				result = WR_CLIENT_ERROR;
				goto done;
			}

			req->content_length = (uintmax_t)len;
			req->has_content_length = 1;
		}
	}

	/*
	 * We do not attempt to read the <message-body>, if it exists.
	 * We let our caller read/chunk it in as appropriate.
	 */

done:
	string_list_clear(&start_line_fields, 0);

	/*
	 * This is useful for debugging the request, but very noisy.
	 */
	if (trace2_is_enabled()) {
		struct string_list_item *item;
		trace2_printf("%s: %s", TR2_CAT, req->start_line.buf);
		trace2_printf("%s: hver: %s", TR2_CAT, req->http_version);
		trace2_printf("%s: hmth: %s", TR2_CAT, req->method);
		trace2_printf("%s: path: %s", TR2_CAT, req->uri_path.buf);
		trace2_printf("%s: qury: %s", TR2_CAT, req->query_args.buf);
		if (req->has_content_length)
			trace2_printf("%s: clen: %"PRIuMAX, TR2_CAT,
				      req->content_length);
		if (req->content_type)
			trace2_printf("%s: ctyp: %s", TR2_CAT, req->content_type);
		for_each_string_list_item(item, &req->header_list)
			trace2_printf("%s: hdrs: %s", TR2_CAT, item->string);
	}

	return result;
}

static int is_git_request(struct req *req)
{
	static regex_t *smart_http_regex;
	static int initialized;

	if (!initialized) {
		smart_http_regex = xmalloc(sizeof(*smart_http_regex));
		/*
		 * This regular expression matches all dumb and smart HTTP
		 * requests that are currently in use, and defined in
		 * Documentation/gitprotocol-http.txt.
		 *
		 */
		if (regcomp(smart_http_regex, "^/(HEAD|info/refs|"
			    "objects/info/[^/]+|git-(upload|receive)-pack)$",
			    REG_EXTENDED)) {
			warning("could not compile smart HTTP regex");
			smart_http_regex = NULL;
		}
		initialized = 1;
	}

	return smart_http_regex &&
		!regexec(smart_http_regex, req->uri_path.buf, 0, NULL, 0);
}

static enum worker_result do__git(struct req *req, const char *user)
{
	const char *ok = "HTTP/1.1 200 OK\r\n";
	struct child_process cp = CHILD_PROCESS_INIT;
	int res;

	/*
	 * Note that we always respond with a 200 OK response even if the
	 * http-backend process exits with an error. This helper is intended
	 * only to be used to exercise the HTTP auth handling in the Git client,
	 * and specifically around authentication (not handled by http-backend).
	 *
	 * If we wanted to respond with a more 'valid' HTTP response status then
	 * we'd need to buffer the output of http-backend, wait for and grok the
	 * exit status of the process, then write the HTTP status line followed
	 * by the http-backend output. This is outside of the scope of this test
	 * helper's use at time of writing.
	 *
	 * The important auth responses (401) we are handling prior to getting
	 * to this point.
	 */
	if (write(STDOUT_FILENO, ok, strlen(ok)) < 0)
		return error(_("could not send '%s'"), ok);

	if (user)
		strvec_pushf(&cp.env, "REMOTE_USER=%s", user);

	strvec_pushf(&cp.env, "REQUEST_METHOD=%s", req->method);
	strvec_pushf(&cp.env, "PATH_TRANSLATED=%s", req->uri_path.buf);
	strvec_push(&cp.env, "SERVER_PROTOCOL=HTTP/1.1");
	if (req->query_args.len)
		strvec_pushf(&cp.env, "QUERY_STRING=%s", req->query_args.buf);
	if (req->content_type)
		strvec_pushf(&cp.env, "CONTENT_TYPE=%s", req->content_type);
	if (req->has_content_length)
		strvec_pushf(&cp.env, "CONTENT_LENGTH=%" PRIuMAX,
			(uintmax_t)req->content_length);
	cp.git_cmd = 1;
	strvec_push(&cp.args, "http-backend");
	res = run_command(&cp);
	close(STDOUT_FILENO);
	close(STDIN_FILENO);
	return !!res;
}

enum auth_result {
	/* No auth module matches the request. */
	AUTH_UNKNOWN = 0,

	/* Auth module denied the request. */
	AUTH_DENY = 1,

	/* Auth module successfully validated the request. */
	AUTH_ALLOW = 2,
};

struct auth_module {
	char *scheme;
	char *challenge_params;
	struct string_list *tokens;
};

static int allow_anonymous;
static struct auth_module **auth_modules = NULL;
static size_t auth_modules_nr = 0;
static size_t auth_modules_alloc = 0;
static struct strvec extra_headers = STRVEC_INIT;

static struct auth_module *get_auth_module(const char *scheme, int create)
{
	struct auth_module *mod;
	for (size_t i = 0; i < auth_modules_nr; i++) {
		mod = auth_modules[i];
		if (!strcasecmp(mod->scheme, scheme))
			return mod;
	}

	if (create) {
		struct auth_module *mod = xmalloc(sizeof(struct auth_module));
		mod->scheme = xstrdup(scheme);
		mod->challenge_params = NULL;
		ALLOC_ARRAY(mod->tokens, 1);
		string_list_init_dup(mod->tokens);

		ALLOC_GROW(auth_modules, auth_modules_nr + 1, auth_modules_alloc);
		auth_modules[auth_modules_nr++] = mod;

		return mod;
	}

	return NULL;
}

static int is_authed(struct req *req, const char **user, enum worker_result *wr)
{
	enum auth_result result = AUTH_UNKNOWN;
	struct string_list hdrs = STRING_LIST_INIT_NODUP;
	struct auth_module *mod;

	struct string_list_item *hdr;
	struct string_list_item *token;
	const char *v;
	struct strbuf **split = NULL;
	int i;
	char *challenge;

	/*
	 * Check all auth modules and try to validate the request.
	 * The first Authorization header that matches a known auth module
	 * scheme will be consulted to either approve or deny the request.
	 * If no module is found, or if there is no valid token, then 401 error.
	 * Otherwise, only permit the request if anonymous auth is enabled.
	 * It's atypical for user agents/clients to send multiple Authorization
	 * headers, but not explicitly forbidden or defined.
	 */
	for_each_string_list_item(hdr, &req->header_list) {
		if (skip_iprefix(hdr->string, "Authorization: ", &v)) {
			split = strbuf_split_str(v, ' ', 2);
			if (split[0] && split[1]) {
				/* trim trailing space ' ' */
				strbuf_rtrim(split[0]);

				mod = get_auth_module(split[0]->buf, 0);
				if (mod) {
					result = AUTH_DENY;

					for_each_string_list_item(token, mod->tokens) {
						if (!strcmp(split[1]->buf, token->string)) {
							result = AUTH_ALLOW;
							break;
						}
					}

					strbuf_list_free(split);
					goto done;
				}
			}

			strbuf_list_free(split);
		}
	}

done:
	switch (result) {
	case AUTH_ALLOW:
		trace2_printf("%s: auth '%s' ALLOW", TR2_CAT, mod->scheme);
		*user = "VALID_TEST_USER";
		*wr = WR_OK;
		break;

	case AUTH_DENY:
		trace2_printf("%s: auth '%s' DENY", TR2_CAT, mod->scheme);
		/* fall-through */

	case AUTH_UNKNOWN:
		if (result != AUTH_DENY && allow_anonymous)
			break;

		for (i = 0; i < auth_modules_nr; i++) {
			mod = auth_modules[i];
			if (mod->challenge_params)
				challenge = xstrfmt("WWW-Authenticate: %s %s",
						    mod->scheme,
						    mod->challenge_params);
			else
				challenge = xstrfmt("WWW-Authenticate: %s",
						    mod->scheme);
			string_list_append(&hdrs, challenge);
		}

		for (i = 0; i < extra_headers.nr; i++)
			string_list_append(&hdrs, extra_headers.v[i]);

		*wr = send_http_error(STDOUT_FILENO, 401, "Unauthorized", -1,
				      &hdrs, *wr);
	}

	string_list_clear(&hdrs, 0);

	return result == AUTH_ALLOW ||
	      (result == AUTH_UNKNOWN && allow_anonymous);
}

static int split_auth_param(const char *str, char **scheme, char **val)
{
	struct strbuf **p = strbuf_split_str(str, ':', 2);

	if (!p[0])
		return -1;

	/* trim trailing ':' */
	if (p[0]->len && p[0]->buf[p[0]->len - 1] == ':')
		strbuf_setlen(p[0], p[0]->len - 1);

	*scheme = strbuf_detach(p[0], NULL);
	*val = p[1] ? strbuf_detach(p[1], NULL) : NULL;

	strbuf_list_free(p);
	return 0;
}

static int read_auth_config(const char *name, const char *val, void *data)
{
	int ret = 0;
	char *scheme = NULL;
	char *token = NULL;
	char *challenge = NULL;
	struct auth_module *mod;

	if (!strcmp(name, "auth.challenge")) {
		if (split_auth_param(val, &scheme, &challenge)) {
			ret = error("invalid auth challenge '%s'", val);
			goto cleanup;
		}

		mod = get_auth_module(scheme, 1);

		/* Replace any existing challenge parameters */
		free(mod->challenge_params);
		mod->challenge_params = challenge ? xstrdup(challenge) : NULL;
	} else if (!strcmp(name, "auth.token")) {
		if (split_auth_param(val, &scheme, &token)) {
			ret = error("invalid auth token '%s'", val);
			goto cleanup;
		}

		mod = get_auth_module(scheme, 1);

		/*
		 * Append to set of valid tokens unless an empty token value
		 * is provided, then clear the existing list.
		 */
		if (token)
			string_list_append(mod->tokens, token);
		else
			string_list_clear(mod->tokens, 1);
	} else if (!strcmp(name, "auth.allowanonymous")) {
		allow_anonymous = git_config_bool(name, val);
	} else if (!strcmp(name, "auth.extraheader")) {
		strvec_push(&extra_headers, val);
	} else {
		warning("unknown auth config '%s'", name);
	}

cleanup:
	free(scheme);
	free(token);
	free(challenge);

	return ret;
}

static enum worker_result dispatch(struct req *req)
{
	enum worker_result wr = WR_OK;
	const char *user = NULL;

	if (!is_authed(req, &user, &wr))
		return wr;

	if (is_git_request(req))
		return do__git(req, user);

	return send_http_error(STDOUT_FILENO, 501, "Not Implemented", -1, NULL,
			       WR_HANGUP);
}

static enum worker_result worker(void)
{
	struct req req = REQ__INIT;
	char *client_addr = getenv("REMOTE_ADDR");
	char *client_port = getenv("REMOTE_PORT");
	enum worker_result wr = WR_OK;

	if (client_addr)
		loginfo("Connection from %s:%s", client_addr, client_port);

	set_keep_alive(0, logerror);

	while (1) {
		req__release(&req);

		alarm(timeout);
		wr = req__read(&req, 0);
		alarm(0);

		if (wr == WR_CLIENT_ERROR)
			wr = send_http_error(STDOUT_FILENO, 400, "Bad Request",
					     -1, NULL, wr);

		if (wr != WR_OK)
			break;

		wr = dispatch(&req);
		if (wr != WR_OK)
			break;
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);

	/* Only WR_OK and WR_HANGUP should result in a non-zero exit code */
	return wr != WR_OK && wr != WR_HANGUP;
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
		if (skip_prefix(arg, "--auth-config=", &v)) {
			if (!strlen(v)) {
				error("invalid argument - missing file path");
				usage(test_http_auth_usage);
			}

			if (git_config_from_file(read_auth_config, v, NULL)) {
				error("failed to read auth config file '%s'", v);
				usage(test_http_auth_usage);
			}

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
