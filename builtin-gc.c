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

static const char builtin_gc_usage[] = "git-gc [--prune] [--aggressive]";

static int pack_refs = -1;
static int aggressive_window = -1;

#define MAX_ADD 10
static const char *argv_pack_refs[] = {"pack-refs", "--prune", NULL};
static const char *argv_reflog[] = {"reflog", "expire", "--all", NULL};
static const char *argv_repack[MAX_ADD] = {"repack", "-a", "-d", "-l", NULL};
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
	if (!strcmp(var, "gc.aggressivewindow")) {
		aggressive_window = git_config_int(var, value);
		return 0;
	}
	return git_default_config(var, value);
}

static void append_option(const char **cmd, const char *opt, int max_length)
{
	int i;

	for (i = 0; cmd[i]; i++)
		;

	if (i + 2 >= max_length)
		die("Too many options specified");
	cmd[i++] = opt;
	cmd[i] = NULL;
}

int cmd_gc(int argc, const char **argv, const char *prefix)
{
	int i;
	int prune = 0;
	char buf[80];

	git_config(gc_config);

	if (pack_refs < 0)
		pack_refs = !is_bare_repository();

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--prune")) {
			prune = 1;
			continue;
		}
		if (!strcmp(arg, "--aggressive")) {
			append_option(argv_repack, "-f", MAX_ADD);
			if (aggressive_window > 0) {
				sprintf(buf, "--window=%d", aggressive_window);
				append_option(argv_repack, buf, MAX_ADD);
			}
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
