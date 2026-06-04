#include "unit-test.h"
#include "utf8.h"
#include "strbuf.h"

/*
 * Test utf8_strnwidth with various Chinese strings
 * Chinese characters typically have a width of 2 columns when displayed
 */
void test_utf8_width__strnwidth_chinese(void)
{
	const char *str;

	/* Test basic ASCII - each character should have width 1 */
	cl_assert_equal_i(5, utf8_strnwidth("Hello", 5, 0));
	/* skip_ansi = 1 */
	cl_assert_equal_i(5, utf8_strnwidth("Hello", 5, 1));

	/* Test simple Chinese characters - each should have width 2 */
	/* "你好" is 6 bytes (3 bytes per char in UTF-8), 4 display columns */
	cl_assert_equal_i(4, utf8_strnwidth("你好", 6, 0));

	/* Test mixed ASCII and Chinese - ASCII = 1 column, Chinese = 2 columns */
	/* "h"(1) + "i"(1) + "你"(2) + "好"(2) = 6 */
	cl_assert_equal_i(6, utf8_strnwidth("Hi你好", 8, 0));

	/* Test longer Chinese string */
	/* 5 Chinese chars = 10 display columns */
	cl_assert_equal_i(10, utf8_strnwidth("你好世界！", 15, 0));

	/* Test individual Chinese character width */
	cl_assert_equal_i(2, utf8_strnwidth("中", 3, 0));

	/* Test empty string */
	cl_assert_equal_i(0, utf8_strnwidth("", 0, 0));

	/* Test length limiting */
	str = "你好世界";
	/* Only first char "你"(2 columns) within 3 bytes */
	cl_assert_equal_i(2, utf8_strnwidth(str, 3, 0));
	/* First two chars "你好"(4 columns) in 6 bytes */
	cl_assert_equal_i(4, utf8_strnwidth(str, 6, 0));
}

/*
 * Tests for utf8_strwidth (simpler version without length limit)
 */
void test_utf8_width__strwidth_chinese(void)
{
	/* Test basic ASCII */
	cl_assert_equal_i(5, utf8_strwidth("Hello"));

	/* Test Chinese characters */
	/* 2 Chinese chars = 4 display columns */
	cl_assert_equal_i(4, utf8_strwidth("你好"));

	/* Test longer Chinese string */
	/* 5 Chinese chars = 10 display columns */
	cl_assert_equal_i(10, utf8_strwidth("你好世界！"));

	/* Test mixed ASCII and Chinese */
	/* 5 ASCII (5 cols) + 2 Chinese (4 cols) = 9 */
	cl_assert_equal_i(9, utf8_strwidth("Hello世界"));
	/* 2 ASCII (2 cols) + 2 Chinese (4 cols) + 1 ASCII (1 col) = 7 */
	cl_assert_equal_i(7, utf8_strwidth("Hi世界!"));
}

/*
 * Additional tests with other East Asian characters
 */
void test_utf8_width__strnwidth_japanese_korean(void)
{
	/* Japanese characters (should also be 2 columns each) */
	/* 5 Japanese chars x 2 cols each = 10 display columns */
	cl_assert_equal_i(10, utf8_strnwidth("こんにちは", 15, 0));

	/* Korean characters (should also be 2 columns each) */
	/* 5 Korean chars x 2 cols each = 10 display columns */
	cl_assert_equal_i(10, utf8_strnwidth("안녕하세요", 15, 0));
}

/*
 * Test utf8_strnwidth with CJK strings and ANSI sequences
 */
void test_utf8_width__strnwidth_cjk_with_ansi(void)
{
	/* Test CJK with ANSI sequences */
	const char *ansi_test = "\033[1m你好\033[0m";
	int width = utf8_strnwidth(ansi_test, strlen(ansi_test), 1);
	/* Should skip ANSI sequences and count "你好" as 4 columns */
	cl_assert_equal_i(4, width);

	/* Test mixed ASCII, CJK, and ANSI */
	ansi_test = "Hello\033[32m世界\033[0m!";
	width = utf8_strnwidth(ansi_test, strlen(ansi_test), 1);
	/* "Hello"(5) + "世界"(4) + "!"(1) = 10 */
	cl_assert_equal_i(10, width);
}

/*
 * Test the strbuf_utf8_align function with CJK characters
 */
void test_utf8_width__strbuf_utf8_align(void)
{
	struct strbuf buf = STRBUF_INIT;

	/* Test left alignment with CJK */
	strbuf_utf8_align(&buf, ALIGN_LEFT, 10, "你好");
	/* Since "你好" is 4 display columns, we need 6 more spaces to reach 10 */
	cl_assert_equal_s("你好      ", buf.buf);
	strbuf_reset(&buf);

	/* Test right alignment with CJK */
	strbuf_utf8_align(&buf, ALIGN_RIGHT, 8, "世界");
	/* "世界" is 4 display columns, so we need 4 leading spaces */
	cl_assert_equal_s("    世界", buf.buf);
	strbuf_reset(&buf);

	/* Test center alignment with CJK */
	strbuf_utf8_align(&buf, ALIGN_MIDDLE, 10, "中");
	/* "中" is 2 display columns, so (10-2)/2 = 4 spaces on left, 4 on right */
	cl_assert_equal_s("    中    ", buf.buf);
	strbuf_reset(&buf);

	strbuf_utf8_align(&buf, ALIGN_MIDDLE, 5, "中");
	/* "中" is 2 display columns, so (5-2)/2 = 1 spaces on left, 2 on right */
	cl_assert_equal_s(" 中  ", buf.buf);
	strbuf_reset(&buf);

	/* Test alignment that is smaller than string width */
	strbuf_utf8_align(&buf, ALIGN_LEFT, 2, "你好");
	/* Since "你好" is 4 display columns, it should not be truncated */
	cl_assert_equal_s("你好", buf.buf);
	strbuf_release(&buf);
}
