#include "cache.h"
#include "config.h"
#include "repository.h"
#include "refs.h"
#include "pkt-line.h"
#include "object.h"
#include "tag.h"
#include "exec-cmd.h"
#include "run-command.h"
#include "string-list.h"
#include "url.h"
#include "strvec.h"
#include "packfile.h"
#include "object-store.h"
#include "protocol.h"
#include "date.h"

static const char content_type[] = "Content-Type";
static const char content_length[] = "Content-Length";
static const char last_modified[] = "Last-Modified";
static int getanyfile = 1;
static unsigned long max_request_buffer = 10 * 1024 * 1024;

static struct string_list *query_params;

struct rpc_service {
	const char *name;
	const char *config_name;
	unsigned buffer_input : 1;
	signed enabled : 2;
};

static struct rpc_service rpc_service[] = {
	{ "upload-pack", "uploadpack", 1, 1 },
	{ "receive-pack", "receivepack", 0, -1 },
};

static struct string_list *get_parameters(void)
{
	if (!query_params) {
		const char *query = getenv("QUERY_STRING");

		CALLOC_ARRAY(query_params, 1);
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

	write_or_die(fd, buffer, n);
}

static void http_status(struct strbuf *hdr, unsigned code, const char *msg)
{
	strbuf_addf(hdr, "Status: %u %s\r\n", code, msg);
}

static void hdr_str(struct strbuf *hdr, const char *name, const char *value)
{
	strbuf_addf(hdr, "%s: %s\r\n", name, value);
}

static void hdr_int(struct strbuf *hdr, const char *name, uintmax_t value)
{
	strbuf_addf(hdr, "%s: %" PRIuMAX "\r\n", name, value);
}

static void hdr_date(struct strbuf *hdr, const char *name, timestamp_t when)
{
	const char *value = show_date(when, 0, DATE_MODE(RFC2822));
	hdr_str(hdr, name, value);
}

static void hdr_nocache(struct strbuf *hdr)
{
	hdr_str(hdr, "Expires", "Fri, 01 Jan 1980 00:00:00 GMT");
	hdr_str(hdr, "Pragma", "no-cache");
	hdr_str(hdr, "Cache-Control", "no-cache, max-age=0, must-revalidate");
}

static void hdr_cache_forever(struct strbuf *hdr)
{
	timestamp_t now = time(NULL);
	hdr_date(hdr, "Date", now);
	hdr_date(hdr, "Expires", now + 31536000);
	hdr_str(hdr, "Cache-Control", "public, max-age=31536000");
}

static void end_headers(struct strbuf *hdr)
{
	strbuf_add(hdr, "\r\n", 2);
	write_or_die(1, hdr->buf, hdr->len);
	strbuf_release(hdr);
}

__attribute__((format (printf, 2, 3)))
static NORETURN void not_found(struct strbuf *hdr, const char *err, ...)
{
	va_list params;

	http_status(hdr, 404, "Not Found");
	hdr_nocache(hdr);
	end_headers(hdr);

	va_start(params, err);
	if (err && *err)
		vfprintf(stderr, err, params);
	va_end(params);
	exit(0);
}

__attribute__((format (printf, 2, 3)))
static NORETURN void forbidden(struct strbuf *hdr, const char *err, ...)
{
	va_list params;

	http_status(hdr, 403, "Forbidden");
	hdr_nocache(hdr);
	end_headers(hdr);

	va_start(params, err);
	if (err && *err)
		vfprintf(stderr, err, params);
	va_end(params);
	exit(0);
}

static void select_getanyfile(struct strbuf *hdr)
{
	if (!getanyfile)
		forbidden(hdr, "Unsupported service: getanyfile");
}

static void send_strbuf(struct strbuf *hdr,
			const char *type, struct strbuf *buf)
{
	hdr_int(hdr, content_length, buf->len);
	hdr_str(hdr, content_type, type);
	end_headers(hdr);
	write_or_die(1, buf->buf, buf->len);
}

static void send_local_file(struct strbuf *hdr, const char *the_type,
				const char *name)
{
	char *p = git_pathdup("%s", name);
	size_t buf_alloc = 8192;
	char *buf = xmalloc(buf_alloc);
	int fd;
	struct stat sb;

	fd = open(p, O_RDONLY);
	if (fd < 0)
		not_found(hdr, "Cannot open '%s': %s", p, strerror(errno));
	if (fstat(fd, &sb) < 0)
		die_errno("Cannot stat '%s'", p);

	hdr_int(hdr, content_length, sb.st_size);
	hdr_str(hdr, content_type, the_type);
	hdr_date(hdr, last_modified, sb.st_mtime);
	end_headers(hdr);

	for (;;) {
		ssize_t n = xread(fd, buf, buf_alloc);
		if (n < 0)
			die_errno("Cannot read '%s'", p);
		if (!n)
			break;
		write_or_die(1, buf, n);
	}
	close(fd);
	free(buf);
	free(p);
}

static void get_text_file(struct strbuf *hdr, char *name)
{
	select_getanyfile(hdr);
	hdr_nocache(hdr);
	send_local_file(hdr, "text/plain", name);
}

static void get_loose_object(struct strbuf *hdr, char *name)
{
	select_getanyfile(hdr);
	hdr_cache_forever(hdr);
	send_local_file(hdr, "application/x-git-loose-object", name);
}

static void get_pack_file(struct strbuf *hdr, char *name)
{
	select_getanyfile(hdr);
	hdr_cache_forever(hdr);
	send_local_file(hdr, "application/x-git-packed-objects", name);
}

static void get_idx_file(struct strbuf *hdr, char *name)
{
	select_getanyfile(hdr);
	hdr_cache_forever(hdr);
	send_local_file(hdr, "application/x-git-packed-objects-toc", name);
}

static void http_config(void)
{
	int i, value = 0;
	struct strbuf var = STRBUF_INIT;

	git_config_get_bool("http.getanyfile", &getanyfile);
	git_config_get_ulong("http.maxrequestbuffer", &max_request_buffer);

	for (i = 0; i < ARRAY_SIZE(rpc_service); i++) {
		struct rpc_service *svc = &rpc_service[i];
		strbuf_addf(&var, "http.%s", svc->config_name);
		if (!git_config_get_bool(var.buf, &value))
			svc->enabled = value;
		strbuf_reset(&var);
	}

	strbuf_release(&var);
}

static struct rpc_service *select_service(struct strbuf *hdr, const char *name)
{
	const char *svc_name;
	struct rpc_service *svc = NULL;
	int i;

	if (!skip_prefix(name, "git-", &svc_name))
		forbidden(hdr, "Unsupported service: '%s'", name);

	for (i = 0; i < ARRAY_SIZE(rpc_service); i++) {
		struct rpc_service *s = &rpc_service[i];
		if (!strcmp(s->name, svc_name)) {
			svc = s;
			break;
		}
	}

	if (!svc)
		forbidden(hdr, "Unsupported service: '%s'", name);

	if (svc->enabled < 0) {
		const char *user = getenv("REMOTE_USER");
		svc->enabled = (user && *user) ? 1 : 0;
	}
	if (!svc->enabled)
		forbidden(hdr, "Service not enabled: '%s'", svc->name);
	return svc;
}

static void write_to_child(int out, const unsigned char *buf, ssize_t len, const char *prog_name)
{
	if (write_in_full(out, buf, len) < 0)
		die("unable to write to '%s'", prog_name);
}

/*
 * This is basically strbuf_read(), except that if we
 * hit max_request_buffer we die (we'd rather reject a
 * maliciously large request than chew up infinite memory).
 */
static ssize_t read_request_eof(int fd, unsigned char **out)
{
	size_t len = 0, alloc = 8192;
	unsigned char *buf = xmalloc(alloc);

	if (max_request_buffer < alloc)
		max_request_buffer = alloc;

	while (1) {
		ssize_t cnt;

		cnt = read_in_full(fd, buf + len, alloc - len);
		if (cnt < 0) {
			free(buf);
			return -1;
		}

		/* partial read from read_in_full means we hit EOF */
		len += cnt;
		if (len < alloc) {
			*out = buf;
			return len;
		}

		/* otherwise, grow and try again (if we can) */
		if (alloc == max_request_buffer)
			die("request was larger than our maximum size (%lu);"
			    " try setting GIT_HTTP_MAX_REQUEST_BUFFER",
			    max_request_buffer);

		alloc = alloc_nr(alloc);
		if (alloc > max_request_buffer)
			alloc = max_request_buffer;
		REALLOC_ARRAY(buf, alloc);
	}
}

static ssize_t read_request_fixed_len(int fd, ssize_t req_len, unsigned char **out)
{
	unsigned char *buf = NULL;
	ssize_t cnt = 0;

	if (max_request_buffer < req_len) {
		die("request was larger than our maximum size (%lu): "
		    "%" PRIuMAX "; try setting GIT_HTTP_MAX_REQUEST_BUFFER",
		    max_request_buffer, (uintmax_t)req_len);
	}

	buf = xmalloc(req_len);
	cnt = read_in_full(fd, buf, req_len);
	if (cnt < 0) {
		free(buf);
		return -1;
	}
	*out = buf;
	return cnt;
}

static ssize_t get_content_length(void)
{
	ssize_t val = -1;
	const char *str = getenv("CONTENT_LENGTH");

	if (str && *str && !git_parse_ssize_t(str, &val))
		die("failed to parse CONTENT_LENGTH: %s", str);
	return val;
}

static ssize_t read_request(int fd, unsigned char **out, ssize_t req_len)
{
	if (req_len < 0)
		return read_request_eof(fd, out);
	else
		return read_request_fixed_len(fd, req_len, out);
}

static void inflate_request(const char *prog_name, int out, int buffer_input, ssize_t req_len)
{
	git_zstream stream;
	unsigned char *full_request = NULL;
	unsigned char in_buf[8192];
	unsigned char out_buf[8192];
	unsigned long cnt = 0;
	int req_len_defined = req_len >= 0;
	size_t req_remaining_len = req_len;

	memset(&stream, 0, sizeof(stream));
	git_inflate_init_gzip_only(&stream);

	while (1) {
		ssize_t n;

		if (buffer_input) {
			if (full_request)
				n = 0; /* nothing left to read */
			else
				n = read_request(0, &full_request, req_len);
			stream.next_in = full_request;
		} else {
			ssize_t buffer_len;
			if (req_len_defined && req_remaining_len <= sizeof(in_buf))
				buffer_len = req_remaining_len;
			else
				buffer_len = sizeof(in_buf);
			n = xread(0, in_buf, buffer_len);
			stream.next_in = in_buf;
			if (req_len_defined && n > 0)
				req_remaining_len -= n;
		}

		if (n <= 0)
			die("request ended in the middle of the gzip stream");
		stream.avail_in = n;

		while (0 < stream.avail_in) {
			int ret;

			stream.next_out = out_buf;
			stream.avail_out = sizeof(out_buf);

			ret = git_inflate(&stream, Z_NO_FLUSH);
			if (ret != Z_OK && ret != Z_STREAM_END)
				die("zlib error inflating request, result %d", ret);

			n = stream.total_out - cnt;
			write_to_child(out, out_buf, stream.total_out - cnt, prog_name);
			cnt = stream.total_out;

			if (ret == Z_STREAM_END)
				goto done;
		}
	}

done:
	git_inflate_end(&stream);
	close(out);
	free(full_request);
}

static void copy_request(const char *prog_name, int out, ssize_t req_len)
{
	unsigned char *buf;
	ssize_t n = read_request(0, &buf, req_len);
	if (n < 0)
		die_errno("error reading request body");
	write_to_child(out, buf, n, prog_name);
	close(out);
	free(buf);
}

static void pipe_fixed_length(const char *prog_name, int out, size_t req_len)
{
	unsigned char buf[8192];
	size_t remaining_len = req_len;

	while (remaining_len > 0) {
		size_t chunk_length = remaining_len > sizeof(buf) ? sizeof(buf) : remaining_len;
		ssize_t n = xread(0, buf, chunk_length);
		if (n < 0)
			die_errno("Reading request failed");
		write_to_child(out, buf, n, prog_name);
		remaining_len -= n;
	}

	close(out);
}

static void run_service(const char **argv, int buffer_input)
{
	const char *encoding = getenv("HTTP_CONTENT_ENCODING");
	const char *user = getenv("REMOTE_USER");
	const char *host = getenv("REMOTE_ADDR");
	int gzipped_request = 0;
	struct child_process cld = CHILD_PROCESS_INIT;
	ssize_t req_len = get_content_length();

	if (encoding && (!strcmp(encoding, "gzip") || !strcmp(encoding, "x-gzip")))
		gzipped_request = 1;

	if (!user || !*user)
		user = "anonymous";
	if (!host || !*host)
		host = "(none)";

	if (!getenv("GIT_COMMITTER_NAME"))
		strvec_pushf(&cld.env_array, "GIT_COMMITTER_NAME=%s", user);
	if (!getenv("GIT_COMMITTER_EMAIL"))
		strvec_pushf(&cld.env_array,
			     "GIT_COMMITTER_EMAIL=%s@http.%s", user, host);

	strvec_pushv(&cld.args, argv);
	if (buffer_input || gzipped_request || req_len >= 0)
		cld.in = -1;
	cld.git_cmd = 1;
	cld.clean_on_exit = 1;
	cld.wait_after_clean = 1;
	if (start_command(&cld))
		exit(1);

	close(1);
	if (gzipped_request)
		inflate_request(argv[0], cld.in, buffer_input, req_len);
	else if (buffer_input)
		copy_request(argv[0], cld.in, req_len);
	else if (req_len >= 0)
		pipe_fixed_length(argv[0], cld.in, req_len);
	else
		close(0);

	if (finish_command(&cld))
		exit(1);
}

static int show_text_ref(const char *name, const struct object_id *oid,
			 int flag, void *cb_data)
{
	const char *name_nons = strip_namespace(name);
	struct strbuf *buf = cb_data;
	struct object *o = parse_object(the_repository, oid);
	if (!o)
		return 0;

	strbuf_addf(buf, "%s\t%s\n", oid_to_hex(oid), name_nons);
	if (o->type == OBJ_TAG) {
		o = deref_tag(the_repository, o, name, 0);
		if (!o)
			return 0;
		strbuf_addf(buf, "%s\t%s^{}\n", oid_to_hex(&o->oid),
			    name_nons);
	}
	return 0;
}

static void get_info_refs(struct strbuf *hdr, char *arg)
{
	const char *service_name = get_parameter("service");
	struct strbuf buf = STRBUF_INIT;

	hdr_nocache(hdr);

	if (service_name) {
		const char *argv[] = {NULL /* service name */,
			"--http-backend-info-refs",
			".", NULL};
		struct rpc_service *svc = select_service(hdr, service_name);

		strbuf_addf(&buf, "application/x-git-%s-advertisement",
			svc->name);
		hdr_str(hdr, content_type, buf.buf);
		end_headers(hdr);


		if (determine_protocol_version_server() != protocol_v2) {
			packet_write_fmt(1, "# service=git-%s\n", svc->name);
			packet_flush(1);
		}

		argv[0] = svc->name;
		run_service(argv, 0);

	} else {
		select_getanyfile(hdr);
		for_each_namespaced_ref(show_text_ref, &buf);
		send_strbuf(hdr, "text/plain", &buf);
	}
	strbuf_release(&buf);
}

static int show_head_ref(const char *refname, const struct object_id *oid,
			 int flag, void *cb_data)
{
	struct strbuf *buf = cb_data;

	if (flag & REF_ISSYMREF) {
		const char *target = resolve_ref_unsafe(refname,
							RESOLVE_REF_READING,
							NULL, NULL);

		if (target)
			strbuf_addf(buf, "ref: %s\n", strip_namespace(target));
	} else {
		strbuf_addf(buf, "%s\n", oid_to_hex(oid));
	}

	return 0;
}

static void get_head(struct strbuf *hdr, char *arg)
{
	struct strbuf buf = STRBUF_INIT;

	select_getanyfile(hdr);
	head_ref_namespaced(show_head_ref, &buf);
	send_strbuf(hdr, "text/plain", &buf);
	strbuf_release(&buf);
}

static void get_info_packs(struct strbuf *hdr, char *arg)
{
	size_t objdirlen = strlen(get_object_directory());
	struct strbuf buf = STRBUF_INIT;
	struct packed_git *p;
	size_t cnt = 0;

	select_getanyfile(hdr);
	for (p = get_all_packs(the_repository); p; p = p->next) {
		if (p->pack_local)
			cnt++;
	}

	strbuf_grow(&buf, cnt * 53 + 2);
	for (p = get_all_packs(the_repository); p; p = p->next) {
		if (p->pack_local)
			strbuf_addf(&buf, "P %s\n", p->pack_name + objdirlen + 6);
	}
	strbuf_addch(&buf, '\n');

	hdr_nocache(hdr);
	send_strbuf(hdr, "text/plain; charset=utf-8", &buf);
	strbuf_release(&buf);
}

static void check_content_type(struct strbuf *hdr, const char *accepted_type)
{
	const char *actual_type = getenv("CONTENT_TYPE");

	if (!actual_type)
		actual_type = "";

	if (strcmp(actual_type, accepted_type)) {
		http_status(hdr, 415, "Unsupported Media Type");
		hdr_nocache(hdr);
		end_headers(hdr);
		format_write(1,
			"Expected POST with Content-Type '%s',"
			" but received '%s' instead.\n",
			accepted_type, actual_type);
		exit(0);
	}
}

static void service_rpc(struct strbuf *hdr, char *service_name)
{
	const char *argv[] = {NULL, "--stateless-rpc", ".", NULL};
	struct rpc_service *svc = select_service(hdr, service_name);
	struct strbuf buf = STRBUF_INIT;

	strbuf_reset(&buf);
	strbuf_addf(&buf, "application/x-git-%s-request", svc->name);
	check_content_type(hdr, buf.buf);

	hdr_nocache(hdr);

	strbuf_reset(&buf);
	strbuf_addf(&buf, "application/x-git-%s-result", svc->name);
	hdr_str(hdr, content_type, buf.buf);

	end_headers(hdr);

	argv[0] = svc->name;
	run_service(argv, svc->buffer_input);
	strbuf_release(&buf);
}

static int dead;
static NORETURN void die_webcgi(const char *err, va_list params)
{
	if (dead <= 1) {
		struct strbuf hdr = STRBUF_INIT;
		report_fn die_message_fn = get_die_message_routine();

		die_message_fn(err, params);

		http_status(&hdr, 500, "Internal Server Error");
		hdr_nocache(&hdr);
		end_headers(&hdr);
	}
	exit(0); /* we successfully reported a failure ;-) */
}

static int die_webcgi_recursing(void)
{
	return dead++ > 1;
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
	void (*imp)(struct strbuf *, char *);
} services[] = {
	{"GET", "/HEAD$", get_head},
	{"GET", "/info/refs$", get_info_refs},
	{"GET", "/objects/info/alternates$", get_text_file},
	{"GET", "/objects/info/http-alternates$", get_text_file},
	{"GET", "/objects/info/packs$", get_info_packs},
	{"GET", "/objects/[0-9a-f]{2}/[0-9a-f]{38}$", get_loose_object},
	{"GET", "/objects/[0-9a-f]{2}/[0-9a-f]{62}$", get_loose_object},
	{"GET", "/objects/pack/pack-[0-9a-f]{40}\\.pack$", get_pack_file},
	{"GET", "/objects/pack/pack-[0-9a-f]{64}\\.pack$", get_pack_file},
	{"GET", "/objects/pack/pack-[0-9a-f]{40}\\.idx$", get_idx_file},
	{"GET", "/objects/pack/pack-[0-9a-f]{64}\\.idx$", get_idx_file},

	{"POST", "/git-upload-pack$", service_rpc},
	{"POST", "/git-receive-pack$", service_rpc}
};

static int bad_request(struct strbuf *hdr, const struct service_cmd *c)
{
	const char *proto = getenv("SERVER_PROTOCOL");

	if (proto && !strcmp(proto, "HTTP/1.1")) {
		http_status(hdr, 405, "Method Not Allowed");
		hdr_str(hdr, "Allow",
			!strcmp(c->method, "GET") ? "GET, HEAD" : c->method);
	} else
		http_status(hdr, 400, "Bad Request");
	hdr_nocache(hdr);
	end_headers(hdr);
	return 0;
}

int cmd_main(int argc, const char **argv)
{
	char *method = getenv("REQUEST_METHOD");
	const char *proto_header;
	char *dir;
	struct service_cmd *cmd = NULL;
	char *cmd_arg = NULL;
	int i;
	struct strbuf hdr = STRBUF_INIT;

	set_die_routine(die_webcgi);
	set_die_is_recursing_routine(die_webcgi_recursing);

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

			if (strcmp(method, c->method))
				return bad_request(&hdr, c);

			cmd = c;
			n = out[0].rm_eo - out[0].rm_so;
			cmd_arg = xmemdupz(dir + out[0].rm_so + 1, n - 1);
			dir[out[0].rm_so] = 0;
			break;
		}
		regfree(&re);
	}

	if (!cmd)
		not_found(&hdr, "Request not supported: '%s'", dir);

	setup_path();
	if (!enter_repo(dir, 0))
		not_found(&hdr, "Not a git repository: '%s'", dir);
	git_config(git_default_config, NULL);
	if (!getenv("GIT_HTTP_EXPORT_ALL") &&
	    access("git-daemon-export-ok", F_OK) )
		not_found(&hdr, "Repository not exported: '%s'", dir);

	http_config();
	max_request_buffer = git_env_ulong("GIT_HTTP_MAX_REQUEST_BUFFER",
					   max_request_buffer);
	proto_header = getenv("HTTP_GIT_PROTOCOL");
	if (proto_header)
		setenv(GIT_PROTOCOL_ENVIRONMENT, proto_header, 0);

	cmd->imp(&hdr, cmd_arg);
	return 0;
}
