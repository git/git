#include "test-lib.h"
#include "trailer.h"

static void t_trailer_iterator(const char *msg, size_t num_expected)
{
	struct trailer_iterator iter;
	size_t i = 0;

	trailer_iterator_init(&iter, msg);
	while (trailer_iterator_advance(&iter))
		i++;
	trailer_iterator_release(&iter);

	check_uint(i, ==, num_expected);
}

static void run_t_trailer_iterator(void)
{
	static struct test_cases {
		const char *name;
		const char *msg;
		size_t num_expected;
	} tc[] = {
		{
			"empty input",
			"",
			0
		},
		{
			"no newline at beginning",
			"Fixes: x\n"
			"Acked-by: x\n"
			"Reviewed-by: x\n",
			0
		},
		{
			"newline at beginning",
			"\n"
			"Fixes: x\n"
			"Acked-by: x\n"
			"Reviewed-by: x\n",
			3
		},
		{
			"without body text",
			"subject: foo bar\n"
			"\n"
			"Fixes: x\n"
			"Acked-by: x\n"
			"Reviewed-by: x\n",
			3
		},
		{
			"with body text, without divider",
			"my subject\n"
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
			4
		},
		{
			"with body text, without divider (second trailer block)",
			"my subject\n"
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
			2
		},
		{
			"with body text, with divider",
			"my subject\n"
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
			1
		},
		{
			"with non-trailer lines in trailer block",
			"subject: foo bar\n"
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
			4
		},
		{
			"with non-trailer lines (one too many) in trailer block",
			"subject: foo bar\n"
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
			0
		},
		{
			"with non-trailer lines (only 1) in trailer block, but no Git-generated trailers",
			"subject: foo bar\n"
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
			0
		},
	};

	for (int i = 0; i < sizeof(tc) / sizeof(tc[0]); i++) {
		TEST(t_trailer_iterator(tc[i].msg,
					tc[i].num_expected),
		     "%s", tc[i].name);
	}
}

int cmd_main(int argc, const char **argv)
{
	run_t_trailer_iterator();
	return test_done();
}
