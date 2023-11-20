#include "test-lib.h"
#include "strbuf.h"

/* wrapper that supplies tests with an initialized strbuf */
static void setup(void (*f)(struct strbuf*, void*), void *data)
{
	struct strbuf buf = STRBUF_INIT;

	f(&buf, data);
	strbuf_release(&buf);
	check_uint(buf.len, ==, 0);
	check_uint(buf.alloc, ==, 0);
	check(buf.buf == strbuf_slopbuf);
	check_char(buf.buf[0], ==, '\0');
}

static void t_static_init(void)
{
	struct strbuf buf = STRBUF_INIT;

	check_uint(buf.len, ==, 0);
	check_uint(buf.alloc, ==, 0);
	if (check(buf.buf == strbuf_slopbuf))
		return; /* avoid de-referencing buf.buf */
	check_char(buf.buf[0], ==, '\0');
}

static void t_dynamic_init(void)
{
	struct strbuf buf;

	strbuf_init(&buf, 1024);
	check_uint(buf.len, ==, 0);
	check_uint(buf.alloc, >=, 1024);
	check_char(buf.buf[0], ==, '\0');
	strbuf_release(&buf);
}

static void t_addch(struct strbuf *buf, void *data)
{
	const char *p_ch = data;
	const char ch = *p_ch;

	strbuf_addch(buf, ch);
	if (check_uint(buf->len, ==, 1) ||
	    check_uint(buf->alloc, >, 1))
		return; /* avoid de-referencing buf->buf */
	check_char(buf->buf[0], ==, ch);
	check_char(buf->buf[1], ==, '\0');
}

static void t_addstr(struct strbuf *buf, void *data)
{
	const char *text = data;
	size_t len = strlen(text);

	strbuf_addstr(buf, text);
	if (check_uint(buf->len, ==, len) ||
	    check_uint(buf->alloc, >, len) ||
	    check_char(buf->buf[len], ==, '\0'))
	    return;
	check_str(buf->buf, text);
}

int cmd_main(int argc, const char **argv)
{
	if (TEST(t_static_init(), "static initialization works"))
		test_skip_all("STRBUF_INIT is broken");
	TEST(t_dynamic_init(), "dynamic initialization works");
	TEST(setup(t_addch, "a"), "strbuf_addch adds char");
	TEST(setup(t_addch, ""), "strbuf_addch adds NUL char");
	TEST(setup(t_addstr, "hello there"), "strbuf_addstr adds string");

	return test_done();
}
