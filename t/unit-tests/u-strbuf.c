#include "unit-test.h"
#include "strbuf.h"

/* wrapper that supplies tests with an empty, initialized strbuf */
static void setup(void (*f)(struct strbuf*, const void*),
		  const void *data)
{
	struct strbuf buf = STRBUF_INIT;

	f(&buf, data);
	strbuf_release(&buf);
	cl_assert_equal_i(buf.len, 0);
	cl_assert_equal_i(buf.alloc, 0);
}

/* wrapper that supplies tests with a populated, initialized strbuf */
static void setup_populated(void (*f)(struct strbuf*, const void*),
			    const char *init_str, const void *data)
{
	struct strbuf buf = STRBUF_INIT;

	strbuf_addstr(&buf, init_str);
	cl_assert_equal_i(buf.len, strlen(init_str));
	f(&buf, data);
	strbuf_release(&buf);
	cl_assert_equal_i(buf.len, 0);
	cl_assert_equal_i(buf.alloc, 0);
}

static void assert_sane_strbuf(struct strbuf *buf)
{
	/* Initialized strbufs should always have a non-NULL buffer */
	cl_assert(buf->buf != NULL);
	/* Buffers should always be NUL-terminated */
	cl_assert(buf->buf[buf->len] == '\0');
	/*
         * In case the buffer contains anything, `alloc` must alloc must
         * be at least one byte larger than `len`.
         */
	if (buf->len)
            cl_assert(buf->len < buf->alloc);
}

void test_strbuf__static_init(void)
{
	struct strbuf buf = STRBUF_INIT;

	cl_assert_equal_i(buf.len, 0);
	cl_assert_equal_i(buf.alloc, 0);
	cl_assert(buf.buf[0] == '\0');
}

void test_strbuf__dynamic_init(void)
{
	struct strbuf buf;

	strbuf_init(&buf, 1024);
	assert_sane_strbuf(&buf);
	cl_assert_equal_i(buf.len, 0);
	cl_assert(buf.alloc >= 1024);
	cl_assert(buf.buf[0] == '\0');
	strbuf_release(&buf);
}

static void t_addch(struct strbuf *buf, const void *data)
{
	const char *p_ch = data;
	const char ch = *p_ch;
	size_t orig_alloc = buf->alloc;
	size_t orig_len = buf->len;

	assert_sane_strbuf(buf);
	strbuf_addch(buf, ch);
	assert_sane_strbuf(buf);
	cl_assert_equal_i(buf->len, orig_len + 1);
	cl_assert(buf->alloc >= orig_alloc);
	cl_assert(buf->buf[buf->len] == '\0');
}

static void t_addstr(struct strbuf *buf, const void *data)
{
	const char *text = data;
	size_t len = strlen(text);
	size_t orig_alloc = buf->alloc;
	size_t orig_len = buf->len;

	assert_sane_strbuf(buf);
	strbuf_addstr(buf, text);
	assert_sane_strbuf(buf);
	cl_assert_equal_i(buf->len, orig_len + len);
	cl_assert(buf->alloc >= orig_alloc);
	cl_assert(buf->buf[buf->len] == '\0');
	cl_assert_equal_s(buf->buf + orig_len, text);
}

void test_strbuf__add_single_char(void)
{
	setup(t_addch, "a");
}

void test_strbuf__add_empty_char(void)
{
	setup(t_addch, "");
}

void test_strbuf__add_append_char(void)
{
	setup_populated(t_addch, "initial value", "a");
}

void test_strbuf__add_single_str(void)
{
	setup(t_addstr, "hello there");
}

void test_strbuf__add_append_str(void)
{
	setup_populated(t_addstr, "initial value", "hello there");
}
