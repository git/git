#include "unit-test.h"
#include "utf8.h"
#include "strbuf.h"

/*
 * Test utf8_strnwidth with various Chinese strings
 * Chinese characters typically have a width of 2 columns when displayed
 */
void test_utf8_width__strnwidth_chinese(void)
{
	const char *ansi_test;
	const char *str;

	/* Test basic ASCII - each character should have width 1 */
	cl_assert_equal_i(5, utf8_strnwidth("hello", 5, 0));
	cl_assert_equal_i(5, utf8_strnwidth("hello", 5, 1));  /* skip_ansi = 1 */

	/* Test simple Chinese characters - each should have width 2 */
	cl_assert_equal_i(4, utf8_strnwidth("你好", 6, 0));  /* "你好" is 6 bytes (3 bytes per char in UTF-8), 4 display columns */

	/* Test mixed ASCII and Chinese - ASCII = 1 column, Chinese = 2 columns */
	cl_assert_equal_i(6, utf8_strnwidth("hi你好", 8, 0));  /* "h"(1) + "i"(1) + "你"(2) + "好"(2) = 6 */

	/* Test longer Chinese string */
	cl_assert_equal_i(10, utf8_strnwidth("你好世界！", 15, 0));  /* 5 Chinese chars = 10 display columns */

	/* Test with skip_ansi = 1 to make sure it works with escape sequences */
	ansi_test = "\033[31m你好\033[0m";
	cl_assert_equal_i(4, utf8_strnwidth(ansi_test, strlen(ansi_test), 1));  /* Skip escape sequences, just count "你好" which should be 4 columns */

	/* Test individual Chinese character width */
	cl_assert_equal_i(2, utf8_strnwidth("中", 3, 0));  /* Single Chinese char should be 2 columns */

	/* Test empty string */
	cl_assert_equal_i(0, utf8_strnwidth("", 0, 0));

	/* Test length limiting */
	str = "你好世界";
	cl_assert_equal_i(2, utf8_strnwidth(str, 3, 0));  /* Only first char "你"(2 columns) within 3 bytes */
	cl_assert_equal_i(4, utf8_strnwidth(str, 6, 0));  /* First two chars "你好"(4 columns) in 6 bytes */
}

/*
 * Tests for utf8_strwidth (simpler version without length limit)
 */
void test_utf8_width__strwidth_chinese(void)
{
	/* Test basic ASCII */
	cl_assert_equal_i(5, utf8_strwidth("hello"));

	/* Test Chinese characters */
	cl_assert_equal_i(4, utf8_strwidth("你好"));  /* 2 Chinese chars = 4 display columns */

	/* Test mixed ASCII and Chinese */
	cl_assert_equal_i(9, utf8_strwidth("hello世界"));  /* 5 ASCII (5 cols) + 2 Chinese (4 cols) = 9 */
	cl_assert_equal_i(7, utf8_strwidth("hi世界!"));   /* 2 ASCII (2 cols) + 2 Chinese (4 cols) + 1 ASCII (1 col) = 7 */
}

/*
 * Additional tests with other East Asian characters
 */
void test_utf8_width__strnwidth_japanese_korean(void)
{
	/* Japanese characters (should also be 2 columns each) */
	cl_assert_equal_i(10, utf8_strnwidth("こんにちは", 15, 0));  /* 5 Japanese chars @ 2 cols each = 10 display columns */

	/* Korean characters (should also be 2 columns each) */
	cl_assert_equal_i(10, utf8_strnwidth("안녕하세요", 15, 0));  /* 5 Korean chars @ 2 cols each = 10 display columns */
}

/*
 * Test edge cases with partial UTF-8 sequences
 */
void test_utf8_width__strnwidth_edge_cases(void)
{
	const char *invalid;
	unsigned char truncated_bytes[] = {0xe4, 0xbd, 0x00};  /* First 2 bytes of "中" + null */

	/* Test invalid UTF-8 - should fall back to byte count */
	invalid = "\xff\xfe";  /* Invalid UTF-8 sequence */
	cl_assert_equal_i(2, utf8_strnwidth(invalid, 2, 0));  /* Should return length if invalid UTF-8 */

	/* Test partial UTF-8 character (truncated) */
	cl_assert_equal_i(2, utf8_strnwidth((const char*)truncated_bytes, 2, 0));  /* Invalid UTF-8, returns byte count */
}
