#include "git-compat-util.h"
#include "hash.h"
#include "hex.h"
#include "reftable/system.h"
#include "reftable/reftable-error.h"
#include "reftable/reftable-merged.h"
#include "reftable/reftable-reader.h"
#include "reftable/reftable-stack.h"
#include "test-tool.h"

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

static int dump_table(struct reftable_merged_table *mt)
{
	struct reftable_iterator it = { NULL };
	struct reftable_ref_record ref = { NULL };
	struct reftable_log_record log = { NULL };
	const struct git_hash_algo *algop;
	int err;

	err = reftable_merged_table_init_ref_iterator(mt, &it);
	if (err < 0)
		return err;

	err = reftable_iterator_seek_ref(&it, "");
	if (err < 0)
		return err;

	algop = &hash_algos[hash_algo_by_id(reftable_merged_table_hash_id(mt))];

	while (1) {
		err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0)
			break;
		if (err < 0)
			return err;

		printf("ref{%s(%" PRIu64 ") ", ref.refname, ref.update_index);
		switch (ref.value_type) {
		case REFTABLE_REF_SYMREF:
			printf("=> %s", ref.value.symref);
			break;
		case REFTABLE_REF_VAL2:
			printf("val 2 %s", hash_to_hex_algop(ref.value.val2.value, algop));
			printf("(T %s)", hash_to_hex_algop(ref.value.val2.target_value, algop));
			break;
		case REFTABLE_REF_VAL1:
			printf("val 1 %s", hash_to_hex_algop(ref.value.val1, algop));
			break;
		case REFTABLE_REF_DELETION:
			printf("delete");
			break;
		}
		printf("}\n");
	}
	reftable_iterator_destroy(&it);
	reftable_ref_record_release(&ref);

	err = reftable_merged_table_init_log_iterator(mt, &it);
	if (err < 0)
		return err;

	err = reftable_iterator_seek_log(&it, "");
	if (err < 0)
		return err;

	while (1) {
		err = reftable_iterator_next_log(&it, &log);
		if (err > 0)
			break;
		if (err < 0)
			return err;

		switch (log.value_type) {
		case REFTABLE_LOG_DELETION:
			printf("log{%s(%" PRIu64 ") delete\n", log.refname,
			       log.update_index);
			break;
		case REFTABLE_LOG_UPDATE:
			printf("log{%s(%" PRIu64 ") %s <%s> %" PRIu64 " %04d\n",
			       log.refname, log.update_index,
			       log.value.update.name ? log.value.update.name : "",
			       log.value.update.email ? log.value.update.email : "",
			       log.value.update.time,
			       log.value.update.tz_offset);
			printf("%s => ", hash_to_hex_algop(log.value.update.old_hash, algop));
			printf("%s\n\n%s\n}\n", hash_to_hex_algop(log.value.update.new_hash, algop),
			       log.value.update.message ? log.value.update.message : "");
			break;
		}
	}
	reftable_iterator_destroy(&it);
	reftable_log_record_release(&log);
	return 0;
}

static int dump_stack(const char *stackdir, uint32_t hash_id)
{
	struct reftable_stack *stack = NULL;
	struct reftable_write_options opts = { .hash_id = hash_id };
	struct reftable_merged_table *merged = NULL;

	int err = reftable_new_stack(&stack, stackdir, &opts);
	if (err < 0)
		goto done;

	merged = reftable_stack_merged_table(stack);
	err = dump_table(merged);
done:
	if (stack)
		reftable_stack_destroy(stack);
	return err;
}

static int dump_reftable(const char *tablename)
{
	struct reftable_block_source src = { 0 };
	struct reftable_merged_table *mt = NULL;
	struct reftable_reader *r = NULL;
	int err;

	err = reftable_block_source_from_file(&src, tablename);
	if (err < 0)
		goto done;

	err = reftable_reader_new(&r, &src, tablename);
	if (err < 0)
		goto done;

	err = reftable_merged_table_new(&mt, &r, 1,
					reftable_reader_hash_id(r));
	if (err < 0)
		goto done;

	err = dump_table(mt);

done:
	reftable_merged_table_free(mt);
	reftable_reader_decref(r);
	return err;
}

int cmd__dump_reftable(int argc, const char **argv)
{
	int err = 0;
	int opt_dump_blocks = 0;
	int opt_dump_table = 0;
	int opt_dump_stack = 0;
	uint32_t opt_hash_id = REFTABLE_HASH_SHA1;
	const char *arg = NULL, *argv0 = argv[0];

	for (; argc > 1; argv++, argc--)
		if (*argv[1] != '-')
			break;
		else if (!strcmp("-b", argv[1]))
			opt_dump_blocks = 1;
		else if (!strcmp("-t", argv[1]))
			opt_dump_table = 1;
		else if (!strcmp("-6", argv[1]))
			opt_hash_id = REFTABLE_HASH_SHA256;
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
