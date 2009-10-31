#include "cache.h"
#include "refs.h"
#include "pkt-line.h"
#include "object.h"
#include "tag.h"
#include "exec_cmd.h"

static const char content_type[] = "Content-Type";
static const char content_length[] = "Content-Length";
static const char last_modified[] = "Last-Modified";

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

static void hdr_int(const char *name, size_t value)
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

static void send_strbuf(const char *type, struct strbuf *buf)
{
	hdr_int(content_length, buf->len);
	hdr_str(content_type, type);
	end_headers();
	safe_write(1, buf->buf, buf->len);
}

static void send_file(const char *the_type, const char *name)
{
	const char *p = git_path("%s", name);
	size_t buf_alloc = 8192;
	char *buf = xmalloc(buf_alloc);
	int fd;
	struct stat sb;
	size_t size;

	fd = open(p, O_RDONLY);
	if (fd < 0)
		not_found("Cannot open '%s': %s", p, strerror(errno));
	if (fstat(fd, &sb) < 0)
		die_errno("Cannot stat '%s'", p);

	size = xsize_t(sb.st_size);

	hdr_int(content_length, size);
	hdr_str(content_type, the_type);
	hdr_date(last_modified, sb.st_mtime);
	end_headers();

	while (size) {
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
	hdr_nocache();
	send_file("text/plain", name);
}

static void get_loose_object(char *name)
{
	hdr_cache_forever();
	send_file("application/x-git-loose-object", name);
}

static void get_pack_file(char *name)
{
	hdr_cache_forever();
	send_file("application/x-git-packed-objects", name);
}

static void get_idx_file(char *name)
{
	hdr_cache_forever();
	send_file("application/x-git-packed-objects-toc", name);
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
	struct strbuf buf = STRBUF_INIT;

	for_each_ref(show_text_ref, &buf);
	hdr_nocache();
	send_strbuf("text/plain", &buf);
	strbuf_release(&buf);
}

static void get_info_packs(char *arg)
{
	size_t objdirlen = strlen(get_object_directory());
	struct strbuf buf = STRBUF_INIT;
	struct packed_git *p;
	size_t cnt = 0;

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

static NORETURN void die_webcgi(const char *err, va_list params)
{
	char buffer[1000];

	http_status(500, "Internal Server Error");
	hdr_nocache();
	end_headers();

	vsnprintf(buffer, sizeof(buffer), err, params);
	fprintf(stderr, "fatal: %s\n", buffer);
	exit(0);
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
	{"GET", "/objects/pack/pack-[0-9a-f]{40}\\.idx$", get_idx_file}
};

int main(int argc, char **argv)
{
	char *method = getenv("REQUEST_METHOD");
	char *dir = getenv("PATH_TRANSLATED");
	struct service_cmd *cmd = NULL;
	char *cmd_arg = NULL;
	int i;

	git_extract_argv0_path(argv[0]);
	set_die_routine(die_webcgi);

	if (!method)
		die("No REQUEST_METHOD from server");
	if (!strcmp(method, "HEAD"))
		method = "GET";
	if (!dir)
		die("No PATH_TRANSLATED from server");

	for (i = 0; i < ARRAY_SIZE(services); i++) {
		struct service_cmd *c = &services[i];
		regex_t re;
		regmatch_t out[1];

		if (regcomp(&re, c->pattern, REG_EXTENDED))
			die("Bogus regex in service table: %s", c->pattern);
		if (!regexec(&re, dir, 1, out, 0)) {
			size_t n = out[0].rm_eo - out[0].rm_so;

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
			cmd_arg = xmalloc(n);
			strncpy(cmd_arg, dir + out[0].rm_so + 1, n);
			cmd_arg[n] = '\0';
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

	cmd->imp(cmd_arg);
	return 0;
}
