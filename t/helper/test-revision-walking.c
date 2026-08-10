/*
 * test-revision-walking.c: test revision walking API.
 *
 * (C) 2012 Heiko Voigt <hvoigt@hvoigt.net>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "commit.h"
#include "diff.h"
#include "line-log.h"
#include "object-name.h"
#include "repository.h"
#include "revision.h"
#include "setup.h"
#include "string-list.h"

static void print_commit(struct commit *commit)
{
	struct strbuf sb = STRBUF_INIT;
	struct pretty_print_context ctx = {0};
	ctx.date_mode.type = DATE_NORMAL;
	repo_format_commit_message(the_repository, commit, " %m %s", &sb,
				   &ctx);
	printf("%s\n", sb.buf);
	strbuf_release(&sb);
}

static int run_revision_walk(void)
{
	struct rev_info rev;
	struct commit *commit;
	const char *argv[] = {NULL, "--all", NULL};
	int argc = ARRAY_SIZE(argv) - 1;
	int got_revision = 0;

	repo_init_revisions(the_repository, &rev, NULL);
	setup_revisions(argc, argv, &rev, NULL);
	if (prepare_revision_walk(&rev))
		die("revision walk setup failed");

	while ((commit = get_revision(&rev)) != NULL) {
		print_commit(commit);
		got_revision = 1;
	}

	reset_revision_walk();
	release_revisions(&rev);
	return got_revision;
}

/*
 * Check that get_commit_action() is a pure predicate by evaluating it on a
 * commit the walk has not reached yet.  No git command makes that out-of-order
 * call, so this probe does it deliberately, and reports whether the call
 * mutated the peeked commit: a pure get_commit_action() leaves it untouched.
 * We compare the commit's flags rather than the emitted commit list because
 * range merges are idempotent, so a side effect would not change which commits
 * are shown.  Only meaningful for a plain "-L" walk with no parent rewriting.
 */
static int line_log_peek(const char **argv)
{
	struct repository *repo = the_repository;
	struct rev_info rev;
	struct string_list range_args = STRING_LIST_INIT_DUP;
	struct object_id oid;
	struct commit *peek;
	const char *rev_argv[3];
	unsigned before, after;

	if (repo_get_oid(repo, argv[0], &oid))
		die("bad peek commit: %s", argv[0]);
	peek = lookup_commit_reference(repo, &oid);
	if (!peek || repo_parse_commit(repo, peek))
		die("cannot parse peek commit: %s", argv[0]);

	repo_init_revisions(repo, &rev, NULL);
	rev.diffopt.flags.recursive = 1;
	rev.line_level_traverse = 1;
	string_list_append(&range_args, argv[1]);

	rev_argv[0] = "line-log-peek";
	rev_argv[1] = argv[2];
	rev_argv[2] = NULL;
	setup_revisions(2, rev_argv, &rev, NULL);

	line_log_init(&rev, NULL, &range_args);

	if (rev.rewrite_parents || rev.children.name)
		die("line-log-peek requires a non-ancestry (-L, no --graph) walk");

	if (prepare_revision_walk(&rev))
		die("prepare_revision_walk failed");

	before = peek->object.flags;
	get_commit_action(&rev, peek);
	after = peek->object.flags;

	printf("mutated %d\n", before != after);

	release_revisions(&rev);
	string_list_clear(&range_args, 0);
	return 0;
}

int cmd__revision_walking(int argc, const char **argv)
{
	if (argc < 2)
		return 1;

	setup_git_directory(the_repository);

	if (!strcmp(argv[1], "run-twice")) {
		printf("1st\n");
		if (!run_revision_walk())
			return 1;
		printf("2nd\n");
		if (!run_revision_walk())
			return 1;

		return 0;
	}

	if (!strcmp(argv[1], "line-log-peek")) {
		if (argc != 5)
			die("usage: test-tool revision-walking line-log-peek <peek-commit> <start,end:file> <rev>");
		return line_log_peek(argv + 2);
	}

	fprintf(stderr, "check usage\n");
	return 1;
}
