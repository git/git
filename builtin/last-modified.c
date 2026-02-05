#include "git-compat-util.h"
#include "bloom.h"
#include "builtin.h"
#include "commit-graph.h"
#include "commit-slab.h"
#include "commit.h"
#include "config.h"
#include "diff.h"
#include "diffcore.h"
#include "environment.h"
#include "ewah/ewok.h"
#include "hashmap.h"
#include "hex.h"
#include "object-name.h"
#include "object.h"
#include "parse-options.h"
#include "prio-queue.h"
#include "quote.h"
#include "repository.h"
#include "revision.h"

/* Remember to update object flag allocation in object.h */
#define PARENT1 (1u<<16) /* used instead of SEEN */
#define PARENT2 (1u<<17) /* used instead of BOTTOM, BOUNDARY */

struct last_modified_entry {
	struct hashmap_entry hashent;
	struct object_id oid;
	struct bloom_key key;
	size_t diff_idx;
	const char path[FLEX_ARRAY];
};

static int last_modified_entry_hashcmp(const void *unused UNUSED,
				       const struct hashmap_entry *hent1,
				       const struct hashmap_entry *hent2,
				       const void *path)
{
	const struct last_modified_entry *ent1 =
		container_of(hent1, const struct last_modified_entry, hashent);
	const struct last_modified_entry *ent2 =
		container_of(hent2, const struct last_modified_entry, hashent);
	return strcmp(ent1->path, path ? path : ent2->path);
}

/*
 * Hold a bitmap for each commit we're working with. In the bitmap, each bit
 * represents a path in `lm->all_paths`. An active bit indicates the path still
 * needs to be associated to a commit.
 */
define_commit_slab(active_paths_for_commit, struct bitmap *);

struct last_modified {
	struct hashmap paths;
	struct rev_info rev;
	bool show_trees;
	bool nul_termination;
	int max_depth;

	const char **all_paths;
	size_t all_paths_nr;
	struct active_paths_for_commit active_paths;

	/* 'scratch' to avoid allocating a bitmap every process_parent() */
	struct bitmap *scratch;
};

static struct bitmap *active_paths_for(struct last_modified *lm, struct commit *c)
{
	struct bitmap **bitmap = active_paths_for_commit_at(&lm->active_paths, c);
	if (!*bitmap)
		*bitmap = bitmap_word_alloc(lm->all_paths_nr / BITS_IN_EWORD + 1);

	return *bitmap;
}

static void active_paths_free(struct last_modified *lm, struct commit *c)
{
	struct bitmap **bitmap = active_paths_for_commit_at(&lm->active_paths, c);
	if (*bitmap) {
		bitmap_free(*bitmap);
		*bitmap = NULL;
	}
}

static void last_modified_release(struct last_modified *lm)
{
	struct hashmap_iter iter;
	struct last_modified_entry *ent;

	hashmap_for_each_entry(&lm->paths, &iter, ent, hashent)
		bloom_key_clear(&ent->key);

	hashmap_clear_and_free(&lm->paths, struct last_modified_entry, hashent);
	release_revisions(&lm->rev);

	free(lm->all_paths);
}

struct last_modified_callback_data {
	struct last_modified *lm;
	struct commit *commit;
};

static void add_path_from_diff(struct diff_queue_struct *q,
			       struct diff_options *opt UNUSED, void *data)
{
	struct last_modified *lm = data;

	for (int i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct last_modified_entry *ent;
		const char *path = p->two->path;

		FLEX_ALLOC_STR(ent, path, path);
		oidcpy(&ent->oid, &p->two->oid);
		if (lm->rev.bloom_filter_settings)
			bloom_key_fill(&ent->key, path, strlen(path),
				       lm->rev.bloom_filter_settings);
		hashmap_entry_init(&ent->hashent, strhash(ent->path));
		hashmap_add(&lm->paths, &ent->hashent);
	}
}

static int populate_paths_from_revs(struct last_modified *lm)
{
	int num_interesting = 0;
	struct diff_options diffopt;

	/*
	 * Create a copy of `struct diff_options`. In this copy a callback is
	 * set that when called adds entries to `paths` in `struct last_modified`.
	 * This copy is used to diff the tree of the target revision against an
	 * empty tree. This results in all paths in the target revision being
	 * listed. After `paths` is populated, we don't need this copy no more.
	 */
	memcpy(&diffopt, &lm->rev.diffopt, sizeof(diffopt));
	copy_pathspec(&diffopt.pathspec, &lm->rev.diffopt.pathspec);
	diffopt.output_format = DIFF_FORMAT_CALLBACK;
	diffopt.format_callback = add_path_from_diff;
	diffopt.format_callback_data = lm;

	for (size_t i = 0; i < lm->rev.pending.nr; i++) {
		struct object_array_entry *obj = lm->rev.pending.objects + i;

		if (obj->item->flags & UNINTERESTING)
			continue;

		if (num_interesting++)
			return error(_("last-modified can only operate on one tree at a time"));

		diff_tree_oid(lm->rev.repo->hash_algo->empty_tree,
			      &obj->item->oid, "", &diffopt);
		diff_flush(&diffopt);
	}
	clear_pathspec(&diffopt.pathspec);

	return 0;
}

static void last_modified_emit(struct last_modified *lm,
			       const char *path, const struct commit *commit)

{
	if (commit->object.flags & BOUNDARY)
		putchar('^');
	printf("%s\t", oid_to_hex(&commit->object.oid));

	if (lm->nul_termination)
		printf("%s%c", path, '\0');
	else
		write_name_quoted(path, stdout, '\n');
}

static void mark_path(const char *path, const struct object_id *oid,
		      struct last_modified_callback_data *data)
{
	struct last_modified_entry *ent;

	/* Is it even a path that we are interested in? */
	ent = hashmap_get_entry_from_hash(&data->lm->paths, strhash(path), path,
					  struct last_modified_entry, hashent);
	if (!ent)
		return;

	/*
	 * Is it arriving at a version of interest, or is it from a side branch
	 * which did not contribute to the final state?
	 */
	if (oid && !oideq(oid, &ent->oid))
		return;

	last_modified_emit(data->lm, path, data->commit);

	hashmap_remove(&data->lm->paths, &ent->hashent, path);
	bloom_key_clear(&ent->key);
	free(ent);
}

static void last_modified_diff(struct diff_queue_struct *q,
			       struct diff_options *opt UNUSED, void *cbdata)
{
	struct last_modified_callback_data *data = cbdata;

	for (int i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		switch (p->status) {
		case DIFF_STATUS_DELETED:
			/*
			 * There's no point in feeding a deletion, as it could
			 * not have resulted in our current state, which
			 * actually has the file.
			 */
			break;

		default:
			/*
			 * Otherwise, we care only that we somehow arrived at
			 * a final oid state. Note that this covers some
			 * potentially controversial areas, including:
			 *
			 *  1. A rename or copy will be found, as it is the
			 *     first time the content has arrived at the given
			 *     path.
			 *
			 *  2. Even a non-content modification like a mode or
			 *     type change will trigger it.
			 *
			 * We take the inclusive approach for now, and find
			 * anything which impacts the path. Options to tweak
			 * the behavior (e.g., to "--follow" the content across
			 * renames) can come later.
			 */
			mark_path(p->two->path, &p->two->oid, data);
			break;
		}
	}
}

static void pass_to_parent(struct bitmap *c,
			   struct bitmap *p,
			   size_t pos)
{
	bitmap_unset(c, pos);
	bitmap_set(p, pos);
}

static bool maybe_changed_path(struct last_modified *lm,
			       struct commit *origin,
			       struct bitmap *active)
{
	struct bloom_filter *filter;
	struct last_modified_entry *ent;
	struct hashmap_iter iter;

	if (!lm->rev.bloom_filter_settings)
		return true;

	if (commit_graph_generation(origin) == GENERATION_NUMBER_INFINITY)
		return true;

	filter = get_bloom_filter(lm->rev.repo, origin);
	if (!filter)
		return true;

	hashmap_for_each_entry(&lm->paths, &iter, ent, hashent) {
		if (active && !bitmap_get(active, ent->diff_idx))
			continue;

		if (bloom_filter_contains(filter, &ent->key,
					  lm->rev.bloom_filter_settings))
			return true;
	}
	return false;
}

static void process_parent(struct last_modified *lm,
			   struct prio_queue *queue,
			   struct commit *c, struct bitmap *active_c,
			   struct commit *parent, int parent_i)
{
	struct bitmap *active_p;

	repo_parse_commit(lm->rev.repo, parent);
	active_p = active_paths_for(lm, parent);

	/*
	 * The first time entering this function for this commit (i.e. first parent)
	 * see if Bloom filters will tell us it's worth to do the diff.
	 */
	if (parent_i || maybe_changed_path(lm, c, active_c)) {
		diff_tree_oid(&parent->object.oid,
			      &c->object.oid, "", &lm->rev.diffopt);
		diffcore_std(&lm->rev.diffopt);
	}

	/*
	 * Test each path for TREESAME-ness against the parent. If a path is
	 * TREESAME, pass it on to this parent.
	 *
	 * First, collect all paths that are *not* TREESAME in 'scratch'.
	 * Then, pass paths that *are* TREESAME and active to the parent.
	 */
	for (int i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *fp = diff_queued_diff.queue[i];
		const char *path = fp->two->path;
		struct last_modified_entry *ent =
			hashmap_get_entry_from_hash(&lm->paths, strhash(path), path,
						    struct last_modified_entry, hashent);
		if (ent) {
			size_t k = ent->diff_idx;
			if (bitmap_get(active_c, k))
				bitmap_set(lm->scratch, k);
		}
	}
	for (size_t i = 0; i < lm->all_paths_nr; i++) {
		if (bitmap_get(active_c, i) && !bitmap_get(lm->scratch, i))
			pass_to_parent(active_c, active_p, i);
	}

	/*
	 * If parent has any active paths, put it on the queue (if not already).
	 */
	if (!bitmap_is_empty(active_p) && !(parent->object.flags & PARENT1)) {
		parent->object.flags |= PARENT1;
		prio_queue_put(queue, parent);
	}
	if (!(parent->object.flags & PARENT1))
		active_paths_free(lm, parent);

	MEMZERO_ARRAY(lm->scratch->words, lm->scratch->word_alloc);
	diff_queue_clear(&diff_queued_diff);
}

static int last_modified_run(struct last_modified *lm)
{
	int max_count, queue_popped = 0;
	struct prio_queue queue = { compare_commits_by_gen_then_commit_date };
	struct prio_queue not_queue = { compare_commits_by_gen_then_commit_date };
	struct commit_list *list;
	struct last_modified_callback_data data = { .lm = lm };

	lm->rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	lm->rev.diffopt.format_callback = last_modified_diff;
	lm->rev.diffopt.format_callback_data = &data;
	lm->rev.no_walk = 1;

	prepare_revision_walk(&lm->rev);

	max_count = lm->rev.max_count;

	init_active_paths_for_commit(&lm->active_paths);
	lm->scratch = bitmap_word_alloc(lm->all_paths_nr);

	/*
	 * lm->rev.commits holds the set of boundary commits for our walk.
	 *
	 * Loop through each such commit, and place it in the appropriate queue.
	 */
	for (list = lm->rev.commits; list; list = list->next) {
		struct commit *c = list->item;

		if (c->object.flags & BOTTOM) {
			prio_queue_put(&not_queue, c);
			c->object.flags |= PARENT2;
		} else if (!(c->object.flags & PARENT1)) {
			/*
			 * If the commit is a starting point (and hasn't been
			 * seen yet), then initialize the set of interesting
			 * paths, too.
			 */
			struct bitmap *active;

			prio_queue_put(&queue, c);
			c->object.flags |= PARENT1;

			active = active_paths_for(lm, c);
			for (size_t i = 0; i < lm->all_paths_nr; i++)
				bitmap_set(active, i);
		}
	}

	while (queue.nr) {
		int parent_i;
		struct commit_list *p;
		struct commit *c = prio_queue_get(&queue);
		struct bitmap *active_c = active_paths_for(lm, c);

		if ((0 <= max_count && max_count < ++queue_popped) ||
		    (c->object.flags & PARENT2)) {
			/*
			 * Either a boundary commit, or we have already seen too
			 * many others. Either way, stop here.
			 */
			c->object.flags |= PARENT2 | BOUNDARY;
			data.commit = c;
			diff_tree_oid(lm->rev.repo->hash_algo->empty_tree,
				      &c->object.oid,
				      "", &lm->rev.diffopt);
			diff_flush(&lm->rev.diffopt);
			goto cleanup;
		}

		/*
		 * Otherwise, make sure that 'c' isn't reachable from anything
		 * in the '--not' queue.
		 */
		repo_parse_commit(lm->rev.repo, c);

		while (not_queue.nr) {
			struct commit_list *np;
			struct commit *n = prio_queue_get(&not_queue);

			repo_parse_commit(lm->rev.repo, n);

			for (np = n->parents; np; np = np->next) {
				if (!(np->item->object.flags & PARENT2)) {
					prio_queue_put(&not_queue, np->item);
					np->item->object.flags |= PARENT2;
				}
			}

			if (commit_graph_generation(n) < commit_graph_generation(c))
				break;
		}

		/*
		 * Look at each parent and pass on each path that's TREESAME
		 * with that parent. Stop early when no active paths remain.
		 */
		for (p = c->parents, parent_i = 0; p; p = p->next, parent_i++) {
			process_parent(lm, &queue,
				       c, active_c,
				       p->item, parent_i);

			if (bitmap_is_empty(active_c))
				break;
		}

		/*
		 * Paths that remain active, or not TREESAME with any parent,
		 * were changed by 'c'.
		 */
		if (!bitmap_is_empty(active_c))  {
			data.commit = c;
			for (size_t i = 0; i < lm->all_paths_nr; i++) {
				if (bitmap_get(active_c, i))
					mark_path(lm->all_paths[i], NULL, &data);
			}
		}

cleanup:
		active_paths_free(lm, c);
	}

	if (hashmap_get_size(&lm->paths))
		BUG("paths remaining beyond boundary in last-modified");

	clear_prio_queue(&not_queue);
	clear_prio_queue(&queue);
	clear_active_paths_for_commit(&lm->active_paths);
	bitmap_free(lm->scratch);

	return 0;
}

static int last_modified_init(struct last_modified *lm, struct repository *r,
			      const char *prefix, int argc, const char **argv)
{
	struct hashmap_iter iter;
	struct last_modified_entry *ent;

	hashmap_init(&lm->paths, last_modified_entry_hashcmp, NULL, 0);

	repo_init_revisions(r, &lm->rev, prefix);
	lm->rev.def = "HEAD";
	lm->rev.combine_merges = 1;
	lm->rev.show_root_diff = 1;
	lm->rev.boundary = 1;
	lm->rev.no_commit_id = 1;
	lm->rev.diff = 1;
	lm->rev.diffopt.flags.no_recursive_diff_tree_combined = 1;
	lm->rev.diffopt.flags.recursive = 1;
	lm->rev.diffopt.flags.tree_in_recursive = lm->show_trees;
	lm->rev.diffopt.max_depth = lm->max_depth;
	lm->rev.diffopt.max_depth_valid = lm->max_depth >= 0;

	argc = setup_revisions(argc, argv, &lm->rev, NULL);
	if (argc > 1) {
		error(_("unknown last-modified argument: %s"), argv[1]);
		return argc;
	}

	lm->rev.bloom_filter_settings = get_bloom_filter_settings(lm->rev.repo);

	if (populate_paths_from_revs(lm) < 0)
		return error(_("unable to setup last-modified"));

	CALLOC_ARRAY(lm->all_paths, hashmap_get_size(&lm->paths));
	lm->all_paths_nr = 0;
	hashmap_for_each_entry(&lm->paths, &iter, ent, hashent) {
		ent->diff_idx = lm->all_paths_nr++;
		lm->all_paths[ent->diff_idx] = ent->path;
	}

	return 0;
}

int cmd_last_modified(int argc, const char **argv, const char *prefix,
		      struct repository *repo)
{
	int ret;
	struct last_modified lm = { 0 };

	const char * const last_modified_usage[] = {
		N_("git last-modified [--recursive] [--show-trees] [--max-depth=<depth>] [-z]\n"
		   "                  [<revision-range>] [[--] <pathspec>...]"),
		NULL
	};

	struct option last_modified_options[] = {
		OPT_SET_INT('r', "recursive", &lm.max_depth,
			    N_("recurse into subtrees"), -1),
		OPT_BOOL('t', "show-trees", &lm.show_trees,
			 N_("show tree entries when recursing into subtrees")),
		OPT_INTEGER_F(0, "max-depth", &lm.max_depth,
			      N_("maximum tree depth to recurse"), PARSE_OPT_NONEG),
		OPT_BOOL('z', NULL, &lm.nul_termination,
			 N_("lines are separated with NUL character")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, last_modified_options,
			     last_modified_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT |
			     PARSE_OPT_KEEP_DASHDASH);

	repo_config(repo, git_default_config, NULL);

	ret = last_modified_init(&lm, repo, prefix, argc, argv);
	if (ret > 0)
		usage_with_options(last_modified_usage,
				   last_modified_options);
	if (ret)
		goto out;

	ret = last_modified_run(&lm);
	if (ret)
		goto out;

out:
	last_modified_release(&lm);

	return ret;
}
