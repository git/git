/*
 * Check-out files from the "current cache directory"
 *
 * Copyright (C) 2005 Linus Torvalds
 *
 * Careful: order of argument flags does matter. For example,
 *
 *	git-checkout-cache -a -f file.c
 *
 * Will first check out all files listed in the cache (but not
 * overwrite any old ones), and then force-checkout "file.c" a
 * second time (ie that one _will_ overwrite any old contents
 * with the same filename).
 *
 * Also, just doing "git-checkout-cache" does nothing. You probably
 * meant "git-checkout-cache -a". And if you want to force it, you
 * want "git-checkout-cache -f -a".
 *
 * Intuitiveness is not the goal here. Repeatability is. The
 * reason for the "no arguments means no work" thing is that
 * from scripts you are supposed to be able to do things like
 *
 *	find . -name '*.h' -print0 | xargs -0 git-checkout-cache -f --
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
				"git-checkout-cache: %s is %s.\n",
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
	int i;

	for (i = 0; i < active_nr ; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce))
			continue;
		if (checkout_entry(ce, &state) < 0)
			return -1;
	}
	return 0;
}

static const char checkout_cache_usage[] =
"git-checkout-cache [-u] [-q] [-a] [-f] [-n] [--prefix=<string>] [--] <file>...";

int main(int argc, char **argv)
{
	int i, force_filename = 0;
	struct cache_file cache_file;
	int newfd = -1;

	if (read_cache() < 0) {
		die("invalid cache");
	}

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!force_filename) {
			if (!strcmp(arg, "-a")) {
				checkout_all();
				continue;
			}
			if (!strcmp(arg, "--")) {
				force_filename = 1;
				continue;
			}
			if (!strcmp(arg, "-f")) {
				state.force = 1;
				continue;
			}
			if (!strcmp(arg, "-q")) {
				state.quiet = 1;
				continue;
			}
			if (!strcmp(arg, "-n")) {
				state.not_new = 1;
				continue;
			}
			if (!strcmp(arg, "-u")) {
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
		checkout_file(arg);
	}

	if (0 <= newfd &&
	    (write_cache(newfd, active_cache, active_nr) ||
	     commit_index_file(&cache_file)))
		die("Unable to write new cachefile");
	return 0;
}
