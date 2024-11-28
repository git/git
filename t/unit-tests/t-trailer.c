#define DISABLE_SIGN_COMPARE_WARNINGS

#include "test-lib.h"
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
			check_str(iter.raw, contents[i].raw);
			check_str(iter.key.buf, contents[i].key);
			check_str(iter.val.buf, contents[i].val);
		}
		i++;
	}
	trailer_iterator_release(&iter);

	check_uint(i, ==, num_expected);
}

static void run_t_trailer_iterator(void)
{

	static struct test_cases {
		const char *name;
		const char *msg;
		size_t num_expected;
		struct contents contents[10];
	} tc[] = {
		{
			"empty input",
			"",
			0,
			{{0}},
		},
		{
			"no newline at beginning",
			"Fixes: x\n"
			"Acked-by: x\n"
			"Reviewed-by: x\n",
			0,
			{{0}},
		},
		{
			"newline at beginning",
			"\n"
			"Fixes: x\n"
			"Acked-by: x\n"
			"Reviewed-by: x\n",
			3,
			{
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
			},
		},
		{
			"without body text",
			"subject: foo bar\n"
			"\n"
			"Fixes: x\n"
			"Acked-by: x\n"
			"Reviewed-by: x\n",
			3,
			{
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
			},
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
			4,
			{
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
			},
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
			2,
			{
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
			},
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
			1,
			{
				{
					.raw = "Signed-off-by: x\n",
					.key = "Signed-off-by",
					.val = "x",
				},
				{
					0
				},
			},
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
			4,
			{
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
			},
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
			0,
			{{0}},
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
			0,
			{{0}},
		},
	};

	for (int i = 0; i < sizeof(tc) / sizeof(tc[0]); i++) {
		TEST(t_trailer_iterator(tc[i].msg,
					tc[i].num_expected,
					tc[i].contents),
		     "%s", tc[i].name);
	}
}

int cmd_main(int argc UNUSED, const char **argv UNUSED)
{
	run_t_trailer_iterator();
	return test_done();
}
