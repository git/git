/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "git-compat-util.h"
#include "hash.h"

#include "reftable-blocksource.h"
#include "reftable-error.h"
#include "reftable-merged.h"
#include "reftable-record.h"
#include "reftable-tests.h"
#include "reftable-writer.h"
#include "reftable-iterator.h"
#include "reftable-reader.h"
#include "reftable-stack.h"
#include "reftable-generic.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static int compact_stack(const char *stackdir)
{
	struct reftable_stack *stack = NULL;
	struct reftable_write_options cfg = { 0 };

	int err = reftable_new_stack(&stack, stackdir, cfg);
	if (err < 0)
		goto done;

	err = reftable_stack_compact_all(stack, NULL);
	if (err < 0)
		goto done;
done:
	if (stack) {
		reftable_stack_destroy(stack);
	}
	return err;
}

static void print_help(void)
{
	printf("usage: dump [-cst] arg\n\n"
	       "options: \n"
	       "  -c compact\n"
	       "  -t dump table\n"
	       "  -s dump stack\n"
	       "  -6 sha256 hash format\n"
	       "  -h this help\n"
	       "\n");
}

int reftable_dump_main(int argc, char *const *argv)
{
	int err = 0;
	int opt_dump_table = 0;
	int opt_dump_stack = 0;
	int opt_compact = 0;
	uint32_t opt_hash_id = GIT_SHA1_FORMAT_ID;
	const char *arg = NULL, *argv0 = argv[0];

	for (; argc > 1; argv++, argc--)
		if (*argv[1] != '-')
			break;
		else if (!strcmp("-t", argv[1]))
			opt_dump_table = 1;
		else if (!strcmp("-6", argv[1]))
			opt_hash_id = GIT_SHA256_FORMAT_ID;
		else if (!strcmp("-s", argv[1]))
			opt_dump_stack = 1;
		else if (!strcmp("-c", argv[1]))
			opt_compact = 1;
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

	if (opt_dump_table) {
		err = reftable_reader_print_file(arg);
	} else if (opt_dump_stack) {
		err = reftable_stack_print_directory(arg, opt_hash_id);
	} else if (opt_compact) {
		err = compact_stack(arg);
	}

	if (err < 0) {
		fprintf(stderr, "%s: %s: %s\n", argv0, arg,
			reftable_error_str(err));
		return 1;
	}
	return 0;
}
