/*
 * Check-out files from the "current cache directory"
 *
 * Copyright (C) 2005 Linus Torvalds
 *
 * Careful: order of argument flags does matter. For example,
 *
 *	git-checkout-index -a -f file.c
 *
 * Will first check out all files listed in the cache (but not
 * overwrite any old ones), and then force-checkout "file.c" a
 * second time (ie that one _will_ overwrite any old contents
 * with the same filename).
 *
 * Also, just doing "git-checkout-index" does nothing. You probably
 * meant "git-checkout-index -a". And if you want to force it, you
 * want "git-checkout-index -f -a".
 *
 * Intuitiveness is not the goal here. Repeatability is. The
 * reason for the "no arguments means no work" thing is that
 * from scripts you are supposed to be able to do things like
 *
 *	find . -name '*.h' -print0 | xargs -0 git-checkout-index -f --
 *
 * which will force all existing *.h files to be replaced with
 * their cached copies. If an empty command line implied "all",
 * then this would force-refresh everything in the cache, which
 * was not the point.
 *
 * Oh, and the "--" is just a good idea when you know the rest
 * will be filenames. Just so that you wouldn't have a filename
 * of "-a" causing problems (not possible in the above example,
 * but get used to it in scripting!).
 */
#include "cache.h"

static const char *prefix;
static int prefix_length;

static struct checkout state = {
	.base_dir = "",
	.base_dir_len = 0,
	.force = 0,
	.quiet = 0,
	.not_new = 0,
	.refresh_cache = 0,
};

static int checkout_file(const char *name)
{
	int pos = cache_name_pos(name, strlen(name));
	if (pos < 0) {
		if (!state.quiet) {
			pos = -pos - 1;
			fprintf(stderr,
				"git-checkout-index: %s is %s.\n",
				name,
				(pos < active_nr &&
				 !strcmp(active_cache[pos]->name, name)) ?
				"unmerged" : "not in the cache");
		}
		return -1;
	}
	return checkout_entry(active_cache[pos], &state);
}

static int checkout_all(void)
{
	int i, errs = 0;

	for (i = 0; i < active_nr ; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce))
			continue;
		if (prefix && *prefix &&
		    ( ce_namelen(ce) <= prefix_length ||
		      memcmp(prefix, ce->name, prefix_length) ))
			continue;
		if (checkout_entry(ce, &state) < 0)
			errs++;
	}
	if (errs)
		/* we have already done our error reporting.
		 * exit with the same code as die().
		 */
		exit(128);
	return 0;
}

static const char checkout_cache_usage[] =
"git-checkout-index [-u] [-q] [-a] [-f] [-n] [--prefix=<string>] [--] <file>...";

static struct cache_file cache_file;

int main(int argc, char **argv)
{
	int i;
	int newfd = -1;
	int all = 0;

	prefix = setup_git_directory();
	prefix_length = prefix ? strlen(prefix) : 0;

	if (read_cache() < 0) {
		die("invalid cache");
	}

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		if (!strcmp(arg, "-a") || !strcmp(arg, "--all")) {
			all = 1;
			continue;
		}
		if (!strcmp(arg, "-f") || !strcmp(arg, "--force")) {
			state.force = 1;
			continue;
		}
		if (!strcmp(arg, "-q") || !strcmp(arg, "--quiet")) {
			state.quiet = 1;
			continue;
		}
		if (!strcmp(arg, "-n") || !strcmp(arg, "--no-create")) {
			state.not_new = 1;
			continue;
		}
		if (!strcmp(arg, "-u") || !strcmp(arg, "--index")) {
			state.refresh_cache = 1;
			if (newfd < 0)
				newfd = hold_index_file_for_update
					(&cache_file,
					 get_index_file());
			if (newfd < 0)
				die("cannot open index.lock file.");
			continue;
		}
		if (!memcmp(arg, "--prefix=", 9)) {
			state.base_dir = arg+9;
			state.base_dir_len = strlen(state.base_dir);
			continue;
		}
		if (arg[0] == '-')
			usage(checkout_cache_usage);
		break;
	}

	if (state.base_dir_len) {
		/* when --prefix is specified we do not
		 * want to update cache.
		 */
		if (state.refresh_cache) {
			close(newfd); newfd = -1;
			rollback_index_file(&cache_file);
		}
		state.refresh_cache = 0;
	}

	/* Check out named files first */
	for ( ; i < argc; i++) {
		const char *arg = argv[i];

		if (all)
			die("git-checkout-index: don't mix '--all' and explicit filenames");
		checkout_file(prefix_path(prefix, prefix_length, arg));
	}

	if (all)
		checkout_all();

	if (0 <= newfd &&
	    (write_cache(newfd, active_cache, active_nr) ||
	     commit_index_file(&cache_file)))
		die("Unable to write new cachefile");
	return 0;
}
