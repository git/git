#include "reftable/system.h"
#include "reftable/reftable-error.h"
#include "reftable/reftable-reader.h"
#include "reftable/reftable-stack.h"
#include "reftable/reftable-tests.h"
#include "test-tool.h"

int cmd__reftable(int argc, const char **argv)
{
	/* test from simple to complex. */
	block_test_main(argc, argv);
	tree_test_main(argc, argv);
	pq_test_main(argc, argv);
	readwrite_test_main(argc, argv);
	stack_test_main(argc, argv);
	return 0;
}

static void print_help(void)
{
	printf("usage: dump [-st] arg\n\n"
	       "options: \n"
	       "  -b dump blocks\n"
	       "  -t dump table\n"
	       "  -s dump stack\n"
	       "  -6 sha256 hash format\n"
	       "  -h this help\n"
	       "\n");
}

int cmd__dump_reftable(int argc, const char **argv)
{
	int err = 0;
	int opt_dump_blocks = 0;
	int opt_dump_table = 0;
	int opt_dump_stack = 0;
	uint32_t opt_hash_id = GIT_SHA1_FORMAT_ID;
	const char *arg = NULL, *argv0 = argv[0];

	for (; argc > 1; argv++, argc--)
		if (*argv[1] != '-')
			break;
		else if (!strcmp("-b", argv[1]))
			opt_dump_blocks = 1;
		else if (!strcmp("-t", argv[1]))
			opt_dump_table = 1;
		else if (!strcmp("-6", argv[1]))
			opt_hash_id = GIT_SHA256_FORMAT_ID;
		else if (!strcmp("-s", argv[1]))
			opt_dump_stack = 1;
		else if (!strcmp("-?", argv[1]) || !strcmp("-h", argv[1])) {
			print_help();
			return 2;
		}

	if (argc != 2) {
		fprintf(stderr, "need argument\n");
		print_help();
		return 2;
	}

	arg = argv[1];

	if (opt_dump_blocks) {
		err = reftable_reader_print_blocks(arg);
	} else if (opt_dump_table) {
		err = reftable_reader_print_file(arg);
	} else if (opt_dump_stack) {
		err = reftable_stack_print_directory(arg, opt_hash_id);
	}

	if (err < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv0, arg,
			reftable_error_str(err));
		return 1;
	}
	return 0;
}
