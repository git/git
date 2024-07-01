#include "test-lib.h"
#include "strbuf.h"

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

static void t_addch(struct strbuf *buf, int ch)
{
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

static void t_addstr(struct strbuf *buf, const char *text)
{
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

static void t_release(struct strbuf *sb)
{
	strbuf_release(sb);
	check_uint(sb->len, ==, 0);
	check_uint(sb->alloc, ==, 0);
}

int cmd_main(int argc, const char **argv)
{
	if (!TEST(t_static_init(), "static initialization works"))
		test_skip_all("STRBUF_INIT is broken");
	TEST(t_dynamic_init(), "dynamic initialization works");

	if (TEST_RUN("strbuf_addch adds char")) {
		struct strbuf sb = STRBUF_INIT;
		t_addch(&sb, 'a');
		t_release(&sb);
	}

	if (TEST_RUN("strbuf_addch adds NUL char")) {
		struct strbuf sb = STRBUF_INIT;
		t_addch(&sb, '\0');
		t_release(&sb);
	}

	if (TEST_RUN("strbuf_addch appends to initial value")) {
		struct strbuf sb = STRBUF_INIT;
		t_addstr(&sb, "initial value");
		t_addch(&sb, 'a');
		t_release(&sb);
	}

	if (TEST_RUN("strbuf_addstr adds string")) {
		struct strbuf sb = STRBUF_INIT;
		t_addstr(&sb, "hello there");
		t_release(&sb);
	}

	if (TEST_RUN("strbuf_addstr appends string to initial value")) {
		struct strbuf sb = STRBUF_INIT;
		t_addstr(&sb, "initial value");
		t_addstr(&sb, "hello there");
		t_release(&sb);
	}

	return test_done();
}
