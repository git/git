#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "object.h"
#include "tag.h"
#include "exec_cmd.h"
#include "run-command.h"
#include "string-list.h"
#include "url.h"

static const char content_type[] = "Content-Type";
static const char content_length[] = "Content-Length";
static const char last_modified[] = "Last-Modified";
static int getanyfile = 1;

static struct string_list *query_params;

struct rpc_service {
	const char *name;
	const char *config_name;
	signed enabled : 2;
};

static struct rpc_service rpc_service[] = {
	{ "upload-pack", "uploadpack", 1 },
	{ "receive-pack", "receivepack", -1 },
};

static struct string_list *get_parameters(void)
{
	if (!query_params) {
		const char *query = getenv("QUERY_STRING");

		query_params = xcalloc(1, sizeof(*query_params));
		while (query && *query) {
			char *name = url_decode_parameter_name(&query);
			char *value = url_decode_parameter_value(&query);
			struct string_list_item *i;

			i = string_list_lookup(query_params, name);
			if (!i)
				i = string_list_insert(query_params, name);
			else
				free(i->util);
			i->util = value;
		}
	}
	return query_params;
}

static const char *get_parameter(const char *name)
{
	struct string_list_item *i;
	i = string_list_lookup(get_parameters(), name);
	return i ? i->util : NULL;
}

__attribute__((format (printf, 2, 3)))
static void format_write(int fd, const char *fmt, ...)
{
	static char buffer[1024];

	va_list args;
	unsigned n;

	va_start(args, fmt);
	n = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	if (n >= sizeof(buffer))
		die("protocol error: impossibly long line");

	safe_write(fd, buffer, n);
}

static void http_status(unsigned code, const char *msg)
{
	format_write(1, "Status: %u %s\r\n", code, msg);
}

static void hdr_str(const char *name, const char *value)
{
	format_write(1, "%s: %s\r\n", name, value);
}

static void hdr_int(const char *name, uintmax_t value)
{
	format_write(1, "%s: %" PRIuMAX "\r\n", name, value);
}

static void hdr_date(const char *name, unsigned long when)
{
	const char *value = show_date(when, 0, DATE_RFC2822);
	hdr_str(name, value);
}

static void hdr_nocache(void)
{
	hdr_str("Expires", "Fri, 01 Jan 1980 00:00:00 GMT");
	hdr_str("Pragma", "no-cache");
	hdr_str("Cache-Control", "no-cache, max-age=0, must-revalidate");
}

static void hdr_cache_forever(void)
{
	unsigned long now = time(NULL);
	hdr_date("Date", now);
	hdr_date("Expires", now + 31536000);
	hdr_str("Cache-Control", "public, max-age=31536000");
}

static void end_headers(void)
{
	safe_write(1, "\r\n", 2);
}

__attribute__((format (printf, 1, 2)))
static NORETURN void not_found(const char *err, ...)
{
	va_list params;

	http_status(404, "Not Found");
	hdr_nocache();
	end_headers();

	va_start(params, err);
	if (err && *err)
		vfprintf(stderr, err, params);
	va_end(params);
	exit(0);
}

__attribute__((format (printf, 1, 2)))
static NORETURN void forbidden(const char *err, ...)
{
	va_list params;

	http_status(403, "Forbidden");
	hdr_nocache();
	end_headers();

	va_start(params, err);
	if (err && *err)
		vfprintf(stderr, err, params);
	va_end(params);
	exit(0);
}

static void select_getanyfile(void)
{
	if (!getanyfile)
		forbidden("Unsupported service: getanyfile");
}

static void send_strbuf(const char *type, struct strbuf *buf)
{
	hdr_int(content_length, buf->len);
	hdr_str(content_type, type);
	end_headers();
	safe_write(1, buf->buf, buf->len);
}

static void send_local_file(const char *the_type, const char *name)
{
	const char *p = git_path("%s", name);
	size_t buf_alloc = 8192;
	char *buf = xmalloc(buf_alloc);
	int fd;
	struct stat sb;

	fd = open(p, O_RDONLY);
	if (fd < 0)
		not_found("Cannot open '%s': %s", p, strerror(errno));
	if (fstat(fd, &sb) < 0)
		die_errno("Cannot stat '%s'", p);

	hdr_int(content_length, sb.st_size);
	hdr_str(content_type, the_type);
	hdr_date(last_modified, sb.st_mtime);
	end_headers();

	for (;;) {
		ssize_t n = xread(fd, buf, buf_alloc);
		if (n < 0)
			die_errno("Cannot read '%s'", p);
		if (!n)
			break;
		safe_write(1, buf, n);
	}
	close(fd);
	free(buf);
}

static void get_text_file(char *name)
{
	select_getanyfile();
	hdr_nocache();
	send_local_file("text/plain", name);
}

static void get_loose_object(char *name)
{
	select_getanyfile();
	hdr_cache_forever();
	send_local_file("application/x-git-loose-object", name);
}

static void get_pack_file(char *name)
{
	select_getanyfile();
	hdr_cache_forever();
	send_local_file("application/x-git-packed-objects", name);
}

static void get_idx_file(char *name)
{
	select_getanyfile();
	hdr_cache_forever();
	send_local_file("application/x-git-packed-objects-toc", name);
}

static int http_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "http.getanyfile")) {
		getanyfile = git_config_bool(var, value);
		return 0;
	}

	if (!prefixcmp(var, "http.")) {
		int i;

		for (i = 0; i < ARRAY_SIZE(rpc_service); i++) {
			struct rpc_service *svc = &rpc_service[i];
			if (!strcmp(var + 5, svc->config_name)) {
				svc->enabled = git_config_bool(var, value);
				return 0;
			}
		}
	}

	/* we are not interested in parsing any other configuration here */
	return 0;
}

static struct rpc_service *select_service(const char *name)
{
	struct rpc_service *svc = NULL;
	int i;

	if (prefixcmp(name, "git-"))
		forbidden("Unsupported service: '%s'", name);

	for (i = 0; i < ARRAY_SIZE(rpc_service); i++) {
		struct rpc_service *s = &rpc_service[i];
		if (!strcmp(s->name, name + 4)) {
			svc = s;
			break;
		}
	}

	if (!svc)
		forbidden("Unsupported service: '%s'", name);

	if (svc->enabled < 0) {
		const char *user = getenv("REMOTE_USER");
		svc->enabled = (user && *user) ? 1 : 0;
	}
	if (!svc->enabled)
		forbidden("Service not enabled: '%s'", svc->name);
	return svc;
}

static void inflate_request(const char *prog_name, int out)
{
	git_zstream stream;
	unsigned char in_buf[8192];
	unsigned char out_buf[8192];
	unsigned long cnt = 0;

	memset(&stream, 0, sizeof(stream));
	git_inflate_init_gzip_only(&stream);

	while (1) {
		ssize_t n = xread(0, in_buf, sizeof(in_buf));
		if (n <= 0)
			die("request ended in the middle of the gzip stream");

		stream.next_in = in_buf;
		stream.avail_in = n;

		while (0 < stream.avail_in) {
			int ret;

			stream.next_out = out_buf;
			stream.avail_out = sizeof(out_buf);

			ret = git_inflate(&stream, Z_NO_FLUSH);
			if (ret != Z_OK && ret != Z_STREAM_END)
				die("zlib error inflating request, result %d", ret);

			n = stream.total_out - cnt;
			if (write_in_full(out, out_buf, n) != n)
				die("%s aborted reading request", prog_name);
			cnt += n;

			if (ret == Z_STREAM_END)
				goto done;
		}
	}

done:
	git_inflate_end(&stream);
	close(out);
}

static void run_service(const char **argv)
{
	const char *encoding = getenv("HTTP_CONTENT_ENCODING");
	const char *user = getenv("REMOTE_USER");
	const char *host = getenv("REMOTE_ADDR");
	char *env[3];
	struct strbuf buf = STRBUF_INIT;
	int gzipped_request = 0;
	struct child_process cld;

	if (encoding && !strcmp(encoding, "gzip"))
		gzipped_request = 1;
	else if (encoding && !strcmp(encoding, "x-gzip"))
		gzipped_request = 1;

	if (!user || !*user)
		user = "anonymous";
	if (!host || !*host)
		host = "(none)";

	memset(&env, 0, sizeof(env));
	strbuf_addf(&buf, "GIT_COMMITTER_NAME=%s", user);
	env[0] = strbuf_detach(&buf, NULL);

	strbuf_addf(&buf, "GIT_COMMITTER_EMAIL=%s@http.%s", user, host);
	env[1] = strbuf_detach(&buf, NULL);
	env[2] = NULL;

	memset(&cld, 0, sizeof(cld));
	cld.argv = argv;
	cld.env = (const char *const *)env;
	if (gzipped_request)
		cld.in = -1;
	cld.git_cmd = 1;
	if (start_command(&cld))
		exit(1);

	close(1);
	if (gzipped_request)
		inflate_request(argv[0], cld.in);
	else
		close(0);

	if (finish_command(&cld))
		exit(1);
	free(env[0]);
	free(env[1]);
	strbuf_release(&buf);
}

static int show_text_ref(const char *name, const unsigned char *sha1,
	int flag, void *cb_data)
{
	struct strbuf *buf = cb_data;
	struct object *o = parse_object(sha1);
	if (!o)
		return 0;

	strbuf_addf(buf, "%s\t%s\n", sha1_to_hex(sha1), name);
	if (o->type == OBJ_TAG) {
		o = deref_tag(o, name, 0);
		if (!o)
			return 0;
		strbuf_addf(buf, "%s\t%s^{}\n", sha1_to_hex(o->sha1), name);
	}
	return 0;
}

static void get_info_refs(char *arg)
{
	const char *service_name = get_parameter("service");
	struct strbuf buf = STRBUF_INIT;

	hdr_nocache();

	if (service_name) {
		const char *argv[] = {NULL /* service name */,
			"--stateless-rpc", "--advertise-refs",
			".", NULL};
		struct rpc_service *svc = select_service(service_name);

		strbuf_addf(&buf, "application/x-git-%s-advertisement",
			svc->name);
		hdr_str(content_type, buf.buf);
		end_headers();

		packet_write(1, "# service=git-%s\n", svc->name);
		packet_flush(1);

		argv[0] = svc->name;
		run_service(argv);

	} else {
		select_getanyfile();
		for_each_ref(show_text_ref, &buf);
		send_strbuf("text/plain", &buf);
	}
	strbuf_release(&buf);
}

static void get_info_packs(char *arg)
{
	size_t objdirlen = strlen(get_object_directory());
	struct strbuf buf = STRBUF_INIT;
	struct packed_git *p;
	size_t cnt = 0;

	select_getanyfile();
	prepare_packed_git();
	for (p = packed_git; p; p = p->next) {
		if (p->pack_local)
			cnt++;
	}

	strbuf_grow(&buf, cnt * 53 + 2);
	for (p = packed_git; p; p = p->next) {
		if (p->pack_local)
			strbuf_addf(&buf, "P %s\n", p->pack_name + objdirlen + 6);
	}
	strbuf_addch(&buf, '\n');

	hdr_nocache();
	send_strbuf("text/plain; charset=utf-8", &buf);
	strbuf_release(&buf);
}

static void check_content_type(const char *accepted_type)
{
	const char *actual_type = getenv("CONTENT_TYPE");

	if (!actual_type)
		actual_type = "";

	if (strcmp(actual_type, accepted_type)) {
		http_status(415, "Unsupported Media Type");
		hdr_nocache();
		end_headers();
		format_write(1,
			"Expected POST with Content-Type '%s',"
			" but received '%s' instead.\n",
			accepted_type, actual_type);
		exit(0);
	}
}

static void service_rpc(char *service_name)
{
	const char *argv[] = {NULL, "--stateless-rpc", ".", NULL};
	struct rpc_service *svc = select_service(service_name);
	struct strbuf buf = STRBUF_INIT;

	strbuf_reset(&buf);
	strbuf_addf(&buf, "application/x-git-%s-request", svc->name);
	check_content_type(buf.buf);

	hdr_nocache();

	strbuf_reset(&buf);
	strbuf_addf(&buf, "application/x-git-%s-result", svc->name);
	hdr_str(content_type, buf.buf);

	end_headers();

	argv[0] = svc->name;
	run_service(argv);
	strbuf_release(&buf);
}

static NORETURN void die_webcgi(const char *err, va_list params)
{
	static int dead;

	if (!dead) {
		dead = 1;
		http_status(500, "Internal Server Error");
		hdr_nocache();
		end_headers();

		vreportf("fatal: ", err, params);
	}
	exit(0); /* we successfully reported a failure ;-) */
}

static char* getdir(void)
{
	struct strbuf buf = STRBUF_INIT;
	char *pathinfo = getenv("PATH_INFO");
	char *root = getenv("GIT_PROJECT_ROOT");
	char *path = getenv("PATH_TRANSLATED");

	if (root && *root) {
		if (!pathinfo || !*pathinfo)
			die("GIT_PROJECT_ROOT is set but PATH_INFO is not");
		if (daemon_avoid_alias(pathinfo))
			die("'%s': aliased", pathinfo);
		end_url_with_slash(&buf, root);
		if (pathinfo[0] == '/')
			pathinfo++;
		strbuf_addstr(&buf, pathinfo);
		return strbuf_detach(&buf, NULL);
	} else if (path && *path) {
		return xstrdup(path);
	} else
		die("No GIT_PROJECT_ROOT or PATH_TRANSLATED from server");
	return NULL;
}

static struct service_cmd {
	const char *method;
	const char *pattern;
	void (*imp)(char *);
} services[] = {
	{"GET", "/HEAD$", get_text_file},
	{"GET", "/info/refs$", get_info_refs},
	{"GET", "/objects/info/alternates$", get_text_file},
	{"GET", "/objects/info/http-alternates$", get_text_file},
	{"GET", "/objects/info/packs$", get_info_packs},
	{"GET", "/objects/[0-9a-f]{2}/[0-9a-f]{38}$", get_loose_object},
	{"GET", "/objects/pack/pack-[0-9a-f]{40}\\.pack$", get_pack_file},
	{"GET", "/objects/pack/pack-[0-9a-f]{40}\\.idx$", get_idx_file},

	{"POST", "/git-upload-pack$", service_rpc},
	{"POST", "/git-receive-pack$", service_rpc}
};

int main(int argc, char **argv)
{
	char *method = getenv("REQUEST_METHOD");
	char *dir;
	struct service_cmd *cmd = NULL;
	char *cmd_arg = NULL;
	int i;

	git_setup_gettext();

	git_extract_argv0_path(argv[0]);
	set_die_routine(die_webcgi);

	if (!method)
		die("No REQUEST_METHOD from server");
	if (!strcmp(method, "HEAD"))
		method = "GET";
	dir = getdir();

	for (i = 0; i < ARRAY_SIZE(services); i++) {
		struct service_cmd *c = &services[i];
		regex_t re;
		regmatch_t out[1];

		if (regcomp(&re, c->pattern, REG_EXTENDED))
			die("Bogus regex in service table: %s", c->pattern);
		if (!regexec(&re, dir, 1, out, 0)) {
			size_t n;

			if (strcmp(method, c->method)) {
				const char *proto = getenv("SERVER_PROTOCOL");
				if (proto && !strcmp(proto, "HTTP/1.1"))
					http_status(405, "Method Not Allowed");
				else
					http_status(400, "Bad Request");
				hdr_nocache();
				end_headers();
				return 0;
			}

			cmd = c;
			n = out[0].rm_eo - out[0].rm_so;
			cmd_arg = xmalloc(n);
			memcpy(cmd_arg, dir + out[0].rm_so + 1, n-1);
			cmd_arg[n-1] = '\0';
			dir[out[0].rm_so] = 0;
			break;
		}
		regfree(&re);
	}

	if (!cmd)
		not_found("Request not supported: '%s'", dir);

	setup_path();
	if (!enter_repo(dir, 0))
		not_found("Not a git repository: '%s'", dir);
	if (!getenv("GIT_HTTP_EXPORT_ALL") &&
	    access("git-daemon-export-ok", F_OK) )
		not_found("Repository not exported: '%s'", dir);

	git_config(http_config, NULL);
	cmd->imp(cmd_arg);
	return 0;
}
