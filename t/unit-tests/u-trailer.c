#define DISABLE_SIGN_COMPARE_WARNINGS

#include "unit-test.h"
#include "trailer.h"

struct contents {
	const char *raw;
	const char *key;
	const char *val;
};

static void t_trailer_iterator(const char *msg, size_t num_expected,
			       struct contents *contents)
{
	struct trailer_iterator iter;
	size_t i = 0;

	trailer_iterator_init(&iter, msg);
	while (trailer_iterator_advance(&iter)) {
		if (num_expected) {
			cl_assert_equal_s(iter.raw, contents[i].raw);
			cl_assert_equal_s(iter.key.buf, contents[i].key);
			cl_assert_equal_s(iter.val.buf, contents[i].val);
		}
		i++;
	}
	trailer_iterator_release(&iter);

	cl_assert_equal_i(i, num_expected);
}

void test_trailer__empty_input(void)
{
	struct contents expected_contents[] = { 0 };
	t_trailer_iterator("", 0, expected_contents);
}

void test_trailer__no_newline_start(void)
{
	struct contents expected_contents[] = { 0 };

	t_trailer_iterator("Fixes: x\n"
			   "Acked-by: x\n"
			   "Reviewed-by: x\n",
			   0,
			   expected_contents);
}

void test_trailer__newline_start(void)
{
	struct contents expected_contents[] = {
		{
			.raw = "Fixes: x\n",
			.key = "Fixes",
			.val = "x",
		},
		{
			.raw = "Acked-by: x\n",
			.key = "Acked-by",
			.val = "x",
		},
		{
			.raw = "Reviewed-by: x\n",
			.key = "Reviewed-by",
			.val = "x",
		},
		{
			0
		},
	};

	t_trailer_iterator("\n"
			   "Fixes: x\n"
			   "Acked-by: x\n"
			   "Reviewed-by: x\n",
			   3,
			   expected_contents);
}

void test_trailer__no_body_text(void)
{
	struct contents expected_contents[] = {

		{
			.raw = "Fixes: x\n",
			.key = "Fixes",
			.val = "x",
		},
		{
			.raw = "Acked-by: x\n",
			.key = "Acked-by",
			.val = "x",
		},
		{
			.raw = "Reviewed-by: x\n",
			.key = "Reviewed-by",
			.val = "x",
		},
		{
			0
		},
	};

	t_trailer_iterator("subject: foo bar\n"
			   "\n"
			   "Fixes: x\n"
			   "Acked-by: x\n"
			   "Reviewed-by: x\n",
			   3,
			   expected_contents);
}

void test_trailer__body_text_no_divider(void)
{
	struct contents expected_contents[] = {
		{
			.raw = "Fixes: x\n",
			.key = "Fixes",
			.val = "x",
		},
		{
			.raw = "Acked-by: x\n",
			.key = "Acked-by",
			.val = "x",
		},
		{
			.raw = "Reviewed-by: x\n",
			.key = "Reviewed-by",
			.val = "x",
		},
		{
			.raw = "Signed-off-by: x\n",
			.key = "Signed-off-by",
			.val = "x",
		},
		{
			0
		},
	};

	t_trailer_iterator("my subject\n"
			   "\n"
			   "my body which is long\n"
			   "and contains some special\n"
			   "chars like : = ? !\n"
			   "hello\n"
			   "\n"
			   "Fixes: x\n"
			   "Acked-by: x\n"
			   "Reviewed-by: x\n"
			   "Signed-off-by: x\n",
			   4,
			   expected_contents);
}

void test_trailer__body_no_divider_2nd_block(void)
{
	struct contents expected_contents[] = {
			{
				.raw = "Helped-by: x\n",
				.key = "Helped-by",
				.val = "x",
			},
			{
				.raw = "Signed-off-by: x\n",
				.key = "Signed-off-by",
				.val = "x",
			},
			{
				0
			},
	};

	t_trailer_iterator("my subject\n"
			   "\n"
			   "my body which is long\n"
			   "and contains some special\n"
			   "chars like : = ? !\n"
			   "hello\n"
			   "\n"
			   "Fixes: x\n"
			   "Acked-by: x\n"
			   "Reviewed-by: x\n"
			   "Signed-off-by: x\n"
			   "\n"
			   /*
			   * Because this is the last trailer block, it takes
			   * precedence over the first one encountered above.
			   */
			   "Helped-by: x\n"
			   "Signed-off-by: x\n",
			   2,
			   expected_contents);
}

void test_trailer__body_and_divider(void)
{
	struct contents expected_contents[] = {
			{
				.raw = "Signed-off-by: x\n",
				.key = "Signed-off-by",
				.val = "x",
			},
			{
				0
			},
	};

	t_trailer_iterator("my subject\n"
			   "\n"
			   "my body which is long\n"
			   "and contains some special\n"
			   "chars like : = ? !\n"
			   "hello\n"
			   "\n"
			   "---\n"
			   "\n"
			   /*
			   * This trailer still counts because the iterator
			   * always ignores the divider.
			   */
			   "Signed-off-by: x\n",
			   1,
			   expected_contents);
}

void test_trailer__non_trailer_in_block(void)
{
	struct contents expected_contents[] = {
		{
			.raw = "not a trailer line\n",
			.key = "not a trailer line",
			.val = "",
		},
		{
			.raw = "not a trailer line\n",
			.key = "not a trailer line",
			.val = "",
		},
		{
			.raw = "not a trailer line\n",
			.key = "not a trailer line",
			.val = "",
		},
		{
			.raw = "Signed-off-by: x\n",
			.key = "Signed-off-by",
			.val = "x",
		},
		{
			0
		},
	};

	t_trailer_iterator("subject: foo bar\n"
			   "\n"
			   /*
			   * Even though this trailer block has a non-trailer line
			   * in it, it's still a valid trailer block because it's
			   * at least 25% trailers and is Git-generated (see
			   * git_generated_prefixes[] in trailer.c).
			   */
			   "not a trailer line\n"
			   "not a trailer line\n"
			   "not a trailer line\n"
			   "Signed-off-by: x\n",
			   /*
			   * Even though there is only really 1 real "trailer"
			   * (Signed-off-by), we still have 4 trailer objects
			   * because we still want to iterate through the entire
			   * block.
			   */
			   4,
			   expected_contents);
}

void test_trailer__too_many_non_trailers(void)
{
	struct contents expected_contents[] = { 0 };

	t_trailer_iterator("subject: foo bar\n"
			   "\n"
			   /*
			   * This block has only 20% trailers, so it's below the
			   * 25% threshold.
			   */
			   "not a trailer line\n"
			   "not a trailer line\n"
			   "not a trailer line\n"
			   "not a trailer line\n"
			   "Signed-off-by: x\n",
			   0,
			   expected_contents);
}

void test_trailer__one_non_trailer_no_git_trailers(void)
{
	struct contents expected_contents[] = { 0 };

	t_trailer_iterator("subject: foo bar\n"
			   "\n"
			   /*
			   * This block has only 1 non-trailer out of 10 (IOW, 90%
			   * trailers) but is not considered a trailer block
			   * because the 25% threshold only applies to cases where
			   * there was a Git-generated trailer.
			   */
			   "Reviewed-by: x\n"
			   "Reviewed-by: x\n"
			   "Reviewed-by: x\n"
			   "Helped-by: x\n"
			   "Helped-by: x\n"
			   "Helped-by: x\n"
			   "Acked-by: x\n"
			   "Acked-by: x\n"
			   "Acked-by: x\n"
			   "not a trailer line\n",
			   0,
			   expected_contents);
}
