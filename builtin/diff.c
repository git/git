/*
 * Builtin "git diff"
 *
 * Copyright (c) 2006 Junio C Hamano
 */
#include "cache.h"
#include "color.h"
#include "commit.h"
#include "blob.h"
#include "tag.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "log-tree.h"
#include "builtin.h"

struct blobinfo {
	unsigned char sha1[20];
	const char *name;
	unsigned mode;
};

static const char builtin_diff_usage[] =
"git diff <options> <rev>{0,2} -- <path>*";

static void stuff_change(struct diff_options *opt,
			 unsigned old_mode, unsigned new_mode,
			 const unsigned char *old_sha1,
			 const unsigned char *new_sha1,
			 const char *old_name,
			 const char *new_name)
{
	struct diff_filespec *one, *two;

	if (!is_null_sha1(old_sha1) && !is_null_sha1(new_sha1) &&
	    !hashcmp(old_sha1, new_sha1) && (old_mode == new_mode))
		return;

	if (DIFF_OPT_TST(opt, REVERSE_DIFF)) {
		unsigned tmp;
		const unsigned char *tmp_u;
		const char *tmp_c;
		tmp = old_mode; old_mode = new_mode; new_mode = tmp;
		tmp_u = old_sha1; old_sha1 = new_sha1; new_sha1 = tmp_u;
		tmp_c = old_name; old_name = new_name; new_name = tmp_c;
	}

	if (opt->prefix &&
	    (strncmp(old_name, opt->prefix, opt->prefix_length) ||
	     strncmp(new_name, opt->prefix, opt->prefix_length)))
		return;

	one = alloc_filespec(old_name);
	two = alloc_filespec(new_name);
	fill_filespec(one, old_sha1, old_mode);
	fill_filespec(two, new_sha1, new_mode);

	diff_queue(&diff_queued_diff, one, two);
}

static int builtin_diff_b_f(struct rev_info *revs,
			    int argc, const char **argv,
			    struct blobinfo *blob,
			    const char *path)
{
	/* Blob vs file in the working tree*/
	struct stat st;

	if (argc > 1)
		usage(builtin_diff_usage);

	if (lstat(path, &st))
		die_errno("failed to stat '%s'", path);
	if (!(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)))
		die("'%s': not a regular file or symlink", path);

	diff_set_mnemonic_prefix(&revs->diffopt, "o/", "w/");

	if (blob[0].mode == S_IFINVALID)
		blob[0].mode = canon_mode(st.st_mode);

	stuff_change(&revs->diffopt,
		     blob[0].mode, canon_mode(st.st_mode),
		     blob[0].sha1, null_sha1,
		     path, path);
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	return 0;
}

static int builtin_diff_blobs(struct rev_info *revs,
			      int argc, const char **argv,
			      struct blobinfo *blob)
{
	unsigned mode = canon_mode(S_IFREG | 0644);

	if (argc > 1)
		usage(builtin_diff_usage);

	if (blob[0].mode == S_IFINVALID)
		blob[0].mode = mode;

	if (blob[1].mode == S_IFINVALID)
		blob[1].mode = mode;

	stuff_change(&revs->diffopt,
		     blob[0].mode, blob[1].mode,
		     blob[0].sha1, blob[1].sha1,
		     blob[0].name, blob[1].name);
	diffcore_std(&revs->diffopt);
	diff_flush(&revs->diffopt);
	return 0;
}

static int builtin_diff_index(struct rev_info *revs,
			      int argc, const char **argv)
{
	int cached = 0;
	while (1 < argc) {
		const char *arg = argv[1];
		if (!strcmp(arg, "--cached") || !strcmp(arg, "--staged"))
			cached = 1;
		else
			usage(builtin_diff_usage);
		argv++; argc--;
	}
	if (!cached)
		setup_work_tree();
	/*
	 * Make sure there is one revision (i.e. pending object),
	 * and there is no revision filtering parameters.
	 */
	if (revs->pending.nr != 1 ||
	    revs->max_count != -1 || revs->min_age != -1 ||
	    revs->max_age != -1)
		usage(builtin_diff_usage);
	if (read_cache_preload(revs->diffopt.paths) < 0) {
		perror("read_cache_preload");
		return -1;
	}
	return run_diff_index(revs, cached);
}

static int builtin_diff_tree(struct rev_info *revs,
			     int argc, const char **argv,
			     struct object_array_entry *ent)
{
	const unsigned char *(sha1[2]);
	int swap = 0;

	if (argc > 1)
		usage(builtin_diff_usage);

	/* We saw two trees, ent[0] and ent[1].
	 * if ent[1] is uninteresting, they are swapped
	 */
	if (ent[1].item->flags & UNINTERESTING)
		swap = 1;
	sha1[swap] = ent[0].item->sha1;
	sha1[1-swap] = ent[1].item->sha1;
	diff_tree_sha1(sha1[0], sha1[1], "", &revs->diffopt);
	log_tree_diff_flush(revs);
	return 0;
}

static int builtin_diff_combined(struct rev_info *revs,
				 int argc, const char **argv,
				 struct object_array_entry *ent,
				 int ents)
{
	const unsigned char (*parent)[20];
	int i;

	if (argc > 1)
		usage(builtin_diff_usage);

	if (!revs->dense_combined_merges && !revs->combine_merges)
		revs->dense_combined_merges = revs->combine_merges = 1;
	parent = xmalloc(ents * sizeof(*parent));
	for (i = 0; i < ents; i++)
		hashcpy((unsigned char *)(parent + i), ent[i].item->sha1);
	diff_tree_combined(parent[0], parent + 1, ents - 1,
			   revs->dense_combined_merges, revs);
	return 0;
}

static void refresh_index_quietly(void)
{
	struct lock_file *lock_file;
	int fd;

	lock_file = xcalloc(1, sizeof(struct lock_file));
	fd = hold_locked_index(lock_file, 0);
	if (fd < 0)
		return;
	discard_cache();
	read_cache();
	refresh_cache(REFRESH_QUIET|REFRESH_UNMERGED);

	if (active_cache_changed &&
	    !write_cache(fd, active_cache, active_nr))
		commit_locked_index(lock_file);

	rollback_lock_file(lock_file);
}

static int builtin_diff_files(struct rev_info *revs, int argc, const char **argv)
{
	int result;
	unsigned int options = 0;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "--base"))
			revs->max_count = 1;
		else if (!strcmp(argv[1], "--ours"))
			revs->max_count = 2;
		else if (!strcmp(argv[1], "--theirs"))
			revs->max_count = 3;
		else if (!strcmp(argv[1], "-q"))
			options |= DIFF_SILENT_ON_REMOVED;
		else if (!strcmp(argv[1], "-h"))
			usage(builtin_diff_usage);
		else
			return error("invalid option: %s", argv[1]);
		argv++; argc--;
	}

	/*
	 * "diff --base" should not combine merges because it was not
	 * asked to.  "diff -c" should not densify (if the user wants
	 * dense one, --cc can be explicitly asked for, or just rely
	 * on the default).
	 */
	if (revs->max_count == -1 && !revs->combine_merges &&
	    (revs->diffopt.output_format & DIFF_FORMAT_PATCH))
		revs->combine_merges = revs->dense_combined_merges = 1;

	setup_work_tree();
	if (read_cache_preload(revs->diffopt.paths) < 0) {
		perror("read_cache_preload");
		return -1;
	}
	result = run_diff_files(revs, options);
	return diff_result_code(&revs->diffopt, result);
}

int cmd_diff(int argc, const char **argv, const char *prefix)
{
	int i;
	struct rev_info rev;
	struct object_array_entry ent[100];
	int ents = 0, blobs = 0, paths = 0;
	const char *path = NULL;
	struct blobinfo blob[2];
	int nongit;
	int result = 0;

	/*
	 * We could get N tree-ish in the rev.pending_objects list.
	 * Also there could be M blobs there, and P pathspecs.
	 *
	 * N=0, M=0:
	 *	cache vs files (diff-files)
	 * N=0, M=2:
	 *      compare two random blobs.  P must be zero.
	 * N=0, M=1, P=1:
	 *	compare a blob with a working tree file.
	 *
	 * N=1, M=0:
	 *      tree vs cache (diff-index --cached)
	 *
	 * N=2, M=0:
	 *      tree vs tree (diff-tree)
	 *
	 * N=0, M=0, P=2:
	 *      compare two filesystem entities (aka --no-index).
	 *
	 * Other cases are errors.
	 */

	prefix = setup_git_directory_gently(&nongit);
	git_config(git_diff_ui_config, NULL);

	if (diff_use_color_default == -1)
		diff_use_color_default = git_use_color_default;

	init_revisions(&rev, prefix);

	/* If this is a no-index diff, just run it and exit there. */
	diff_no_index(&rev, argc, argv, nongit, prefix);

	/* Otherwise, we are doing the usual "git" diff */
	rev.diffopt.skip_stat_unmatch = !!diff_auto_refresh_index;

	/* Default to let external and textconv be used */
	DIFF_OPT_SET(&rev.diffopt, ALLOW_EXTERNAL);
	DIFF_OPT_SET(&rev.diffopt, ALLOW_TEXTCONV);

	if (nongit)
		die("Not a git repository");
	argc = setup_revisions(argc, argv, &rev, NULL);
	if (!rev.diffopt.output_format) {
		rev.diffopt.output_format = DIFF_FORMAT_PATCH;
		if (diff_setup_done(&rev.diffopt) < 0)
			die("diff_setup_done failed");
	}

	DIFF_OPT_SET(&rev.diffopt, RECURSIVE);

	/*
	 * If the user asked for our exit code then don't start a
	 * pager or we would end up reporting its exit code instead.
	 */
	if (!DIFF_OPT_TST(&rev.diffopt, EXIT_WITH_STATUS) &&
	    check_pager_config("diff") != 0)
		setup_pager();

	/*
	 * Do we have --cached and not have a pending object, then
	 * default to HEAD by hand.  Eek.
	 */
	if (!rev.pending.nr) {
		int i;
		for (i = 1; i < argc; i++) {
			const char *arg = argv[i];
			if (!strcmp(arg, "--"))
				break;
			else if (!strcmp(arg, "--cached") ||
				 !strcmp(arg, "--staged")) {
				add_head_to_pending(&rev);
				if (!rev.pending.nr)
					die("No HEAD commit to compare with (yet)");
				break;
			}
		}
	}

	for (i = 0; i < rev.pending.nr; i++) {
		struct object_array_entry *list = rev.pending.objects+i;
		struct object *obj = list->item;
		const char *name = list->name;
		int flags = (obj->flags & UNINTERESTING);
		if (!obj->parsed)
			obj = parse_object(obj->sha1);
		obj = deref_tag(obj, NULL, 0);
		if (!obj)
			die("invalid object '%s' given.", name);
		if (obj->type == OBJ_COMMIT)
			obj = &((struct commit *)obj)->tree->object;
		if (obj->type == OBJ_TREE) {
			if (ARRAY_SIZE(ent) <= ents)
				die("more than %d trees given: '%s'",
				    (int) ARRAY_SIZE(ent), name);
			obj->flags |= flags;
			ent[ents].item = obj;
			ent[ents].name = name;
			ents++;
			continue;
		}
		if (obj->type == OBJ_BLOB) {
			if (2 <= blobs)
				die("more than two blobs given: '%s'", name);
			hashcpy(blob[blobs].sha1, obj->sha1);
			blob[blobs].name = name;
			blob[blobs].mode = list->mode;
			blobs++;
			continue;

		}
		die("unhandled object '%s' given.", name);
	}
	if (rev.prune_data) {
		const char **pathspec = rev.prune_data;
		while (*pathspec) {
			if (!path)
				path = *pathspec;
			paths++;
			pathspec++;
		}
	}

	/*
	 * Now, do the arguments look reasonable?
	 */
	if (!ents) {
		switch (blobs) {
		case 0:
			result = builtin_diff_files(&rev, argc, argv);
			break;
		case 1:
			if (paths != 1)
				usage(builtin_diff_usage);
			result = builtin_diff_b_f(&rev, argc, argv, blob, path);
			break;
		case 2:
			if (paths)
				usage(builtin_diff_usage);
			result = builtin_diff_blobs(&rev, argc, argv, blob);
			break;
		default:
			usage(builtin_diff_usage);
		}
	}
	else if (blobs)
		usage(builtin_diff_usage);
	else if (ents == 1)
		result = builtin_diff_index(&rev, argc, argv);
	else if (ents == 2)
		result = builtin_diff_tree(&rev, argc, argv, ent);
	else if (ent[0].item->flags & UNINTERESTING) {
		/*
		 * diff A...B where there is at least one merge base
		 * between A and B.  We have ent[0] == merge-base,
		 * ent[ents-2] == A, and ent[ents-1] == B.  Show diff
		 * between the base and B.  Note that we pick one
		 * merge base at random if there are more than one.
		 */
		ent[1] = ent[ents-1];
		result = builtin_diff_tree(&rev, argc, argv, ent);
	} else
		result = builtin_diff_combined(&rev, argc, argv,
					       ent, ents);
	result = diff_result_code(&rev.diffopt, result);
	if (1 < rev.diffopt.skip_stat_unmatch)
		refresh_index_quietly();
	return result;
}
