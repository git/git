/*
 * "git reset" builtin command
 *
 * Copyright (c) 2007 Carlos Rica
 *
 * Based on git-reset.sh, which is
 *
 * Copyright (c) 2005, 2006 Linus Torvalds and Junio C Hamano
 */
#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "advice.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "lockfile.h"
#include "object.h"
#include "pretty.h"
#include "refs.h"
#include "diff.h"
#include "diffcore.h"
#include "tree.h"
#include "branch.h"
#include "object-name.h"
#include "parse-options.h"
#include "path.h"
#include "repository.h"
#include "unpack-trees.h"
#include "cache-tree.h"
#include "setup.h"
#include "sparse-index.h"
#include "submodule.h"
#include "trace.h"
#include "trace2.h"
#include "dir.h"
#include "add-interactive.h"
#include "../safety-protocol.h"

#define REFRESH_INDEX_DELAY_WARNING_IN_MS (2 * 1000)

static const char * const git_reset_usage[] = {
	N_("git reset [--mixed | --soft | --hard | --merge | --keep] [-q] [<commit>]"),
	N_("git reset [-q] [<tree-ish>] [--] <pathspec>..."),
	N_("git reset [-q] [--pathspec-from-file [--pathspec-file-nul]] [<tree-ish>]"),
	N_("git reset --patch [<tree-ish>] [--] [<pathspec>...]"),
	NULL
};

enum reset_type { MIXED, SOFT, HARD, MERGE, KEEP, NONE };
static const char *reset_type_names[] = {
	N_("mixed"), N_("soft"), N_("hard"), N_("merge"), N_("keep"), NULL
};

static inline int is_merge(void)
{
	return !access(git_path_merge_head(the_repository), F_OK);
}

struct reset_safety {
	unsigned int has_nested_git:1;
	unsigned int has_ignored_files:1;
	unsigned int has_build_artifacts:1;
	unsigned int has_untracked_deps:1;
	unsigned long total_size;
	int file_count;
	struct string_list critical_paths;
};

static struct reset_safety safety = {0};

static const char *critical_patterns[] = {
	/* Build artifacts and dependencies */
	"node_modules/", "vendor/", "build/", "dist/",
	"target/", "bin/", "obj/",
	/* Config and env files */
	".env", "config/", "settings/",
	/* IDE files */
	".idea/", ".vscode/",
	/* Lock files */
	"package-lock.json", "yarn.lock", "Gemfile.lock",
	NULL
};

static void check_path_safety(const char *path, struct reset_safety *safety)
{
	struct stat st;
	const char **pattern;

	if (is_nonbare_repository_dir(path))
		safety->has_nested_git = 1;

	/* Check if path matches any critical patterns */
	for (pattern = critical_patterns; *pattern; pattern++) {
		if (strstr(path, *pattern)) {
			safety->has_build_artifacts = 1;
			break;
		}
	}

	/* Gather size information */
	if (lstat(path, &st) == 0) {
		safety->total_size += st.st_size;
		safety->file_count++;
	}
}

static void print_reset_warning(struct reset_safety *safety, int force_level)
{
	struct strbuf msg = STRBUF_INIT;
	int is_dangerous = 0;

	strbuf_addstr(&msg, _("WARNING: You are about to perform a destructive reset operation:\n"));

	if (safety->has_nested_git) {
		strbuf_addstr(&msg, _("  ! DANGER: Will affect nested Git repositories!\n"));
		is_dangerous = 1;
	}

	if (safety->has_build_artifacts) {
		strbuf_addstr(&msg, _("  ! Will affect build artifacts and dependencies\n"));
		is_dangerous = 1;
	}

	if (safety->has_untracked_deps) {
		strbuf_addstr(&msg, _("  ! Will affect package dependencies\n"));
		is_dangerous = 1;
	}

	strbuf_addf(&msg, _("  * Total: %d files, %lu bytes will be affected\n"), 
		    safety->file_count, safety->total_size);

	if (is_dangerous && force_level < 2) {
		strbuf_addstr(&msg, _("\nThis operation requires -ff (double force) due to dangerous content\n"));
		die("%s", msg.buf);
	}

	fprintf(stderr, "%s\n", msg.buf);
	strbuf_release(&msg);

	if (is_dangerous) {
		if (!isatty(0)) {
			die(_("Refusing to reset dangerous content in non-interactive mode.\nUse -ff to override or run in terminal"));
		}
		if (!ask(_("Are you ABSOLUTELY sure you want to proceed? Type 'yes' to confirm: "), 0)) {
			die(_("Operation aborted by user"));
		}
	}
}

static int reset_index(const char *ref, const struct object_id *oid, int reset_type, int quiet, int force_level)
{
	int i, nr = 0;
	struct tree_desc desc[2];
	struct tree *tree;
	struct unpack_trees_options opts;
	int ret = -1;

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = 1;
	opts.src_index = the_repository->index;
	opts.dst_index = the_repository->index;
	opts.fn = oneway_merge;
	opts.merge = 1;
	init_checkout_metadata(&opts.meta, ref, oid, NULL);
	if (!quiet)
		opts.verbose_update = 1;

	switch (reset_type) {
	case KEEP:
	case MERGE:
		opts.update = 1;
		opts.preserve_ignored = 0; /* FIXME: !overwrite_ignore */
		break;
	case HARD:
		opts.update = 1;
		if (force_level >= 2) {
			opts.reset = UNPACK_RESET_OVERWRITE_UNTRACKED;
		} else {
			opts.reset = UNPACK_RESET_PROTECT_UNTRACKED;
		}
		opts.skip_cache_tree_update = 1;
		break;
	case MIXED:
		opts.reset = UNPACK_RESET_PROTECT_UNTRACKED;
		opts.skip_cache_tree_update = 1;
		/* but opts.update=0, so working tree not updated */
		break;
	default:
		BUG("invalid reset_type passed to reset_index");
	}

	repo_read_index_unmerged(the_repository);

	if (reset_type == KEEP) {
		struct object_id head_oid;
		if (repo_get_oid(the_repository, "HEAD", &head_oid))
			return error(_("You do not have a valid HEAD."));
		if (!fill_tree_descriptor(the_repository, desc + nr, &head_oid))
			return error(_("Failed to find tree of HEAD."));
		nr++;
		opts.fn = twoway_merge;
	}

	if (!fill_tree_descriptor(the_repository, desc + nr, oid)) {
		error(_("Failed to find tree of %s."), oid_to_hex(oid));
		goto out;
	}
	nr++;

	if (unpack_trees(nr, desc, &opts))
		goto out;

	if (reset_type == MIXED || reset_type == HARD) {
		tree = parse_tree_indirect(oid);
		if (!tree) {
			error(_("unable to read tree (%s)"), oid_to_hex(oid));
			goto out;
		}
		prime_cache_tree(the_repository, the_repository->index, tree);
	}

	if (reset_type == HARD) {
		struct diff_options diff_opts;
		struct pathspec pathspec;
		memset(&pathspec, 0, sizeof(pathspec));
		diff_setup(&diff_opts);
		diff_opts.flags.recursive = 1;
		diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
		diff_setup_done(&diff_opts);
		run_diff_files(&diff_opts, 0);
		diffcore_std(&diff_opts);

		/* Check safety of each path that would be affected */
		for (i = 0; i < diff_queued_diff.nr; i++) {
			struct diff_filepair *p = diff_queued_diff.queue[i];
			check_path_safety(p->one->path, &safety);
		}

		print_reset_warning(&safety, force_level);
	}

	ret = 0;

out:
	for (i = 0; i < nr; i++)
		free((void *)desc[i].buffer);
	return ret;
}

static void print_new_head_line(struct commit *commit)
{
	struct strbuf buf = STRBUF_INIT;

	printf(_("HEAD is now at %s"),
		repo_find_unique_abbrev(the_repository, &commit->object.oid, DEFAULT_ABBREV));

	pp_commit_easy(CMIT_FMT_ONELINE, commit, &buf);
	if (buf.len > 0)
		printf(" %s", buf.buf);
	putchar('\n');
	strbuf_release(&buf);
}

static void update_index_from_diff(struct diff_queue_struct *q,
				   struct diff_options *opt UNUSED,
				   void *data)
{
	int i;
	int intent_to_add = *(int *)data;

	for (i = 0; i < q->nr; i++) {
		int pos;
		struct diff_filespec *one = q->queue[i]->one;
		int is_in_reset_tree = one->mode && !is_null_oid(&one->oid);
		struct cache_entry *ce;

		if (!is_in_reset_tree && !intent_to_add) {
			remove_file_from_index(the_repository->index, one->path);
			continue;
		}

		ce = make_cache_entry(the_repository->index, one->mode, &one->oid, one->path,
				      0, 0);

		/*
		 * If the file 1) corresponds to an existing index entry with
		 * skip-worktree set, or 2) does not exist in the index but is
		 * outside the sparse checkout definition, add a skip-worktree bit
		 * to the new index entry. Note that a sparse index will be expanded
		 * if this entry is outside the sparse cone - this is necessary
		 * to properly construct the reset sparse directory.
		 */
		pos = index_name_pos(the_repository->index, one->path, strlen(one->path));
		if ((pos >= 0 && ce_skip_worktree(the_repository->index->cache[pos])) ||
		    (pos < 0 && !path_in_sparse_checkout(one->path, the_repository->index)))
			ce->ce_flags |= CE_SKIP_WORKTREE;

		if (!ce)
			die(_("make_cache_entry failed for path '%s'"),
			    one->path);
		if (!is_in_reset_tree) {
			ce->ce_flags |= CE_INTENT_TO_ADD;
			set_object_name_for_intent_to_add_entry(ce);
		}
		add_index_entry(the_repository->index, ce,
				ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE);
	}
}

static int read_from_tree(const struct pathspec *pathspec,
			  struct object_id *tree_oid,
			  int intent_to_add)
{
	struct diff_options opt;

	memset(&opt, 0, sizeof(opt));
	copy_pathspec(&opt.pathspec, pathspec);
	opt.output_format = DIFF_FORMAT_CALLBACK;
	opt.format_callback = update_index_from_diff;
	opt.format_callback_data = &intent_to_add;
	opt.flags.override_submodule_config = 1;
	opt.flags.recursive = 1;
	opt.repo = the_repository;
	opt.change = diff_change;
	opt.add_remove = diff_addremove;

	if (pathspec->nr && pathspec_needs_expanded_index(the_repository->index, pathspec))
		ensure_full_index(the_repository->index);

	if (do_diff_cache(tree_oid, &opt))
		return 1;
	diffcore_std(&opt);
	diff_flush(&opt);

	return 0;
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
	if (is_merge() || unmerged_index(the_repository->index))
		die(_("Cannot do a %s reset in the middle of a merge."),
		    _(reset_type_names[reset_type]));

}

static void parse_args(struct pathspec *pathspec,
		       const char **argv, const char *prefix,
		       int patch_mode,
		       const char **rev_ret)
{
	const char *rev = "HEAD";
	struct object_id unused;
	/*
	 * Possible arguments are:
	 *
	 * git reset [-opts] [<rev>]
	 * git reset [-opts] <tree> [<paths>...]
	 * git reset [-opts] <tree> -- [<paths>...]
	 * git reset [-opts] -- [<paths>...]
	 * git reset [-opts] <paths>...
	 *
	 * At this point, argv points immediately after [-opts].
	 */

	if (argv[0]) {
		if (!strcmp(argv[0], "--")) {
			argv++; /* reset to HEAD, possibly with paths */
		} else if (argv[1] && !strcmp(argv[1], "--")) {
			rev = argv[0];
			argv += 2;
		}
		/*
		 * Otherwise, argv[0] could be either <rev> or <paths> and
		 * has to be unambiguous. If there is a single argument, it
		 * can not be a tree
		 */
		else if ((!argv[1] && !repo_get_oid_committish(the_repository, argv[0], &unused)) ||
			 (argv[1] && !repo_get_oid_treeish(the_repository, argv[0], &unused))) {
			/*
			 * Ok, argv[0] looks like a commit/tree; it should not
			 * be a filename.
			 */
			verify_non_filename(prefix, argv[0]);
			rev = *argv++;
		} else {
			/* Otherwise we treat this as a filename */
			verify_filename(prefix, argv[0], 1);
		}
	}

	/* treat '@' as a shortcut for 'HEAD' */
	*rev_ret = !strcmp("@", rev) ? "HEAD" : rev;

	parse_pathspec(pathspec, 0,
		       PATHSPEC_PREFER_FULL |
		       (patch_mode ? PATHSPEC_PREFIX_ORIGIN : 0),
		       prefix, argv);
}

static int reset_refs(const char *rev, const struct object_id *oid)
{
	int update_ref_status;
	struct strbuf msg = STRBUF_INIT;
	struct object_id *orig = NULL, oid_orig,
		*old_orig = NULL, oid_old_orig;

	if (!repo_get_oid(the_repository, "ORIG_HEAD", &oid_old_orig))
		old_orig = &oid_old_orig;
	if (!repo_get_oid(the_repository, "HEAD", &oid_orig)) {
		orig = &oid_orig;
		set_reflog_message(&msg, "updating ORIG_HEAD", NULL);
		refs_update_ref(get_main_ref_store(the_repository), msg.buf,
				"ORIG_HEAD", orig, old_orig, 0,
				UPDATE_REFS_MSG_ON_ERR);
	} else if (old_orig)
		refs_delete_ref(get_main_ref_store(the_repository), NULL,
				"ORIG_HEAD", old_orig, 0);
	set_reflog_message(&msg, "updating HEAD", rev);
	update_ref_status = refs_update_ref(get_main_ref_store(the_repository),
					    msg.buf, "HEAD", oid, orig, 0,
					    UPDATE_REFS_MSG_ON_ERR);
	strbuf_release(&msg);
	return update_ref_status;
}

static int git_reset_config(const char *var, const char *value,
			    const struct config_context *ctx, void *cb)
{
	if (!strcmp(var, "submodule.recurse"))
		return git_default_submodule_config(var, value, cb);

	return git_default_config(var, value, ctx, cb);
}

static struct safety_state reset_safety_state;

int cmd_reset(int argc, const char **argv, const char *prefix)
{
    int reset_type = NONE, update_ref_status = 0, quiet = 0;
    int no_refresh = 0;
    int patch_mode = 0, pathspec_file_nul = 0, unborn;
    const char *rev;
    char *pathspec_from_file = NULL;
    struct object_id oid;
    struct pathspec pathspec;
    int intent_to_add = 0;
    int force = 0;
    int double_force = 0;
    int has_safety_concerns = 0;
    
    /* Initialize safety state */
    safety_init(&reset_safety_state, SAFETY_OP_RESET);
    
    struct option builtin_reset_options[] = {
        OPT__QUIET(&quiet, N_("be quiet, only report errors")),
        OPT_SET_INT('p', "patch", &reset_type,
            N_("select hunks interactively"), RESET_HEAD),
        OPT_SET_INT(0, "patch-with-stat", &reset_type,
            N_("select hunks interactively"), RESET_HEAD),
        OPT_SET_INT_F(0, "soft", &reset_type,
            N_("reset only HEAD"), RESET_SOFT,
            PARSE_OPT_NONEG),
        OPT_SET_INT_F(0, "mixed", &reset_type,
            N_("reset HEAD and index"), RESET_MIXED,
            PARSE_OPT_NONEG),
        OPT_SET_INT_F(0, "hard", &reset_type,
            N_("reset HEAD, index and working tree"), RESET_HARD,
            PARSE_OPT_NONEG),
        OPT_SET_INT_F(0, "merge", &reset_type,
            N_("reset HEAD, index and working tree"), RESET_MERGE,
            PARSE_OPT_NONEG),
        OPT_SET_INT_F(0, "keep", &reset_type,
            N_("reset HEAD but keep local changes"), RESET_KEEP,
            PARSE_OPT_NONEG),
        OPT_BOOL('f', "force", &force, N_("force reset")),
        OPT_BOOL('F', "force-force", &double_force, N_("override all safety checks")),
        OPT_PATHSPEC_FROM_FILE(&pathspec_from_file),
        OPT_PATHSPEC_FILE_NUL(&pathspec_file_nul),
        OPT_END()
    };
    
    git_config(git_reset_config, NULL);

    argc = parse_options(argc, argv, prefix, builtin_reset_options, git_reset_usage,
						PARSE_OPT_KEEP_DASHDASH);
    parse_args(&pathspec, argv, prefix, patch_mode, &rev);

    if (pathspec_from_file) {
        if (patch_mode)
            die(_("options '%s' and '%s' cannot be used together"), "--pathspec-from-file", "--patch");

        if (pathspec.nr)
            die(_("'%s' and pathspec arguments cannot be used together"), "--pathspec-from-file");

        parse_pathspec_file(&pathspec, 0,
                            PATHSPEC_PREFER_FULL,
                            prefix, pathspec_from_file, pathspec_file_nul);
    } else if (pathspec_file_nul) {
        die(_("the option '%s' requires '%s'"), "--pathspec-file-nul", "--pathspec-from-file");
    }

    unborn = !strcmp(rev, "HEAD") && repo_get_oid(the_repository, "HEAD",
                                                  &oid);
    if (unborn) {
        /* reset on unborn branch: treat as reset to empty tree */
        oidcpy(&oid, the_hash_algo->empty_tree);
    } else if (!pathspec.nr && !patch_mode) {
        struct commit *commit;
        if (repo_get_oid_committish(the_repository, rev, &oid))
            die(_("Failed to resolve '%s' as a valid revision."), rev);
        commit = lookup_commit_reference(the_repository, &oid);
        if (!commit)
            die(_("Could not parse object '%s'."), rev);
        oidcpy(&oid, &commit->object.oid);
    } else {
        struct tree *tree;
        if (repo_get_oid_treeish(the_repository, rev, &oid))
            die(_("Failed to resolve '%s' as a valid tree."), rev);
        tree = parse_tree_indirect(&oid);
        if (!tree)
            die(_("Could not parse object '%s'."), rev);
        oidcpy(&oid, &tree->object.oid);
    }

    if (patch_mode) {
        if (reset_type != NONE)
            die(_("options '%s' and '%s' cannot be used together"), "--patch", "--{hard,mixed,soft}");
        trace2_cmd_mode("patch-interactive");
        update_ref_status = !!run_add_p(the_repository, ADD_P_RESET, rev,
                                        &pathspec);
        goto cleanup;
    }

    /* Set force level based on flags */
    if (double_force)
        reset_safety_state.force_level = SAFETY_FORCE_DOUBLE;
    else if (force)
        reset_safety_state.force_level = SAFETY_FORCE_SINGLE;
        
    /* For hard reset, check working directory for safety concerns */
    if (reset_type == RESET_HARD || reset_type == RESET_MERGE) {
        struct dir_struct dir = DIR_INIT;
        struct pathspec pathspec = { 0 };
        
        dir.flags |= DIR_SHOW_IGNORED;
        setup_standard_excludes(&dir);
        
        fill_directory(&dir, &pathspec);
        
        /* Check each path for safety concerns */
        for (int i = 0; i < dir.nr; i++) {
            const char *path = dir.entries[i]->name;
            if (safety_check_path(&reset_safety_state, path)) {
                has_safety_concerns = 1;
            }
        }
        
        clear_directory(&dir);
        
        /* Get confirmation if needed */
        if (has_safety_concerns) {
            const char *op_desc = "perform a hard reset";
            if (!safety_confirm_operation(&reset_safety_state, op_desc)) {
                return 1;
            }
        }
    }
    
    /* git reset tree [--] paths... can be used to
     * load chosen paths from the tree into the index without
     * affecting the working tree nor HEAD. */
    if (pathspec.nr) {
        if (reset_type == MIXED)
            warning(_("--mixed with paths is deprecated; use 'git reset -- <paths>' instead."));
        else if (reset_type != NONE)
            die(_("Cannot do %s reset with paths."),
                    _(reset_type_names[reset_type]));
    }
    if (reset_type == NONE)
        reset_type = MIXED; /* by default */

    if (pathspec.nr)
        trace2_cmd_mode("path");
    else
        trace2_cmd_mode(reset_type_names[reset_type]);

    if (reset_type != SOFT && (reset_type != MIXED || repo_get_work_tree(the_repository)))
        setup_work_tree();

    if (reset_type == MIXED && is_bare_repository())
        die(_("%s reset is not allowed in a bare repository"),
            _(reset_type_names[reset_type]));

    if (intent_to_add && reset_type != MIXED)
        die(_("the option '%s' requires '%s'"), "-N", "--mixed");

    prepare_repo_settings(the_repository);
    the_repository->settings.command_requires_full_index = 0;

    if (repo_read_index(the_repository) < 0)
        die(_("index file corrupt"));

    /* Soft reset does not touch the index file nor the working tree
     * at all, but requires them in a good order.  Other resets reset
     * the index file to the tree object we are switching to. */
    if (reset_type == SOFT || reset_type == KEEP)
        die_if_unmerged_cache(reset_type);

    if (reset_type != SOFT) {
        struct lock_file lock = LOCK_INIT;
        repo_hold_locked_index(the_repository, &lock,
                               LOCK_DIE_ON_ERROR);
        if (reset_type == MIXED) {
            int flags = quiet ? REFRESH_QUIET : REFRESH_IN_PORCELAIN;
            if (read_from_tree(&pathspec, &oid, intent_to_add)) {
                update_ref_status = 1;
                goto cleanup;
            }
            the_repository->index->updated_skipworktree = 1;
            if (!no_refresh && repo_get_work_tree(the_repository)) {
                uint64_t t_begin, t_delta_in_ms;

                t_begin = getnanotime();
                refresh_index(the_repository->index, flags, NULL, NULL,
                              _("Unstaged changes after reset:"));
                t_delta_in_ms = (getnanotime() - t_begin) / 1000000;
                if (!quiet && advice_enabled(ADVICE_RESET_NO_REFRESH_WARNING) && t_delta_in_ms > REFRESH_INDEX_DELAY_WARNING_IN_MS) {
                    advise(_("It took %.2f seconds to refresh the index after reset.  You can use\n"
                            "'--no-refresh' to avoid this."), t_delta_in_ms / 1000.0);
                }
            }
        } else {
            struct object_id dummy;
            char *ref = NULL;
            int err;

            repo_dwim_ref(the_repository, rev, strlen(rev),
                          &dummy, &ref, 0);
            if (ref && !starts_with(ref, "refs/"))
                FREE_AND_NULL(ref);

            err = reset_index(ref, &oid, reset_type, quiet, reset_safety_state.force_level);
            if (reset_type == KEEP && !err)
                err = reset_index(ref, &oid, MIXED, quiet, reset_safety_state.force_level);
            if (err)
                die(_("Could not reset index file to revision '%s'."), rev);
            free(ref);
        }

        if (write_locked_index(the_repository->index, &lock, COMMIT_LOCK))
            die(_("Could not write new index file."));
    }

    if (!pathspec.nr && !unborn) {
        /* Any resets without paths update HEAD to the head being
         * switched to, saving the previous head in ORIG_HEAD before. */
        update_ref_status = reset_refs(rev, &oid);

        if (reset_type == HARD && !update_ref_status && !quiet)
            print_new_head_line(lookup_commit_reference(the_repository, &oid));
    }
    if (!pathspec.nr)
        remove_branch_state(the_repository, 0);

    discard_index(the_repository->index);

cleanup:
    clear_pathspec(&pathspec);
    free(pathspec_from_file);
    safety_clear(&reset_safety_state);
    return update_ref_status;
}
