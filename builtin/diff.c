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
#include "submodule.h"
#include "sha1-array.h"

#define DIFF_NO_INDEX_EXPLICIT 1
#define DIFF_NO_INDEX_IMPLICIT 2

struct blobinfo {
	unsigned char sha1[20];
	const char *name;
	unsigned mode;
};

static const char builtin_diff_usage[] =
"git diff [<options>] [<commit> [<commit>]] [--] [<path>...]";

static void stuff_change(struct diff_options *opt,
			 unsigned old_mode, unsigned new_mode,
			 const unsigned char *old_sha1,
			 const unsigned char *new_sha1,
			 int old_sha1_valid,
			 int new_sha1_valid,
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
	fill_filespec(one, old_sha1, old_sha1_valid, old_mode);
	fill_filespec(two, new_sha1, new_sha1_valid, new_mode);

	diff_queue(&diff_queued_diff, one, two);
}

static int builtin_diff_b_f(struct rev_info *revs,
			    int argc, const char **argv,
			    struct blobinfo *blob)
{
	/* Blob vs file in the working tree*/
	struct stat st;
	const char *path;

	if (argc > 1)
		usage(builtin_diff_usage);

	GUARD_PATHSPEC(&revs->prune_data, PATHSPEC_FROMTOP | PATHSPEC_LITERAL);
	path = revs->prune_data.items[0].match;

	if (lstat(path, &st))
		die_errno(_("failed to stat '%s'"), path);
	if (!(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)))
		die(_("'%s': not a regular file or symlink"), path);

	diff_set_mnemonic_prefix(&revs->diffopt, "o/", "w/");

	if (blob[0].mode == S_IFINVALID)
		blob[0].mode = canon_mode(st.st_mode);

	stuff_change(&revs->diffopt,
		     blob[0].mode, canon_mode(st.st_mode),
		     blob[0].sha1, null_sha1,
		     1, 0,
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
		     1, 1,
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
	/*
	 * Make sure there is one revision (i.e. pending object),
	 * and there is no revision filtering parameters.
	 */
	if (revs->pending.nr != 1 ||
	    revs->max_count != -1 || revs->min_age != -1 ||
	    revs->max_age != -1)
		usage(builtin_diff_usage);
	if (!cached) {
		setup_work_tree();
		if (read_cache_preload(&revs->diffopt.pathspec) < 0) {
			perror("read_cache_preload");
			return -1;
		}
	} else if (read_cache() < 0) {
		perror("read_cache");
		return -1;
	}
	return run_diff_index(revs, cached);
}

static int builtin_diff_tree(struct rev_info *revs,
			     int argc, const char **argv,
			     struct object_array_entry *ent0,
			     struct object_array_entry *ent1)
{
	const unsigned char *(sha1[2]);
	int swap = 0;

	if (argc > 1)
		usage(builtin_diff_usage);

	/*
	 * We saw two trees, ent0 and ent1.  If ent1 is uninteresting,
	 * swap them.
	 */
	if (ent1->item->flags & UNINTERESTING)
		swap = 1;
	sha1[swap] = ent0->item->sha1;
	sha1[1 - swap] = ent1->item->sha1;
	diff_tree_sha1(sha1[0], sha1[1], "", &revs->diffopt);
	log_tree_diff_flush(revs);
	return 0;
}

static int builtin_diff_combined(struct rev_info *revs,
				 int argc, const char **argv,
				 struct object_array_entry *ent,
				 int ents)
{
	struct sha1_array parents = SHA1_ARRAY_INIT;
	int i;

	if (argc > 1)
		usage(builtin_diff_usage);

	if (!revs->dense_combined_merges && !revs->combine_merges)
		revs->dense_combined_merges = revs->combine_merges = 1;
	for (i = 1; i < ents; i++)
		sha1_array_append(&parents, ent[i].item->sha1);
	diff_tree_combined(ent[0].item->sha1, &parents,
			   revs->dense_combined_merges, revs);
	sha1_array_clear(&parents);
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
	update_index_if_able(&the_index, lock_file);
}

static int builtin_diff_files(struct rev_info *revs, int argc, const char **argv)
{
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
			return error(_("invalid option: %s"), argv[1]);
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
	if (read_cache_preload(&revs->diffopt.pathspec) < 0) {
		perror("read_cache_preload");
		return -1;
	}
	return run_diff_files(revs, options);
}

int cmd_diff(int argc, const char **argv, const char *prefix)
{
	int i;
	struct rev_info rev;
	struct object_array ent = OBJECT_ARRAY_INIT;
	int blobs = 0, paths = 0;
	struct blobinfo blob[2];
	int nongit = 0, no_index = 0;
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

	/* Were we asked to do --no-index explicitly? */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
			i++;
			break;
		}
		if (!strcmp(argv[i], "--no-index"))
			no_index = DIFF_NO_INDEX_EXPLICIT;
		if (argv[i][0] != '-')
			break;
	}

	if (!no_index)
		prefix = setup_git_directory_gently(&nongit);

	/*
	 * Treat git diff with at least one path outside of the
	 * repo the same as if the command would have been executed
	 * outside of a git repository.  In this case it behaves
	 * the same way as "git diff --no-index <a> <b>", which acts
	 * as a colourful "diff" replacement.
	 */
	if (nongit || ((argc == i + 2) &&
		       (!path_inside_repo(prefix, argv[i]) ||
			!path_inside_repo(prefix, argv[i + 1]))))
		no_index = DIFF_NO_INDEX_IMPLICIT;

	if (!no_index)
		gitmodules_config();
	git_config(git_diff_ui_config, NULL);

	init_revisions(&rev, prefix);

	if (no_index && argc != i + 2) {
		if (no_index == DIFF_NO_INDEX_IMPLICIT) {
			/*
			 * There was no --no-index and there were not two
			 * paths. It is possible that the user intended
			 * to do an inside-repository operation.
			 */
			fprintf(stderr, "Not a git repository\n");
			fprintf(stderr,
				"To compare two paths outside a working tree:\n");
		}
		/* Give the usage message for non-repository usage and exit. */
		usagef("git diff %s <path> <path>",
		       no_index == DIFF_NO_INDEX_EXPLICIT ?
		       "--no-index" : "[--no-index]");

	}
	if (no_index)
		/* If this is a no-index diff, just run it and exit there. */
		diff_no_index(&rev, argc, argv, prefix);

	/* Otherwise, we are doing the usual "git" diff */
	rev.diffopt.skip_stat_unmatch = !!diff_auto_refresh_index;

	/* Scale to real terminal size and respect statGraphWidth config */
	rev.diffopt.stat_width = -1;
	rev.diffopt.stat_graph_width = -1;

	/* Default to let external and textconv be used */
	DIFF_OPT_SET(&rev.diffopt, ALLOW_EXTERNAL);
	DIFF_OPT_SET(&rev.diffopt, ALLOW_TEXTCONV);

	if (nongit)
		die(_("Not a git repository"));
	argc = setup_revisions(argc, argv, &rev, NULL);
	if (!rev.diffopt.output_format) {
		rev.diffopt.output_format = DIFF_FORMAT_PATCH;
		diff_setup_done(&rev.diffopt);
	}

	DIFF_OPT_SET(&rev.diffopt, RECURSIVE);

	setup_diff_pager(&rev.diffopt);

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
				if (!rev.pending.nr) {
					struct tree *tree;
					tree = lookup_tree(EMPTY_TREE_SHA1_BIN);
					add_pending_object(&rev, &tree->object, "HEAD");
				}
				break;
			}
		}
	}

	for (i = 0; i < rev.pending.nr; i++) {
		struct object_array_entry *entry = &rev.pending.objects[i];
		struct object *obj = entry->item;
		const char *name = entry->name;
		int flags = (obj->flags & UNINTERESTING);
		if (!obj->parsed)
			obj = parse_object(obj->sha1);
		obj = deref_tag(obj, NULL, 0);
		if (!obj)
			die(_("invalid object '%s' given."), name);
		if (obj->type == OBJ_COMMIT)
			obj = &((struct commit *)obj)->tree->object;

		if (obj->type == OBJ_TREE) {
			obj->flags |= flags;
			add_object_array(obj, name, &ent);
		} else if (obj->type == OBJ_BLOB) {
			if (2 <= blobs)
				die(_("more than two blobs given: '%s'"), name);
			hashcpy(blob[blobs].sha1, obj->sha1);
			blob[blobs].name = name;
			blob[blobs].mode = entry->mode;
			blobs++;

		} else {
			die(_("unhandled object '%s' given."), name);
		}
	}
	if (rev.prune_data.nr)
		paths += rev.prune_data.nr;

	/*
	 * Now, do the arguments look reasonable?
	 */
	if (!ent.nr) {
		switch (blobs) {
		case 0:
			result = builtin_diff_files(&rev, argc, argv);
			break;
		case 1:
			if (paths != 1)
				usage(builtin_diff_usage);
			result = builtin_diff_b_f(&rev, argc, argv, blob);
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
	else if (ent.nr == 1)
		result = builtin_diff_index(&rev, argc, argv);
	else if (ent.nr == 2)
		result = builtin_diff_tree(&rev, argc, argv,
					   &ent.objects[0], &ent.objects[1]);
	else if (ent.objects[0].item->flags & UNINTERESTING) {
		/*
		 * diff A...B where there is at least one merge base
		 * between A and B.  We have ent.objects[0] ==
		 * merge-base, ent.objects[ents-2] == A, and
		 * ent.objects[ents-1] == B.  Show diff between the
		 * base and B.  Note that we pick one merge base at
		 * random if there are more than one.
		 */
		result = builtin_diff_tree(&rev, argc, argv,
					   &ent.objects[0],
					   &ent.objects[ent.nr-1]);
	} else
		result = builtin_diff_combined(&rev, argc, argv,
					       ent.objects, ent.nr);
	result = diff_result_code(&rev.diffopt, result);
	if (1 < rev.diffopt.skip_stat_unmatch)
		refresh_index_quietly();
	return result;
}
