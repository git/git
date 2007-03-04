/*
 * Builtin "git diff"
 *
 * Copyright (c) 2006 Junio C Hamano
 */
#include "cache.h"
#include "commit.h"
#include "blob.h"
#include "tag.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "log-tree.h"
#include "builtin.h"

/* NEEDSWORK: struct object has place for name but we _do_
 * know mode when we extracted the blob out of a tree, which
 * we currently lose.
 */
struct blobinfo {
	unsigned char sha1[20];
	const char *name;
};

static const char builtin_diff_usage[] =
"git-diff <options> <rev>{0,2} -- <path>*";

static void stuff_change(struct diff_options *opt,
			 unsigned old_mode, unsigned new_mode,
			 const unsigned char *old_sha1,
			 const unsigned char *new_sha1,
			 const char *old_name,
			 const char *new_name)
{
	struct diff_filespec *one, *two;

	if (!is_null_sha1(old_sha1) && !is_null_sha1(new_sha1) &&
	    !hashcmp(old_sha1, new_sha1))
		return;

	if (opt->reverse_diff) {
		unsigned tmp;
		const unsigned char *tmp_u;
		const char *tmp_c;
		tmp = old_mode; old_mode = new_mode; new_mode = tmp;
		tmp_u = old_sha1; old_sha1 = new_sha1; new_sha1 = tmp_u;
		tmp_c = old_name; old_name = new_name; new_name = tmp_c;
	}
	one = alloc_filespec(old_name);
	two = alloc_filespec(new_name);
	fill_filespec(one, old_sha1, old_mode);
	fill_filespec(two, new_sha1, new_mode);

	/* NEEDSWORK: shouldn't this part of diffopt??? */
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
		die("'%s': %s", path, strerror(errno));
	if (!(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)))
		die("'%s': not a regular file or symlink", path);
	stuff_change(&revs->diffopt,
		     canon_mode(st.st_mode), canon_mode(st.st_mode),
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

	stuff_change(&revs->diffopt,
		     mode, mode,
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
		if (!strcmp(arg, "--cached"))
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
	if (read_cache() < 0) {
		perror("read_cache");
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
	/* Again, the revs are all reverse */
	for (i = 0; i < ents; i++)
		hashcpy((unsigned char *)(parent + i),
			ent[ents - 1 - i].item->sha1);
	diff_tree_combined(parent[0], parent + 1, ents - 1,
			   revs->dense_combined_merges, revs);
	return 0;
}

void add_head(struct rev_info *revs)
{
	unsigned char sha1[20];
	struct object *obj;
	if (get_sha1("HEAD", sha1))
		return;
	obj = parse_object(sha1);
	if (!obj)
		return;
	add_pending_object(revs, obj, "HEAD");
}

int cmd_diff(int argc, const char **argv, const char *prefix)
{
	int i;
	struct rev_info rev;
	struct object_array_entry ent[100];
	int ents = 0, blobs = 0, paths = 0;
	const char *path = NULL;
	struct blobinfo blob[2];
	int nongit = 0;

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
	 * Other cases are errors.
	 */

	prefix = setup_git_directory_gently(&nongit);
	git_config(git_diff_ui_config);
	init_revisions(&rev, prefix);

	if (!setup_diff_no_index(&rev, argc, argv, nongit, prefix))
		argc = 0;
	else
		argc = setup_revisions(argc, argv, &rev, NULL);
	if (!rev.diffopt.output_format) {
		rev.diffopt.output_format = DIFF_FORMAT_PATCH;
		if (diff_setup_done(&rev.diffopt) < 0)
			die("diff_setup_done failed");
	}

	/* Do we have --cached and not have a pending object, then
	 * default to HEAD by hand.  Eek.
	 */
	if (!rev.pending.nr) {
		int i;
		for (i = 1; i < argc; i++) {
			const char *arg = argv[i];
			if (!strcmp(arg, "--"))
				break;
			else if (!strcmp(arg, "--cached")) {
				add_head(&rev);
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
			return run_diff_files_cmd(&rev, argc, argv);
			break;
		case 1:
			if (paths != 1)
				usage(builtin_diff_usage);
			return builtin_diff_b_f(&rev, argc, argv, blob, path);
			break;
		case 2:
			if (paths)
				usage(builtin_diff_usage);
			return builtin_diff_blobs(&rev, argc, argv, blob);
			break;
		default:
			usage(builtin_diff_usage);
		}
	}
	else if (blobs)
		usage(builtin_diff_usage);
	else if (ents == 1)
		return builtin_diff_index(&rev, argc, argv);
	else if (ents == 2)
		return builtin_diff_tree(&rev, argc, argv, ent);
	else if ((ents == 3) && (ent[0].item->flags & UNINTERESTING)) {
		/* diff A...B where there is one sane merge base between
		 * A and B.  We have ent[0] == merge-base, ent[1] == A,
		 * and ent[2] == B.  Show diff between the base and B.
		 */
		ent[1] = ent[2];
		return builtin_diff_tree(&rev, argc, argv, ent);
	}
	else
		return builtin_diff_combined(&rev, argc, argv,
					     ent, ents);
	usage(builtin_diff_usage);
}
