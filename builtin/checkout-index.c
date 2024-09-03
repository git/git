/*
 * Check-out files from the "current cache directory"
 *
 * Copyright (C) 2005 Linus Torvalds
 *
 */
#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "lockfile.h"
#include "quote.h"
#include "cache-tree.h"
#include "parse-options.h"
#include "entry.h"
#include "parallel-checkout.h"
#include "read-cache-ll.h"
#include "setup.h"
#include "sparse-index.h"

#define CHECKOUT_ALL 4
static int nul_term_line;
static int checkout_stage; /* default to checkout stage0 */
static int ignore_skip_worktree; /* default to 0 */
static int to_tempfile = -1;
static char topath[4][TEMPORARY_FILENAME_LENGTH + 1];

static struct checkout state = CHECKOUT_INIT;

static void write_tempfile_record(const char *name, const char *prefix)
{
	int i;
	int have_tempname = 0;

	if (CHECKOUT_ALL == checkout_stage) {
		for (i = 1; i < 4; i++)
			if (topath[i][0]) {
				have_tempname = 1;
				break;
			}

		if (have_tempname) {
			for (i = 1; i < 4; i++) {
				if (i > 1)
					putchar(' ');
				if (topath[i][0])
					fputs(topath[i], stdout);
				else
					putchar('.');
			}
		}
	} else if (topath[checkout_stage][0]) {
		have_tempname = 1;
		fputs(topath[checkout_stage], stdout);
	}

	if (have_tempname) {
		putchar('\t');
		write_name_quoted_relative(name, prefix, stdout,
					   nul_term_line ? '\0' : '\n');
	}

	for (i = 0; i < 4; i++) {
		topath[i][0] = 0;
	}
}

static int checkout_file(const char *name, const char *prefix)
{
	int namelen = strlen(name);
	int pos = index_name_pos(the_repository->index, name, namelen);
	int has_same_name = 0;
	int is_file = 0;
	int is_skipped = 1;
	int did_checkout = 0;
	int errs = 0;

	if (pos < 0)
		pos = -pos - 1;

	while (pos <the_repository->index->cache_nr) {
		struct cache_entry *ce =the_repository->index->cache[pos];
		if (ce_namelen(ce) != namelen ||
		    memcmp(ce->name, name, namelen))
			break;
		has_same_name = 1;
		pos++;
		if (S_ISSPARSEDIR(ce->ce_mode))
			break;
		is_file = 1;
		if (!ignore_skip_worktree && ce_skip_worktree(ce))
			break;
		is_skipped = 0;
		if (ce_stage(ce) != checkout_stage
		    && (CHECKOUT_ALL != checkout_stage || !ce_stage(ce)))
			continue;
		did_checkout = 1;
		if (checkout_entry(ce, &state,
				   to_tempfile ? topath[ce_stage(ce)] : NULL,
				   NULL) < 0)
			errs++;
	}

	if (did_checkout) {
		if (to_tempfile)
			write_tempfile_record(name, prefix);
		return errs > 0 ? -1 : 0;
	}

	/*
	 * At this point we know we didn't try to check anything out. If it was
	 * because we did find an entry but it was stage 0, that's not an
	 * error.
	 */
	if (has_same_name && checkout_stage == CHECKOUT_ALL)
		return 0;

	if (!state.quiet) {
		fprintf(stderr, "git checkout-index: %s ", name);
		if (!has_same_name)
			fprintf(stderr, "is not in the cache");
		else if (!is_file)
			fprintf(stderr, "is a sparse directory");
		else if (is_skipped)
			fprintf(stderr, "has skip-worktree enabled; "
					"use '--ignore-skip-worktree-bits' to checkout");
		else if (checkout_stage)
			fprintf(stderr, "does not exist at stage %d",
				checkout_stage);
		else
			fprintf(stderr, "is unmerged");
		fputc('\n', stderr);
	}
	return -1;
}

static int checkout_all(const char *prefix, int prefix_length)
{
	int i, errs = 0;
	struct cache_entry *last_ce = NULL;

	for (i = 0; i < the_repository->index->cache_nr ; i++) {
		struct cache_entry *ce = the_repository->index->cache[i];

		if (S_ISSPARSEDIR(ce->ce_mode)) {
			if (!ce_skip_worktree(ce))
				BUG("sparse directory '%s' does not have skip-worktree set", ce->name);

			/*
			 * If the current entry is a sparse directory and skip-worktree
			 * entries are being checked out, expand the index and continue
			 * the loop on the current index position (now pointing to the
			 * first entry inside the expanded sparse directory).
			 */
			if (ignore_skip_worktree) {
				ensure_full_index(the_repository->index);
				ce = the_repository->index->cache[i];
			}
		}

		if (!ignore_skip_worktree && ce_skip_worktree(ce))
			continue;
		if (ce_stage(ce) != checkout_stage
		    && (CHECKOUT_ALL != checkout_stage || !ce_stage(ce)))
			continue;
		if (prefix && *prefix &&
		    (ce_namelen(ce) <= prefix_length ||
		     memcmp(prefix, ce->name, prefix_length)))
			continue;
		if (last_ce && to_tempfile) {
			if (ce_namelen(last_ce) != ce_namelen(ce)
			    || memcmp(last_ce->name, ce->name, ce_namelen(ce)))
				write_tempfile_record(last_ce->name, prefix);
		}
		if (checkout_entry(ce, &state,
				   to_tempfile ? topath[ce_stage(ce)] : NULL,
				   NULL) < 0)
			errs++;
		last_ce = ce;
	}
	if (last_ce && to_tempfile)
		write_tempfile_record(last_ce->name, prefix);
	return !!errs;
}

static const char * const builtin_checkout_index_usage[] = {
	N_("git checkout-index [<options>] [--] [<file>...]"),
	NULL
};

static int option_parse_stage(const struct option *opt,
			      const char *arg, int unset)
{
	int *stage = opt->value;

	BUG_ON_OPT_NEG(unset);

	if (!strcmp(arg, "all")) {
		*stage = CHECKOUT_ALL;
	} else {
		int ch = arg[0];
		if ('1' <= ch && ch <= '3')
			*stage = arg[0] - '0';
		else
			die(_("stage should be between 1 and 3 or all"));
	}
	return 0;
}

int cmd_checkout_index(int argc,
		       const char **argv,
		       const char *prefix,
		       struct repository *repo UNUSED)
{
	int i;
	struct lock_file lock_file = LOCK_INIT;
	int all = 0;
	int read_from_stdin = 0;
	int prefix_length;
	int force = 0, quiet = 0, not_new = 0;
	int index_opt = 0;
	int err = 0;
	int pc_workers, pc_threshold;
	struct option builtin_checkout_index_options[] = {
		OPT_BOOL('a', "all", &all,
			N_("check out all files in the index")),
		OPT_BOOL(0, "ignore-skip-worktree-bits", &ignore_skip_worktree,
			N_("do not skip files with skip-worktree set")),
		OPT__FORCE(&force, N_("force overwrite of existing files"), 0),
		OPT__QUIET(&quiet,
			N_("no warning for existing files and files not in index")),
		OPT_BOOL('n', "no-create", &not_new,
			N_("don't checkout new files")),
		OPT_BOOL('u', "index", &index_opt,
			 N_("update stat information in the index file")),
		OPT_BOOL('z', NULL, &nul_term_line,
			N_("paths are separated with NUL character")),
		OPT_BOOL(0, "stdin", &read_from_stdin,
			N_("read list of paths from the standard input")),
		OPT_BOOL(0, "temp", &to_tempfile,
			N_("write the content to temporary files")),
		OPT_STRING(0, "prefix", &state.base_dir, N_("string"),
			N_("when creating files, prepend <string>")),
		OPT_CALLBACK_F(0, "stage", &checkout_stage, "(1|2|3|all)",
			N_("copy out the files from named stage"),
			PARSE_OPT_NONEG, option_parse_stage),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_checkout_index_usage,
				   builtin_checkout_index_options);
	git_config(git_default_config, NULL);
	prefix_length = prefix ? strlen(prefix) : 0;

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	if (repo_read_index(the_repository) < 0) {
		die("invalid cache");
	}

	argc = parse_options(argc, argv, prefix, builtin_checkout_index_options,
			builtin_checkout_index_usage, 0);
	state.istate = the_repository->index;
	state.force = force;
	state.quiet = quiet;
	state.not_new = not_new;

	if (!state.base_dir)
		state.base_dir = "";
	state.base_dir_len = strlen(state.base_dir);

	if (to_tempfile < 0)
		to_tempfile = (checkout_stage == CHECKOUT_ALL);
	if (!to_tempfile && checkout_stage == CHECKOUT_ALL)
		die(_("options '%s' and '%s' cannot be used together"),
		    "--stage=all", "--no-temp");

	/*
	 * when --prefix is specified we do not want to update cache.
	 */
	if (index_opt && !state.base_dir_len && !to_tempfile) {
		state.refresh_cache = 1;
		state.istate = the_repository->index;
		repo_hold_locked_index(the_repository, &lock_file,
				       LOCK_DIE_ON_ERROR);
	}

	get_parallel_checkout_configs(&pc_workers, &pc_threshold);
	if (pc_workers > 1)
		init_parallel_checkout();

	/* Check out named files first */
	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];
		char *p;

		if (all)
			die("git checkout-index: don't mix '--all' and explicit filenames");
		if (read_from_stdin)
			die("git checkout-index: don't mix '--stdin' and explicit filenames");
		p = prefix_path(prefix, prefix_length, arg);
		err |= checkout_file(p, prefix);
		free(p);
	}

	if (read_from_stdin) {
		struct strbuf buf = STRBUF_INIT;
		struct strbuf unquoted = STRBUF_INIT;
		strbuf_getline_fn getline_fn;

		if (all)
			die("git checkout-index: don't mix '--all' and '--stdin'");

		getline_fn = nul_term_line ? strbuf_getline_nul : strbuf_getline_lf;
		while (getline_fn(&buf, stdin) != EOF) {
			char *p;
			if (!nul_term_line && buf.buf[0] == '"') {
				strbuf_reset(&unquoted);
				if (unquote_c_style(&unquoted, buf.buf, NULL))
					die("line is badly quoted");
				strbuf_swap(&buf, &unquoted);
			}
			p = prefix_path(prefix, prefix_length, buf.buf);
			err |= checkout_file(p, prefix);
			free(p);
		}
		strbuf_release(&unquoted);
		strbuf_release(&buf);
	}

	if (all)
		err |= checkout_all(prefix, prefix_length);

	if (pc_workers > 1)
		err |= run_parallel_checkout(&state, pc_workers, pc_threshold,
					     NULL, NULL);

	if (err)
		return 1;

	if (is_lock_file_locked(&lock_file) &&
	    write_locked_index(the_repository->index, &lock_file, COMMIT_LOCK))
		die("Unable to write new index file");
	return 0;
}
