#include "cache.h"
#include "credential.h"
#include "string-list.h"
#include "parse-options.h"
#include "unix-socket.h"
#include "run-command.h"

static int send_request(const char *socket, const struct strbuf *out)
{
	int got_data = 0;
	int fd = unix_stream_connect(socket);

	if (fd < 0)
		return -1;

	if (write_in_full(fd, out->buf, out->len) < 0)
		die_errno("unable to write to cache daemon");
	shutdown(fd, SHUT_WR);

	while (1) {
		char in[1024];
		int r;

		r = read_in_full(fd, in, sizeof(in));
		if (r == 0)
			break;
		if (r < 0)
			die_errno("read error from cache daemon");
		write_or_die(1, in, r);
		got_data = 1;
	}
	return got_data;
}

static void out_str(struct strbuf *out, const char *key, const char *value)
{
	if (!value)
		return;
	strbuf_addf(out, "%s=%s", key, value);
	strbuf_addch(out, '\0');
}

static void out_int(struct strbuf *out, const char *key, int value)
{
	strbuf_addf(out, "%s=%d", key, value);
	strbuf_addch(out, '\0');
}

static int do_cache(const char *socket, const char *action,
		    const struct credential *c, int timeout)
{
	struct strbuf buf = STRBUF_INIT;
	int ret;

	out_str(&buf, "action", action);
	if (c) {
		out_str(&buf, "unique", c->unique);
		out_str(&buf, "username", c->username);
		out_str(&buf, "password", c->password);
	}
	if (timeout > 0)
		out_int(&buf, "timeout", timeout);

	ret = send_request(socket, &buf);

	strbuf_release(&buf);
	return ret;
}

static void spawn_daemon(const char *socket)
{
	struct child_process daemon;
	const char *argv[] = { NULL, NULL, NULL };
	char buf[128];
	int r;

	memset(&daemon, 0, sizeof(daemon));
	argv[0] = "git-credential-cache--daemon";
	argv[1] = socket;
	daemon.argv = argv;
	daemon.no_stdin = 1;
	daemon.out = -1;

	if (start_command(&daemon))
		die_errno("unable to start cache daemon");
	r = read_in_full(daemon.out, buf, sizeof(buf));
	if (r < 0)
		die_errno("unable to read result code from cache daemon");
	if (r != 3 || memcmp(buf, "ok\n", 3))
		die("cache daemon did not start: %.*s", r, buf);
	close(daemon.out);
}

int main(int argc, const char **argv)
{
	struct credential c = { NULL };
	char *socket_path = NULL;
	int timeout = 900;
	struct string_list chain = STRING_LIST_INIT_NODUP;
	int exit_mode = 0;
	int reject_mode = 0;
	const char * const usage[] = {
		"git credential-cache [options]",
		NULL
	};
	struct option options[] = {
		OPT_BOOLEAN(0, "exit", &exit_mode,
			    "tell a running daemon to exit"),
		OPT_BOOLEAN(0, "reject", &reject_mode,
			    "reject a cached credential"),
		OPT_INTEGER(0, "timeout", &timeout,
			    "number of seconds to cache credentials"),
		OPT_STRING(0, "socket", &socket_path, "path",
			   "path of cache-daemon socket"),
		OPT_STRING_LIST(0, "chain", &chain, "helper",
				"use <helper> to get non-cached credentials"),
		OPT_STRING(0, "username", &c.username, "name",
			   "an existing username"),
		OPT_STRING(0, "description", &c.description, "desc",
			   "human-readable description of the credential"),
		OPT_STRING(0, "unique", &c.unique, "token",
			   "a unique context for the credential"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options, usage, 0);
	if (argc)
		usage_with_options(usage, options);
	/* credential_reject wants to free() these */
	if (c.username)
		c.username = xstrdup(c.username);
	if (c.password)
		c.password = xstrdup(c.password);

	if (!socket_path)
		socket_path = expand_user_path("~/.git-credential-cache/socket");
	if (!socket_path)
		die("unable to find a suitable socket path; use --socket");

	if (exit_mode) {
		do_cache(socket_path, "exit", NULL, -1);
		return 0;
	}

	if (reject_mode) {
		do_cache(socket_path, "erase", &c, -1);
		credential_reject(&c, &chain);
		return 0;
	}

	if (do_cache(socket_path, "get", &c, -1) > 0)
		return 0;

	credential_fill(&c, &chain);
	printf("username=%s\n", c.username);
	printf("password=%s\n", c.password);

	if (do_cache(socket_path, "store", &c, timeout) < 0) {
		spawn_daemon(socket_path);
		do_cache(socket_path, "store", &c, timeout);
	}
	return 0;
}
