#include "unit-test.h"
#include "utf8.h"

/*
 * Test display_mode_esc_sequence_len_counted() for NUL-terminated strings.
 */
void test_utf8_ansi__sgr_len_counted(void)
{
	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted("", SIZE_MAX));

	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted("\033", SIZE_MAX));
	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted("\033[", SIZE_MAX));
	cl_assert_equal_u(3, display_mode_esc_sequence_len_counted("\033[m", SIZE_MAX));
	cl_assert_equal_u(3, display_mode_esc_sequence_len_counted("\033[ma", SIZE_MAX));

	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted("\033[3", SIZE_MAX));
	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted("\033[31", SIZE_MAX));
	cl_assert_equal_u(5, display_mode_esc_sequence_len_counted("\033[31m", SIZE_MAX));
	cl_assert_equal_u(5, display_mode_esc_sequence_len_counted("\033[31ma", SIZE_MAX));

	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted("\033[31;", SIZE_MAX));
	cl_assert_equal_u(6, display_mode_esc_sequence_len_counted("\033[31;m", SIZE_MAX));
	cl_assert_equal_u(6, display_mode_esc_sequence_len_counted("\033[31;ma", SIZE_MAX));

	cl_assert_equal_u(7, display_mode_esc_sequence_len_counted("\033[31;0m", SIZE_MAX));
	cl_assert_equal_u(7, display_mode_esc_sequence_len_counted("\033[31;0ma", SIZE_MAX));
}

/*
 * Same test as above for the wrapper display_mode_esc_sequence_len().
 */
void test_utf8_ansi__sgr_len(void)
{
	cl_assert_equal_u(0, display_mode_esc_sequence_len(""));

	cl_assert_equal_u(0, display_mode_esc_sequence_len("\033"));
	cl_assert_equal_u(0, display_mode_esc_sequence_len("\033["));
	cl_assert_equal_u(3, display_mode_esc_sequence_len("\033[m"));
	cl_assert_equal_u(3, display_mode_esc_sequence_len("\033[ma"));

	cl_assert_equal_u(0, display_mode_esc_sequence_len("\033[3"));
	cl_assert_equal_u(0, display_mode_esc_sequence_len("\033[31"));
	cl_assert_equal_u(5, display_mode_esc_sequence_len("\033[31m"));
	cl_assert_equal_u(5, display_mode_esc_sequence_len("\033[31ma"));

	cl_assert_equal_u(0, display_mode_esc_sequence_len("\033[31;"));
	cl_assert_equal_u(6, display_mode_esc_sequence_len("\033[31;m"));
	cl_assert_equal_u(6, display_mode_esc_sequence_len("\033[31;ma"));

	cl_assert_equal_u(7, display_mode_esc_sequence_len("\033[31;0m"));
	cl_assert_equal_u(7, display_mode_esc_sequence_len("\033[31;0ma"));
}

/*
 * Test display_mode_esc_sequence_len_counted() with length limits.
 */
void test_utf8_ansi__sgr_len_limited(void)
{
	/* sequences followed by one non-sequence character */
	char sgr_1[] = "\033[ma";
	char sgr_2[] = "\033[31ma";
	char sgr_3[] = "\033[31;ma";
	char sgr_4[] = "\033[31;0ma";

	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted(sgr_1, 0));
	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted(sgr_1, 1));
	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted(sgr_1, 2));
	cl_assert_equal_u(3, display_mode_esc_sequence_len_counted(sgr_1, 3));
	cl_assert_equal_u(3, display_mode_esc_sequence_len_counted(sgr_1, 4));

	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted(sgr_2, 3));
	cl_assert_equal_u(0, display_mode_esc_sequence_len_counted(sgr_2, 4));
	cl_assert_equal_u(5, display_mode_esc_sequence_len_counted(sgr_2, 5));
	cl_assert_equal_u(5, display_mode_esc_sequence_len_counted(sgr_2, 6));

	cl_assert_equal_u(6, display_mode_esc_sequence_len_counted(sgr_3,
								   sizeof(sgr_3) - 2));
	cl_assert_equal_u(6, display_mode_esc_sequence_len_counted(sgr_3,
								   sizeof(sgr_3) - 1));

	cl_assert_equal_u(7, display_mode_esc_sequence_len_counted(sgr_4,
								   sizeof(sgr_4) - 2));
	cl_assert_equal_u(7, display_mode_esc_sequence_len_counted(sgr_4,
								   sizeof(sgr_4) - 1));
}
