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

#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "run-command.h"

#define FAILED_RUN "failed to run %s"

static const char * const builtin_gc_usage[] = {
	"git gc [options]",
	NULL
};

static int pack_refs = 1;
static int aggressive_window = 250;
static int gc_auto_threshold = 6700;
static int gc_auto_pack_limit = 50;
static const char *prune_expire = "2.weeks.ago";

#define MAX_ADD 10
static const char *argv_pack_refs[] = {"pack-refs", "--all", "--prune", NULL};
static const char *argv_reflog[] = {"reflog", "expire", "--all", NULL};
static const char *argv_repack[MAX_ADD] = {"repack", "-d", "-l", NULL};
static const char *argv_prune[] = {"prune", "--expire", NULL, NULL};
static const char *argv_rerere[] = {"rerere", "gc", NULL};

static int gc_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "gc.packrefs")) {
		if (value && !strcmp(value, "notbare"))
			pack_refs = -1;
		else
			pack_refs = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "gc.aggressivewindow")) {
		aggressive_window = git_config_int(var, value);
		return 0;
	}
	if (!strcmp(var, "gc.auto")) {
		gc_auto_threshold = git_config_int(var, value);
		return 0;
	}
	if (!strcmp(var, "gc.autopacklimit")) {
		gc_auto_pack_limit = git_config_int(var, value);
		return 0;
	}
	if (!strcmp(var, "gc.pruneexpire")) {
		if (value && strcmp(value, "now")) {
			unsigned long now = approxidate("now");
			if (approxidate(value) >= now)
				return error(_("Invalid %s: '%s'"), var, value);
		}
		return git_config_string(&prune_expire, var, value);
	}
	return git_default_config(var, value, cb);
}

static void append_option(const char **cmd, const char *opt, int max_length)
{
	int i;

	for (i = 0; cmd[i]; i++)
		;

	if (i + 2 >= max_length)
		die(_("Too many options specified"));
	cmd[i++] = opt;
	cmd[i] = NULL;
}

static int too_many_loose_objects(void)
{
	/*
	 * Quickly check if a "gc" is needed, by estimating how
	 * many loose objects there are.  Because SHA-1 is evenly
	 * distributed, we can check only one and get a reasonable
	 * estimate.
	 */
	char path[PATH_MAX];
	const char *objdir = get_object_directory();
	DIR *dir;
	struct dirent *ent;
	int auto_threshold;
	int num_loose = 0;
	int needed = 0;

	if (gc_auto_threshold <= 0)
		return 0;

	if (sizeof(path) <= snprintf(path, sizeof(path), "%s/17", objdir)) {
		warning(_("insanely long object directory %.*s"), 50, objdir);
		return 0;
	}
	dir = opendir(path);
	if (!dir)
		return 0;

	auto_threshold = (gc_auto_threshold + 255) / 256;
	while ((ent = readdir(dir)) != NULL) {
		if (strspn(ent->d_name, "0123456789abcdef") != 38 ||
		    ent->d_name[38] != '\0')
			continue;
		if (++num_loose > auto_threshold) {
			needed = 1;
			break;
		}
	}
	closedir(dir);
	return needed;
}

static int too_many_packs(void)
{
	struct packed_git *p;
	int cnt;

	if (gc_auto_pack_limit <= 0)
		return 0;

	prepare_packed_git();
	for (cnt = 0, p = packed_git; p; p = p->next) {
		if (!p->pack_local)
			continue;
		if (p->pack_keep)
			continue;
		/*
		 * Perhaps check the size of the pack and count only
		 * very small ones here?
		 */
		cnt++;
	}
	return gc_auto_pack_limit <= cnt;
}

static int need_to_gc(void)
{
	/*
	 * Setting gc.auto to 0 or negative can disable the
	 * automatic gc.
	 */
	if (gc_auto_threshold <= 0)
		return 0;

	/*
	 * If there are too many loose objects, but not too many
	 * packs, we run "repack -d -l".  If there are too many packs,
	 * we run "repack -A -d -l".  Otherwise we tell the caller
	 * there is no need.
	 */
	if (too_many_packs())
		append_option(argv_repack,
			      prune_expire && !strcmp(prune_expire, "now") ?
			      "-a" : "-A",
			      MAX_ADD);
	else if (!too_many_loose_objects())
		return 0;

	if (run_hook(NULL, "pre-auto-gc", NULL))
		return 0;
	return 1;
}

int cmd_gc(int argc, const char **argv, const char *prefix)
{
	int aggressive = 0;
	int auto_gc = 0;
	int quiet = 0;
	char buf[80];

	struct option builtin_gc_options[] = {
		OPT__QUIET(&quiet, "suppress progress reporting"),
		{ OPTION_STRING, 0, "prune", &prune_expire, "date",
			"prune unreferenced objects",
			PARSE_OPT_OPTARG, NULL, (intptr_t)prune_expire },
		OPT_BOOLEAN(0, "aggressive", &aggressive, "be more thorough (increased runtime)"),
		OPT_BOOLEAN(0, "auto", &auto_gc, "enable auto-gc mode"),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_gc_usage, builtin_gc_options);

	git_config(gc_config, NULL);

	if (pack_refs < 0)
		pack_refs = !is_bare_repository();

	argc = parse_options(argc, argv, prefix, builtin_gc_options,
			     builtin_gc_usage, 0);
	if (argc > 0)
		usage_with_options(builtin_gc_usage, builtin_gc_options);

	if (aggressive) {
		append_option(argv_repack, "-f", MAX_ADD);
		append_option(argv_repack, "--depth=250", MAX_ADD);
		if (aggressive_window > 0) {
			sprintf(buf, "--window=%d", aggressive_window);
			append_option(argv_repack, buf, MAX_ADD);
		}
	}
	if (quiet)
		append_option(argv_repack, "-q", MAX_ADD);

	if (auto_gc) {
		/*
		 * Auto-gc should be least intrusive as possible.
		 */
		if (!need_to_gc())
			return 0;
		if (quiet)
			fprintf(stderr, _("Auto packing the repository for optimum performance.\n"));
		else
			fprintf(stderr,
					_("Auto packing the repository for optimum performance. You may also\n"
					"run \"git gc\" manually. See "
					"\"git help gc\" for more information.\n"));
	} else
		append_option(argv_repack,
			      prune_expire && !strcmp(prune_expire, "now")
			      ? "-a" : "-A",
			      MAX_ADD);

	if (pack_refs && run_command_v_opt(argv_pack_refs, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_pack_refs[0]);

	if (run_command_v_opt(argv_reflog, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_reflog[0]);

	if (run_command_v_opt(argv_repack, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_repack[0]);

	if (prune_expire) {
		argv_prune[2] = prune_expire;
		if (run_command_v_opt(argv_prune, RUN_GIT_CMD))
			return error(FAILED_RUN, argv_prune[0]);
	}

	if (run_command_v_opt(argv_rerere, RUN_GIT_CMD))
		return error(FAILED_RUN, argv_rerere[0]);

	if (auto_gc && too_many_loose_objects())
		warning(_("There are too many unreachable loose objects; "
			"run 'git prune' to remove them."));

	return 0;
}
