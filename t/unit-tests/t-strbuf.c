#include "test-lib.h"
#include "strbuf.h"

/* wrapper that supplies tests with an empty, initialized strbuf */
static void setup(void (*f)(struct strbuf*, const void*),
		  const void *data)
{
	struct strbuf buf = STRBUF_INIT;

	f(&buf, data);
	strbuf_release(&buf);
	check_uint(buf.len, ==, 0);
	check_uint(buf.alloc, ==, 0);
}

/* wrapper that supplies tests with a populated, initialized strbuf */
static void setup_populated(void (*f)(struct strbuf*, const void*),
			    const char *init_str, const void *data)
{
	struct strbuf buf = STRBUF_INIT;

	strbuf_addstr(&buf, init_str);
	check_uint(buf.len, ==, strlen(init_str));
	f(&buf, data);
	strbuf_release(&buf);
	check_uint(buf.len, ==, 0);
	check_uint(buf.alloc, ==, 0);
}

static int assert_sane_strbuf(struct strbuf *buf)
{
	/* Initialized strbufs should always have a non-NULL buffer */
	if (!check(!!buf->buf))
		return 0;
	/* Buffers should always be NUL-terminated */
	if (!check_char(buf->buf[buf->len], ==, '\0'))
		return 0;
	/*
	 * Freshly-initialized strbufs may not have a dynamically allocated
	 * buffer
	 */
	if (buf->len == 0 && buf->alloc == 0)
		return 1;
	/* alloc must be at least one byte larger than len */
	return check_uint(buf->len, <, buf->alloc);
}

static void t_static_init(void)
{
	struct strbuf buf = STRBUF_INIT;

	check_uint(buf.len, ==, 0);
	check_uint(buf.alloc, ==, 0);
	check_char(buf.buf[0], ==, '\0');
}

static void t_dynamic_init(void)
{
	struct strbuf buf;

	strbuf_init(&buf, 1024);
	check(assert_sane_strbuf(&buf));
	check_uint(buf.len, ==, 0);
	check_uint(buf.alloc, >=, 1024);
	check_char(buf.buf[0], ==, '\0');
	strbuf_release(&buf);
}

static void t_addch(struct strbuf *buf, const void *data)
{
	const char *p_ch = data;
	const char ch = *p_ch;
	size_t orig_alloc = buf->alloc;
	size_t orig_len = buf->len;

	if (!check(assert_sane_strbuf(buf)))
		return;
	strbuf_addch(buf, ch);
	if (!check(assert_sane_strbuf(buf)))
		return;
	if (!(check_uint(buf->len, ==, orig_len + 1) &&
	      check_uint(buf->alloc, >=, orig_alloc)))
		return; /* avoid de-referencing buf->buf */
	check_char(buf->buf[buf->len - 1], ==, ch);
	check_char(buf->buf[buf->len], ==, '\0');
}

static void t_addstr(struct strbuf *buf, const void *data)
{
	const char *text = data;
	size_t len = strlen(text);
	size_t orig_alloc = buf->alloc;
	size_t orig_len = buf->len;

	if (!check(assert_sane_strbuf(buf)))
		return;
	strbuf_addstr(buf, text);
	if (!check(assert_sane_strbuf(buf)))
		return;
	if (!(check_uint(buf->len, ==, orig_len + len) &&
	      check_uint(buf->alloc, >=, orig_alloc) &&
	      check_uint(buf->alloc, >, orig_len + len) &&
	      check_char(buf->buf[orig_len + len], ==, '\0')))
	    return;
	check_str(buf->buf + orig_len, text);
}

int cmd_main(int argc, const char **argv)
{
	if (!TEST(t_static_init(), "static initialization works"))
		test_skip_all("STRBUF_INIT is broken");
	TEST(t_dynamic_init(), "dynamic initialization works");
	TEST(setup(t_addch, "a"), "strbuf_addch adds char");
	TEST(setup(t_addch, ""), "strbuf_addch adds NUL char");
	TEST(setup_populated(t_addch, "initial value", "a"),
	     "strbuf_addch appends to initial value");
	TEST(setup(t_addstr, "hello there"), "strbuf_addstr adds string");
	TEST(setup_populated(t_addstr, "initial value", "hello there"),
	     "strbuf_addstr appends string to initial value");

	return test_done();
}
