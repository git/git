/*
 * git gc builtin command
 *
 * Cleanup unreachable files and optimize the repository.
 *
 * Copyright (c) 2007 James Bowes
 *
 * Based on git-gc.sh, which is
 *
 * Copyright (c) 2006 Shawn O. Pearce
 */

#include "cache.h"
#include "run-command.h"

#define FAILED_RUN "failed to run %s"

static const char builtin_gc_usage[] = "git-gc [--prune]";

static int pack_refs = -1;

static const char *argv_pack_refs[] = {"pack-refs", "--prune", NULL};
static const char *argv_reflog[] = {"reflog", "expire", "--all", NULL};
static const char *argv_repack[] = {"repack", "-a", "-d", "-l", NULL};
static const char *argv_prune[] = {"prune", NULL};
static const char *argv_rerere[] = {"rerere", "gc", NULL};

static int gc_config(const char *var, const char *value)
{
	if (!strcmp(var, "gc.packrefs")) {
		if (!strcmp(value, "notbare"))
			pack_refs = -1;
		else
			pack_refs = git_config_bool(var, value);
		return 0;
	}
	return git_default_config(var, value);
}

int cmd_gc(int argc, const char **argv, const char *prefix)
{
	int i;
	int prune = 0;

	git_config(gc_config);

	if (pack_refs < 0)
		pack_refs = !is_bare_repository();

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--prune")) {
			prune = 1;
			continue;
		}
		/* perhaps other parameters later... */
		break;
	}
	if (i != argc)
		usage(builtin_gc_usage);

	if (pack_refs && run_command_v_opt(argv_pack_refs, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_pack_refs[0]);

	if (run_command_v_opt(argv_reflog, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_reflog[0]);

	if (run_command_v_opt(argv_repack, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_repack[0]);

	if (prune && run_command_v_opt(argv_prune, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_prune[0]);

	if (run_command_v_opt(argv_rerere, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_rerere[0]);

	return 0;
}
