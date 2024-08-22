#include "reftable/system.h"
#include "reftable/reftable-error.h"
#include "reftable/reftable-generic.h"
#include "reftable/reftable-merged.h"
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

static int dump_stack(const char *stackdir, uint32_t hash_id)
{
	struct reftable_stack *stack = NULL;
	struct reftable_write_options opts = { .hash_id = hash_id };
	struct reftable_merged_table *merged = NULL;
	struct reftable_table table = { NULL };

	int err = reftable_new_stack(&stack, stackdir, &opts);
	if (err < 0)
		goto done;

	merged = reftable_stack_merged_table(stack);
	reftable_table_from_merged_table(&table, merged);
	err = reftable_table_print(&table);
done:
	if (stack)
		reftable_stack_destroy(stack);
	return err;
}

static int dump_reftable(const char *tablename)
{
	struct reftable_block_source src = { NULL };
	int err = reftable_block_source_from_file(&src, tablename);
	struct reftable_reader *r = NULL;
	struct reftable_table tab = { NULL };
	if (err < 0)
		goto done;

	err = reftable_new_reader(&r, &src, tablename);
	if (err < 0)
		goto done;

	reftable_table_from_reader(&tab, r);
	err = reftable_table_print(&tab);
done:
	reftable_reader_free(r);
	return err;
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
		err = dump_reftable(arg);
	} else if (opt_dump_stack) {
		err = dump_stack(arg, opt_hash_id);
	}

	if (err < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv0, arg,
			reftable_error_str(err));
		return 1;
	}
	return 0;
}
