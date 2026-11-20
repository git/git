#include "unit-test.h"
#include "utf8.h"
#include "strbuf.h"

/*
 * Test display_mode_esc_sequence_len() for NUL terminated strings.
 */
void test_utf8_ansi__sgr_len_null(void)
{
	cl_assert_equal_i(0, display_mode_esc_sequence_len("", NULL));

	cl_assert_equal_i(0, display_mode_esc_sequence_len("\033", NULL));
	cl_assert_equal_i(0, display_mode_esc_sequence_len("\033[", NULL));
	cl_assert_equal_i(3, display_mode_esc_sequence_len("\033[m", NULL));

	cl_assert_equal_i(0, display_mode_esc_sequence_len("\033[3", NULL));
	cl_assert_equal_i(0, display_mode_esc_sequence_len("\033[31", NULL));
	cl_assert_equal_i(5, display_mode_esc_sequence_len("\033[31m", NULL));

	cl_assert_equal_i(0, display_mode_esc_sequence_len("\033[31;", NULL));
	cl_assert_equal_i(6, display_mode_esc_sequence_len("\033[31;m", NULL));
	cl_assert_equal_i(7, display_mode_esc_sequence_len("\033[31;0m", NULL));
}

/*
 * Test display_mode_esc_sequence_len() with end pointers.
 */
void test_utf8_ansi__sgr_len_end_ptr(void)
{
	char *sgr_1 = (char *)"\033[m";
	char *sgr_2 = (char *)"\033[31m";
	char *sgr_3 = (char *)"\033[31;m";
	char *sgr_4 = (char *)"\033[31;0m";

	cl_assert_equal_i(0, display_mode_esc_sequence_len("", ""));

	cl_assert_equal_i(0, display_mode_esc_sequence_len(sgr_1, sgr_1 + 1));
	cl_assert_equal_i(0, display_mode_esc_sequence_len(sgr_1, sgr_1 + 2));
	cl_assert_equal_i(3, display_mode_esc_sequence_len(sgr_1, sgr_1 + 3));

	cl_assert_equal_i(0, display_mode_esc_sequence_len(sgr_2, sgr_2 + 1));
	cl_assert_equal_i(0, display_mode_esc_sequence_len(sgr_2, sgr_2 + 2));
	cl_assert_equal_i(0, display_mode_esc_sequence_len(sgr_2, sgr_2 + 3));
	cl_assert_equal_i(0, display_mode_esc_sequence_len(sgr_2, sgr_2 + 4));
	cl_assert_equal_i(5, display_mode_esc_sequence_len(sgr_2, sgr_2 + 5));

	cl_assert_equal_i(6, display_mode_esc_sequence_len(sgr_3, sgr_3 + sizeof(sgr_3) - 1));
	cl_assert_equal_i(7, display_mode_esc_sequence_len(sgr_4, sgr_4 + sizeof(sgr_4) - 1));
}

/*
 * Test display_mode_esc_sequence_len() for swapped start and end pointers.
 */
void test_utf8_ansi__sgr_len_start_end_swap(void)
{
	char *sgr_1 = (char *)"\033[m";

	cl_assert_equal_i(0, display_mode_esc_sequence_len(sgr_1 + 3, sgr_1));
}
