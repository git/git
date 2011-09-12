/*
 * "git reset" builtin command
 *
 * Copyright (c) 2007 Carlos Rica
 *
 * Based on git-reset.sh, which is
 *
 * Copyright (c) 2005, 2006 Linus Torvalds and Junio C Hamano
 */
#include "builtin.h"
#include "tag.h"
#include "object.h"
#include "commit.h"
#include "run-command.h"
#include "refs.h"
#include "diff.h"
#include "diffcore.h"
#include "tree.h"
#include "branch.h"
#include "parse-options.h"
#include "unpack-trees.h"
#include "cache-tree.h"

static const char * const git_reset_usage[] = {
	"git reset [--mixed | --soft | --hard | --merge | --keep] [-q] [<commit>]",
	"git reset [-q] <commit> [--] <paths>...",
	"git reset --patch [<commit>] [--] [<paths>...]",
	NULL
};

enum reset_type { MIXED, SOFT, HARD, MERGE, KEEP, NONE };
static const char *reset_type_names[] = {
	N_("mixed"), N_("soft"), N_("hard"), N_("merge"), N_("keep"), NULL
};

static inline int is_merge(void)
{
	return !access(git_path("MERGE_HEAD"), F_OK);
}

static int reset_index_file(const unsigned char *sha1, int reset_type, int quiet)
{
	int nr = 1;
	int newfd;
	struct tree_desc desc[2];
	struct unpack_trees_options opts;
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = 1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	opts.fn = oneway_merge;
	opts.merge = 1;
	if (!quiet)
		opts.verbose_update = 1;
	switch (reset_type) {
	case KEEP:
	case MERGE:
		opts.update = 1;
		break;
	case HARD:
		opts.update = 1;
		/* fallthrough */
	default:
		opts.reset = 1;
	}

	newfd = hold_locked_index(lock, 1);

	read_cache_unmerged();

	if (reset_type == KEEP) {
		unsigned char head_sha1[20];
		if (get_sha1("HEAD", head_sha1))
			return error(_("You do not have a valid HEAD."));
		if (!fill_tree_descriptor(desc, head_sha1))
			return error(_("Failed to find tree of HEAD."));
		nr++;
		opts.fn = twoway_merge;
	}

	if (!fill_tree_descriptor(desc + nr - 1, sha1))
		return error(_("Failed to find tree of %s."), sha1_to_hex(sha1));
	if (unpack_trees(nr, desc, &opts))
		return -1;
	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_locked_index(lock))
		return error(_("Could not write new index file."));

	return 0;
}

static void print_new_head_line(struct commit *commit)
{
	const char *hex, *body;

	hex = find_unique_abbrev(commit->object.sha1, DEFAULT_ABBREV);
	printf(_("HEAD is now at %s"), hex);
	body = strstr(commit->buffer, "\n\n");
	if (body) {
		const char *eol;
		size_t len;
		body += 2;
		eol = strchr(body, '\n');
		len = eol ? eol - body : strlen(body);
		printf(" %.*s\n", (int) len, body);
	}
	else
		printf("\n");
}

static int update_index_refresh(int fd, struct lock_file *index_lock, int flags)
{
	int result;

	if (!index_lock) {
		index_lock = xcalloc(1, sizeof(struct lock_file));
		fd = hold_locked_index(index_lock, 1);
	}

	if (read_cache() < 0)
		return error(_("Could not read index"));

	result = refresh_index(&the_index, (flags), NULL, NULL,
			       _("Unstaged changes after reset:")) ? 1 : 0;
	if (write_cache(fd, active_cache, active_nr) ||
			commit_locked_index(index_lock))
		return error ("Could not refresh index");
	return result;
}

static void update_index_from_diff(struct diff_queue_struct *q,
		struct diff_options *opt, void *data)
{
	int i;
	int *discard_flag = data;

	/* do_diff_cache() mangled the index */
	discard_cache();
	*discard_flag = 1;
	read_cache();

	for (i = 0; i < q->nr; i++) {
		struct diff_filespec *one = q->queue[i]->one;
		if (one->mode && !is_null_sha1(one->sha1)) {
			struct cache_entry *ce;
			ce = make_cache_entry(one->mode, one->sha1, one->path,
				0, 0);
			if (!ce)
				die(_("make_cache_entry failed for path '%s'"),
				    one->path);
			add_cache_entry(ce, ADD_CACHE_OK_TO_ADD |
				ADD_CACHE_OK_TO_REPLACE);
		} else
			remove_file_from_cache(one->path);
	}
}

static int interactive_reset(const char *revision, const char **argv,
			     const char *prefix)
{
	const char **pathspec = NULL;

	if (*argv)
		pathspec = get_pathspec(prefix, argv);

	return run_add_interactive(revision, "--patch=reset", pathspec);
}

static int read_from_tree(const char *prefix, const char **argv,
		unsigned char *tree_sha1, int refresh_flags)
{
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));
	int index_fd, index_was_discarded = 0;
	struct diff_options opt;

	memset(&opt, 0, sizeof(opt));
	diff_tree_setup_paths(get_pathspec(prefix, (const char **)argv), &opt);
	opt.output_format = DIFF_FORMAT_CALLBACK;
	opt.format_callback = update_index_from_diff;
	opt.format_callback_data = &index_was_discarded;

	index_fd = hold_locked_index(lock, 1);
	index_was_discarded = 0;
	read_cache();
	if (do_diff_cache(tree_sha1, &opt))
		return 1;
	diffcore_std(&opt);
	diff_flush(&opt);
	diff_tree_release_paths(&opt);

	if (!index_was_discarded)
		/* The index is still clobbered from do_diff_cache() */
		discard_cache();
	return update_index_refresh(index_fd, lock, refresh_flags);
}

static void set_reflog_message(struct strbuf *sb, const char *action,
			       const char *rev)
{
	const char *rla = getenv("GIT_REFLOG_ACTION");

	strbuf_reset(sb);
	if (rla)
		strbuf_addf(sb, "%s: %s", rla, action);
	else if (rev)
		strbuf_addf(sb, "reset: moving to %s", rev);
	else
		strbuf_addf(sb, "reset: %s", action);
}

static void die_if_unmerged_cache(int reset_type)
{
	if (is_merge() || read_cache() < 0 || unmerged_cache())
		die(_("Cannot do a %s reset in the middle of a merge."),
		    _(reset_type_names[reset_type]));

}

int cmd_reset(int argc, const char **argv, const char *prefix)
{
	int i = 0, reset_type = NONE, update_ref_status = 0, quiet = 0;
	int patch_mode = 0;
	const char *rev = "HEAD";
	unsigned char sha1[20], *orig = NULL, sha1_orig[20],
				*old_orig = NULL, sha1_old_orig[20];
	struct commit *commit;
	struct strbuf msg = STRBUF_INIT;
	const struct option options[] = {
		OPT__QUIET(&quiet, "be quiet, only report errors"),
		OPT_SET_INT(0, "mixed", &reset_type,
						"reset HEAD and index", MIXED),
		OPT_SET_INT(0, "soft", &reset_type, "reset only HEAD", SOFT),
		OPT_SET_INT(0, "hard", &reset_type,
				"reset HEAD, index and working tree", HARD),
		OPT_SET_INT(0, "merge", &reset_type,
				"reset HEAD, index and working tree", MERGE),
		OPT_SET_INT(0, "keep", &reset_type,
				"reset HEAD but keep local changes", KEEP),
		OPT_BOOLEAN('p', "patch", &patch_mode, "select hunks interactively"),
		OPT_END()
	};

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options, git_reset_usage,
						PARSE_OPT_KEEP_DASHDASH);

	/*
	 * Possible arguments are:
	 *
	 * git reset [-opts] <rev> <paths>...
	 * git reset [-opts] <rev> -- <paths>...
	 * git reset [-opts] -- <paths>...
	 * git reset [-opts] <paths>...
	 *
	 * At this point, argv[i] points immediately after [-opts].
	 */

	if (i < argc) {
		if (!strcmp(argv[i], "--")) {
			i++; /* reset to HEAD, possibly with paths */
		} else if (i + 1 < argc && !strcmp(argv[i+1], "--")) {
			rev = argv[i];
			i += 2;
		}
		/*
		 * Otherwise, argv[i] could be either <rev> or <paths> and
		 * has to be unambiguous.
		 */
		else if (!get_sha1(argv[i], sha1)) {
			/*
			 * Ok, argv[i] looks like a rev; it should not
			 * be a filename.
			 */
			verify_non_filename(prefix, argv[i]);
			rev = argv[i++];
		} else {
			/* Otherwise we treat this as a filename */
			verify_filename(prefix, argv[i]);
		}
	}

	if (get_sha1(rev, sha1))
		die(_("Failed to resolve '%s' as a valid ref."), rev);

	commit = lookup_commit_reference(sha1);
	if (!commit)
		die(_("Could not parse object '%s'."), rev);
	hashcpy(sha1, commit->object.sha1);

	if (patch_mode) {
		if (reset_type != NONE)
			die(_("--patch is incompatible with --{hard,mixed,soft}"));
		return interactive_reset(rev, argv + i, prefix);
	}

	/* git reset tree [--] paths... can be used to
	 * load chosen paths from the tree into the index without
	 * affecting the working tree nor HEAD. */
	if (i < argc) {
		if (reset_type == MIXED)
			warning(_("--mixed with paths is deprecated; use 'git reset -- <paths>' instead."));
		else if (reset_type != NONE)
			die(_("Cannot do %s reset with paths."),
					_(reset_type_names[reset_type]));
		return read_from_tree(prefix, argv + i, sha1,
				quiet ? REFRESH_QUIET : REFRESH_IN_PORCELAIN);
	}
	if (reset_type == NONE)
		reset_type = MIXED; /* by default */

	if (reset_type != SOFT && reset_type != MIXED)
		setup_work_tree();

	if (reset_type == MIXED && is_bare_repository())
		die(_("%s reset is not allowed in a bare repository"),
		    _(reset_type_names[reset_type]));

	/* Soft reset does not touch the index file nor the working tree
	 * at all, but requires them in a good order.  Other resets reset
	 * the index file to the tree object we are switching to. */
	if (reset_type == SOFT)
		die_if_unmerged_cache(reset_type);
	else {
		int err;
		if (reset_type == KEEP)
			die_if_unmerged_cache(reset_type);
		err = reset_index_file(sha1, reset_type, quiet);
		if (reset_type == KEEP)
			err = err || reset_index_file(sha1, MIXED, quiet);
		if (err)
			die(_("Could not reset index file to revision '%s'."), rev);
	}

	/* Any resets update HEAD to the head being switched to,
	 * saving the previous head in ORIG_HEAD before. */
	if (!get_sha1("ORIG_HEAD", sha1_old_orig))
		old_orig = sha1_old_orig;
	if (!get_sha1("HEAD", sha1_orig)) {
		orig = sha1_orig;
		set_reflog_message(&msg, "updating ORIG_HEAD", NULL);
		update_ref(msg.buf, "ORIG_HEAD", orig, old_orig, 0, MSG_ON_ERR);
	}
	else if (old_orig)
		delete_ref("ORIG_HEAD", old_orig, 0);
	set_reflog_message(&msg, "updating HEAD", rev);
	update_ref_status = update_ref(msg.buf, "HEAD", sha1, orig, 0, MSG_ON_ERR);

	switch (reset_type) {
	case HARD:
		if (!update_ref_status && !quiet)
			print_new_head_line(commit);
		break;
	case SOFT: /* Nothing else to do. */
		break;
	case MIXED: /* Report what has not been updated. */
		update_index_refresh(0, NULL,
				quiet ? REFRESH_QUIET : REFRESH_IN_PORCELAIN);
		break;
	}

	remove_branch_state();

	strbuf_release(&msg);

	return update_ref_status;
}
