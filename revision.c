#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "object-name.h"
#include "object-file.h"
#include "odb.h"
#include "oidset.h"
#include "tag.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "diff.h"
#include "diff-merges.h"
#include "refs.h"
#include "revision.h"
#include "repository.h"
#include "graph.h"
#include "grep.h"
#include "reflog-walk.h"
#include "patch-ids.h"
#include "decorate.h"
#include "string-list.h"
#include "line-log.h"
#include "mailmap.h"
#include "commit-slab.h"
#include "cache-tree.h"
#include "bisect.h"
#include "packfile.h"
#include "worktree.h"
#include "path.h"
#include "read-cache.h"
#include "setup.h"
#include "sparse-index.h"
#include "strvec.h"
#include "trace2.h"
#include "commit-reach.h"
#include "commit-graph.h"
#include "prio-queue.h"
#include "hashmap.h"
#include "utf8.h"
#include "bloom.h"
#include "json-writer.h"
#include "list-objects-filter-options.h"
#include "resolve-undo.h"
#include "parse-options.h"
#include "wildmatch.h"

volatile show_early_output_fn_t show_early_output;

static char *term_bad;
static char *term_good;

implement_shared_commit_slab(revision_sources, char *);

static inline int want_ancestry(const struct rev_info *revs);

static void mark_blob_uninteresting(struct blob *blob)
{
	if (!blob)
		return;
	if (blob->object.flags & UNINTERESTING)
		return;
	blob->object.flags |= UNINTERESTING;
}

static void mark_tree_contents_uninteresting(struct repository *r,
					     struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry entry;

	if (parse_tree_gently(tree, 1) < 0)
		return;

	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			mark_tree_uninteresting(r, lookup_tree(r, &entry.oid));
			break;
		case OBJ_BLOB:
			mark_blob_uninteresting(lookup_blob(r, &entry.oid));
			break;
		default:
			/* Subproject commit - not in this repository */
			break;
		}
	}

	/*
	 * We don't care about the tree any more
	 * after it has been marked uninteresting.
	 */
	free_tree_buffer(tree);
}

void mark_tree_uninteresting(struct repository *r, struct tree *tree)
{
	struct object *obj;

	if (!tree)
		return;

	obj = &tree->object;
	if (obj->flags & UNINTERESTING)
		return;
	obj->flags |= UNINTERESTING;
	mark_tree_contents_uninteresting(r, tree);
}

struct path_and_oids_entry {
	struct hashmap_entry ent;
	char *path;
	struct oidset trees;
};

static int path_and_oids_cmp(const void *hashmap_cmp_fn_data UNUSED,
			     const struct hashmap_entry *eptr,
			     const struct hashmap_entry *entry_or_key,
			     const void *keydata UNUSED)
{
	const struct path_and_oids_entry *e1, *e2;

	e1 = container_of(eptr, const struct path_and_oids_entry, ent);
	e2 = container_of(entry_or_key, const struct path_and_oids_entry, ent);

	return strcmp(e1->path, e2->path);
}

static void paths_and_oids_clear(struct hashmap *map)
{
	struct hashmap_iter iter;
	struct path_and_oids_entry *entry;

	hashmap_for_each_entry(map, &iter, entry, ent /* member name */) {
		oidset_clear(&entry->trees);
		free(entry->path);
	}

	hashmap_clear_and_free(map, struct path_and_oids_entry, ent);
}

static void paths_and_oids_insert(struct hashmap *map,
				  const char *path,
				  const struct object_id *oid)
{
	int hash = strhash(path);
	struct path_and_oids_entry key;
	struct path_and_oids_entry *entry;

	hashmap_entry_init(&key.ent, hash);

	/* use a shallow copy for the lookup */
	key.path = (char *)path;
	oidset_init(&key.trees, 0);

	entry = hashmap_get_entry(map, &key, ent, NULL);
	if (!entry) {
		CALLOC_ARRAY(entry, 1);
		hashmap_entry_init(&entry->ent, hash);
		entry->path = xstrdup(key.path);
		oidset_init(&entry->trees, 16);
		hashmap_put(map, &entry->ent);
	}

	oidset_insert(&entry->trees, oid);
}

static void add_children_by_path(struct repository *r,
				 struct tree *tree,
				 struct hashmap *map)
{
	struct tree_desc desc;
	struct name_entry entry;

	if (!tree)
		return;

	if (parse_tree_gently(tree, 1) < 0)
		return;

	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			paths_and_oids_insert(map, entry.path, &entry.oid);

			if (tree->object.flags & UNINTERESTING) {
				struct tree *child = lookup_tree(r, &entry.oid);
				if (child)
					child->object.flags |= UNINTERESTING;
			}
			break;
		case OBJ_BLOB:
			if (tree->object.flags & UNINTERESTING) {
				struct blob *child = lookup_blob(r, &entry.oid);
				if (child)
					child->object.flags |= UNINTERESTING;
			}
			break;
		default:
			/* Subproject commit - not in this repository */
			break;
		}
	}

	free_tree_buffer(tree);
}

void mark_trees_uninteresting_sparse(struct repository *r,
				     struct oidset *trees)
{
	unsigned has_interesting = 0, has_uninteresting = 0;
	struct hashmap map = HASHMAP_INIT(path_and_oids_cmp, NULL);
	struct hashmap_iter map_iter;
	struct path_and_oids_entry *entry;
	struct object_id *oid;
	struct oidset_iter iter;

	oidset_iter_init(trees, &iter);
	while ((!has_interesting || !has_uninteresting) &&
	       (oid = oidset_iter_next(&iter))) {
		struct tree *tree = lookup_tree(r, oid);

		if (!tree)
			continue;

		if (tree->object.flags & UNINTERESTING)
			has_uninteresting = 1;
		else
			has_interesting = 1;
	}

	/* Do not walk unless we have both types of trees. */
	if (!has_uninteresting || !has_interesting)
		return;

	oidset_iter_init(trees, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		struct tree *tree = lookup_tree(r, oid);
		add_children_by_path(r, tree, &map);
	}

	hashmap_for_each_entry(&map, &map_iter, entry, ent /* member name */)
		mark_trees_uninteresting_sparse(r, &entry->trees);

	paths_and_oids_clear(&map);
}

struct commit_stack {
	struct commit **items;
	size_t nr, alloc;
};
#define COMMIT_STACK_INIT { 0 }

static void commit_stack_push(struct commit_stack *stack, struct commit *commit)
{
	ALLOC_GROW(stack->items, stack->nr + 1, stack->alloc);
	stack->items[stack->nr++] = commit;
}

static struct commit *commit_stack_pop(struct commit_stack *stack)
{
	return stack->nr ? stack->items[--stack->nr] : NULL;
}

static void commit_stack_clear(struct commit_stack *stack)
{
	FREE_AND_NULL(stack->items);
	stack->nr = stack->alloc = 0;
}

static void mark_one_parent_uninteresting(struct rev_info *revs, struct commit *commit,
					  struct commit_stack *pending)
{
	struct commit_list *l;

	if (commit->object.flags & UNINTERESTING)
		return;
	commit->object.flags |= UNINTERESTING;

	/*
	 * Normally we haven't parsed the parent
	 * yet, so we won't have a parent of a parent
	 * here. However, it may turn out that we've
	 * reached this commit some other way (where it
	 * wasn't uninteresting), in which case we need
	 * to mark its parents recursively too..
	 */
	for (l = commit->parents; l; l = l->next) {
		commit_stack_push(pending, l->item);
		if (revs && revs->exclude_first_parent_only)
			break;
	}
}

void mark_parents_uninteresting(struct rev_info *revs, struct commit *commit)
{
	struct commit_stack pending = COMMIT_STACK_INIT;
	struct commit_list *l;

	for (l = commit->parents; l; l = l->next) {
		mark_one_parent_uninteresting(revs, l->item, &pending);
		if (revs && revs->exclude_first_parent_only)
			break;
	}

	while (pending.nr > 0)
		mark_one_parent_uninteresting(revs, commit_stack_pop(&pending),
					      &pending);

	commit_stack_clear(&pending);
}

static void add_pending_object_with_path(struct rev_info *revs,
					 struct object *obj,
					 const char *name, unsigned mode,
					 const char *path)
{
	struct interpret_branch_name_options options = { 0 };
	if (!obj)
		return;
	if (revs->no_walk && (obj->flags & UNINTERESTING))
		revs->no_walk = 0;
	if (revs->reflog_info && obj->type == OBJ_COMMIT) {
		struct strbuf buf = STRBUF_INIT;
		size_t namelen = strlen(name);
		int len = repo_interpret_branch_name(the_repository, name,
						     namelen, &buf, &options);

		if (0 < len && len < namelen && buf.len)
			strbuf_addstr(&buf, name + len);
		add_reflog_for_walk(revs->reflog_info,
				    (struct commit *)obj,
				    buf.buf[0] ? buf.buf: name);
		strbuf_release(&buf);
		return; /* do not add the commit itself */
	}
	add_object_array_with_path(obj, name, &revs->pending, mode, path);
}

static void add_pending_object_with_mode(struct rev_info *revs,
					 struct object *obj,
					 const char *name, unsigned mode)
{
	add_pending_object_with_path(revs, obj, name, mode, NULL);
}

void add_pending_object(struct rev_info *revs,
			struct object *obj, const char *name)
{
	add_pending_object_with_mode(revs, obj, name, S_IFINVALID);
}

void add_head_to_pending(struct rev_info *revs)
{
	struct object_id oid;
	struct object *obj;
	if (repo_get_oid(the_repository, "HEAD", &oid))
		return;
	obj = parse_object(revs->repo, &oid);
	if (!obj)
		return;
	add_pending_object(revs, obj, "HEAD");
}

static struct object *get_reference(struct rev_info *revs, const char *name,
				    const struct object_id *oid,
				    unsigned int flags)
{
	struct object *object;

	object = parse_object_with_flags(revs->repo, oid,
					 revs->verify_objects ? 0 :
					 PARSE_OBJECT_SKIP_HASH_CHECK |
					 PARSE_OBJECT_DISCARD_TREE);

	if (!object) {
		if (revs->ignore_missing)
			return NULL;
		if (revs->exclude_promisor_objects &&
		    is_promisor_object(revs->repo, oid))
			return NULL;
		if (revs->do_not_die_on_missing_objects) {
			oidset_insert(&revs->missing_commits, oid);
			return NULL;
		}
		die("bad object %s", name);
	}
	object->flags |= flags;
	return object;
}

void add_pending_oid(struct rev_info *revs, const char *name,
		      const struct object_id *oid, unsigned int flags)
{
	struct object *object = get_reference(revs, name, oid, flags);
	add_pending_object(revs, object, name);
}

static struct commit *handle_commit(struct rev_info *revs,
				    struct object_array_entry *entry)
{
	struct object *object = entry->item;
	const char *name = entry->name;
	const char *path = entry->path;
	unsigned int mode = entry->mode;
	unsigned long flags = object->flags;

	/*
	 * Tag object? Look what it points to..
	 */
	while (object->type == OBJ_TAG) {
		struct tag *tag = (struct tag *) object;
		struct object_id *oid;
		if (revs->tag_objects && !(flags & UNINTERESTING))
			add_pending_object(revs, object, tag->tag);
		oid = get_tagged_oid(tag);
		object = parse_object(revs->repo, oid);
		if (!object) {
			if (revs->ignore_missing_links || (flags & UNINTERESTING))
				return NULL;
			if (revs->exclude_promisor_objects &&
			    is_promisor_object(revs->repo, &tag->tagged->oid))
				return NULL;
			if (revs->do_not_die_on_missing_objects && oid) {
				oidset_insert(&revs->missing_commits, oid);
				return NULL;
			}
			die("bad object %s", oid_to_hex(&tag->tagged->oid));
		}
		object->flags |= flags;
		/*
		 * We'll handle the tagged object by looping or dropping
		 * through to the non-tag handlers below. Do not
		 * propagate path data from the tag's pending entry.
		 */
		path = NULL;
		mode = 0;
	}

	/*
	 * Commit object? Just return it, we'll do all the complex
	 * reachability crud.
	 */
	if (object->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *)object;

		if (repo_parse_commit(revs->repo, commit) < 0)
			die("unable to parse commit %s", name);
		if (flags & UNINTERESTING) {
			mark_parents_uninteresting(revs, commit);

			if (!revs->topo_order || !generation_numbers_enabled(the_repository))
				revs->limited = 1;
		}
		if (revs->sources) {
			char **slot = revision_sources_at(revs->sources, commit);

			if (!*slot)
				*slot = xstrdup(name);
		}
		return commit;
	}

	/*
	 * Tree object? Either mark it uninteresting, or add it
	 * to the list of objects to look at later..
	 */
	if (object->type == OBJ_TREE) {
		struct tree *tree = (struct tree *)object;
		if (!revs->tree_objects)
			return NULL;
		if (flags & UNINTERESTING) {
			mark_tree_contents_uninteresting(revs->repo, tree);
			return NULL;
		}
		add_pending_object_with_path(revs, object, name, mode, path);
		return NULL;
	}

	/*
	 * Blob object? You know the drill by now..
	 */
	if (object->type == OBJ_BLOB) {
		if (!revs->blob_objects)
			return NULL;
		if (flags & UNINTERESTING)
			return NULL;
		add_pending_object_with_path(revs, object, name, mode, path);
		return NULL;
	}
	die("%s is unknown object", name);
}

static int everybody_uninteresting(struct commit_list *orig,
				   struct commit **interesting_cache)
{
	struct commit_list *list = orig;

	if (*interesting_cache) {
		struct commit *commit = *interesting_cache;
		if (!(commit->object.flags & UNINTERESTING))
			return 0;
	}

	while (list) {
		struct commit *commit = list->item;
		list = list->next;
		if (commit->object.flags & UNINTERESTING)
			continue;

		*interesting_cache = commit;
		return 0;
	}
	return 1;
}

/*
 * A definition of "relevant" commit that we can use to simplify limited graphs
 * by eliminating side branches.
 *
 * A "relevant" commit is one that is !UNINTERESTING (ie we are including it
 * in our list), or that is a specified BOTTOM commit. Then after computing
 * a limited list, during processing we can generally ignore boundary merges
 * coming from outside the graph, (ie from irrelevant parents), and treat
 * those merges as if they were single-parent. TREESAME is defined to consider
 * only relevant parents, if any. If we are TREESAME to our on-graph parents,
 * we don't care if we were !TREESAME to non-graph parents.
 *
 * Treating bottom commits as relevant ensures that a limited graph's
 * connection to the actual bottom commit is not viewed as a side branch, but
 * treated as part of the graph. For example:
 *
 *   ....Z...A---X---o---o---B
 *        .     /
 *         W---Y
 *
 * When computing "A..B", the A-X connection is at least as important as
 * Y-X, despite A being flagged UNINTERESTING.
 *
 * And when computing --ancestry-path "A..B", the A-X connection is more
 * important than Y-X, despite both A and Y being flagged UNINTERESTING.
 */
static inline int relevant_commit(struct commit *commit)
{
	return (commit->object.flags & (UNINTERESTING | BOTTOM)) != UNINTERESTING;
}

/*
 * Return a single relevant commit from a parent list. If we are a TREESAME
 * commit, and this selects one of our parents, then we can safely simplify to
 * that parent.
 */
static struct commit *one_relevant_parent(const struct rev_info *revs,
					  struct commit_list *orig)
{
	struct commit_list *list = orig;
	struct commit *relevant = NULL;

	if (!orig)
		return NULL;

	/*
	 * For 1-parent commits, or if first-parent-only, then return that
	 * first parent (even if not "relevant" by the above definition).
	 * TREESAME will have been set purely on that parent.
	 */
	if (revs->first_parent_only || !orig->next)
		return orig->item;

	/*
	 * For multi-parent commits, identify a sole relevant parent, if any.
	 * If we have only one relevant parent, then TREESAME will be set purely
	 * with regard to that parent, and we can simplify accordingly.
	 *
	 * If we have more than one relevant parent, or no relevant parents
	 * (and multiple irrelevant ones), then we can't select a parent here
	 * and return NULL.
	 */
	while (list) {
		struct commit *commit = list->item;
		list = list->next;
		if (relevant_commit(commit)) {
			if (relevant)
				return NULL;
			relevant = commit;
		}
	}
	return relevant;
}

/*
 * The goal is to get REV_TREE_NEW as the result only if the
 * diff consists of all '+' (and no other changes), REV_TREE_OLD
 * if the whole diff is removal of old data, and otherwise
 * REV_TREE_DIFFERENT (of course if the trees are the same we
 * want REV_TREE_SAME).
 *
 * The only time we care about the distinction is when
 * remove_empty_trees is in effect, in which case we care only about
 * whether the whole change is REV_TREE_NEW, or if there's another type
 * of change. Which means we can stop the diff early in either of these
 * cases:
 *
 *   1. We're not using remove_empty_trees at all.
 *
 *   2. We saw anything except REV_TREE_NEW.
 */
#define REV_TREE_SAME		0
#define REV_TREE_NEW		1	/* Only new files */
#define REV_TREE_OLD		2	/* Only files removed */
#define REV_TREE_DIFFERENT	3	/* Mixed changes */
static int tree_difference = REV_TREE_SAME;

static void file_add_remove(struct diff_options *options,
		    int addremove,
		    unsigned mode UNUSED,
		    const struct object_id *oid UNUSED,
		    int oid_valid UNUSED,
		    const char *fullpath UNUSED,
		    unsigned dirty_submodule UNUSED)
{
	int diff = addremove == '+' ? REV_TREE_NEW : REV_TREE_OLD;
	struct rev_info *revs = options->change_fn_data;

	tree_difference |= diff;
	if (!revs->remove_empty_trees || tree_difference != REV_TREE_NEW)
		options->flags.has_changes = 1;
}

static void file_change(struct diff_options *options,
		 unsigned old_mode UNUSED,
		 unsigned new_mode UNUSED,
		 const struct object_id *old_oid UNUSED,
		 const struct object_id *new_oid UNUSED,
		 int old_oid_valid UNUSED,
		 int new_oid_valid UNUSED,
		 const char *fullpath UNUSED,
		 unsigned old_dirty_submodule UNUSED,
		 unsigned new_dirty_submodule UNUSED)
{
	tree_difference = REV_TREE_DIFFERENT;
	options->flags.has_changes = 1;
}

static int bloom_filter_atexit_registered;
static unsigned int count_bloom_filter_maybe;
static unsigned int count_bloom_filter_definitely_not;
static unsigned int count_bloom_filter_false_positive;
static unsigned int count_bloom_filter_not_present;

static void trace2_bloom_filter_statistics_atexit(void)
{
	struct json_writer jw = JSON_WRITER_INIT;

	jw_object_begin(&jw, 0);
	jw_object_intmax(&jw, "filter_not_present", count_bloom_filter_not_present);
	jw_object_intmax(&jw, "maybe", count_bloom_filter_maybe);
	jw_object_intmax(&jw, "definitely_not", count_bloom_filter_definitely_not);
	jw_object_intmax(&jw, "false_positive", count_bloom_filter_false_positive);
	jw_end(&jw);

	trace2_data_json("bloom", the_repository, "statistics", &jw);

	jw_release(&jw);
}

static int forbid_bloom_filters(struct pathspec *spec)
{
	if (spec->has_wildcard)
		return 1;
	if (spec->nr > 1)
		return 1;
	if (spec->magic & ~PATHSPEC_LITERAL)
		return 1;
	if (spec->nr && (spec->items[0].magic & ~PATHSPEC_LITERAL))
		return 1;

	return 0;
}

static void prepare_to_use_bloom_filter(struct rev_info *revs)
{
	struct pathspec_item *pi;
	char *path_alloc = NULL;
	const char *path, *p;
	size_t len;
	int path_component_nr = 1;

	if (!revs->commits)
		return;

	if (forbid_bloom_filters(&revs->prune_data))
		return;

	repo_parse_commit(revs->repo, revs->commits->item);

	revs->bloom_filter_settings = get_bloom_filter_settings(revs->repo);
	if (!revs->bloom_filter_settings)
		return;

	if (!revs->pruning.pathspec.nr)
		return;

	pi = &revs->pruning.pathspec.items[0];

	/* remove single trailing slash from path, if needed */
	if (pi->len > 0 && pi->match[pi->len - 1] == '/') {
		path_alloc = xmemdupz(pi->match, pi->len - 1);
		path = path_alloc;
	} else
		path = pi->match;

	len = strlen(path);
	if (!len) {
		revs->bloom_filter_settings = NULL;
		free(path_alloc);
		return;
	}

	p = path;
	while (*p) {
		/*
		 * At this point, the path is normalized to use Unix-style
		 * path separators. This is required due to how the
		 * changed-path Bloom filters store the paths.
		 */
		if (*p == '/')
			path_component_nr++;
		p++;
	}

	revs->bloom_keys_nr = path_component_nr;
	ALLOC_ARRAY(revs->bloom_keys, revs->bloom_keys_nr);

	fill_bloom_key(path, len, &revs->bloom_keys[0],
		       revs->bloom_filter_settings);
	path_component_nr = 1;

	p = path + len - 1;
	while (p > path) {
		if (*p == '/')
			fill_bloom_key(path, p - path,
				       &revs->bloom_keys[path_component_nr++],
				       revs->bloom_filter_settings);
		p--;
	}

	if (trace2_is_enabled() && !bloom_filter_atexit_registered) {
		atexit(trace2_bloom_filter_statistics_atexit);
		bloom_filter_atexit_registered = 1;
	}

	free(path_alloc);
}

static int check_maybe_different_in_bloom_filter(struct rev_info *revs,
						 struct commit *commit)
{
	struct bloom_filter *filter;
	int result = 1, j;

	if (!revs->repo->objects->commit_graph)
		return -1;

	if (commit_graph_generation(commit) == GENERATION_NUMBER_INFINITY)
		return -1;

	filter = get_bloom_filter(revs->repo, commit);

	if (!filter) {
		count_bloom_filter_not_present++;
		return -1;
	}

	for (j = 0; result && j < revs->bloom_keys_nr; j++) {
		result = bloom_filter_contains(filter,
					       &revs->bloom_keys[j],
					       revs->bloom_filter_settings);
	}

	if (result)
		count_bloom_filter_maybe++;
	else
		count_bloom_filter_definitely_not++;

	return result;
}

static int rev_compare_tree(struct rev_info *revs,
			    struct commit *parent, struct commit *commit, int nth_parent)
{
	struct tree *t1 = repo_get_commit_tree(the_repository, parent);
	struct tree *t2 = repo_get_commit_tree(the_repository, commit);
	int bloom_ret = 1;

	if (!t1)
		return REV_TREE_NEW;
	if (!t2)
		return REV_TREE_OLD;

	if (revs->simplify_by_decoration) {
		/*
		 * If we are simplifying by decoration, then the commit
		 * is worth showing if it has a tag pointing at it.
		 */
		if (get_name_decoration(&commit->object))
			return REV_TREE_DIFFERENT;
		/*
		 * A commit that is not pointed by a tag is uninteresting
		 * if we are not limited by path.  This means that you will
		 * see the usual "commits that touch the paths" plus any
		 * tagged commit by specifying both --simplify-by-decoration
		 * and pathspec.
		 */
		if (!revs->prune_data.nr)
			return REV_TREE_SAME;
	}

	if (revs->bloom_keys_nr && !nth_parent) {
		bloom_ret = check_maybe_different_in_bloom_filter(revs, commit);

		if (bloom_ret == 0)
			return REV_TREE_SAME;
	}

	tree_difference = REV_TREE_SAME;
	revs->pruning.flags.has_changes = 0;
	diff_tree_oid(&t1->object.oid, &t2->object.oid, "", &revs->pruning);

	if (!nth_parent)
		if (bloom_ret == 1 && tree_difference == REV_TREE_SAME)
			count_bloom_filter_false_positive++;

	return tree_difference;
}

static int rev_same_tree_as_empty(struct rev_info *revs, struct commit *commit,
				  int nth_parent)
{
	struct tree *t1 = repo_get_commit_tree(the_repository, commit);
	int bloom_ret = -1;

	if (!t1)
		return 0;

	if (!nth_parent && revs->bloom_keys_nr) {
		bloom_ret = check_maybe_different_in_bloom_filter(revs, commit);
		if (!bloom_ret)
			return 1;
	}

	tree_difference = REV_TREE_SAME;
	revs->pruning.flags.has_changes = 0;
	diff_tree_oid(NULL, &t1->object.oid, "", &revs->pruning);

	if (bloom_ret == 1 && tree_difference == REV_TREE_SAME)
		count_bloom_filter_false_positive++;

	return tree_difference == REV_TREE_SAME;
}

struct treesame_state {
	unsigned int nparents;
	unsigned char treesame[FLEX_ARRAY];
};

static struct treesame_state *initialise_treesame(struct rev_info *revs, struct commit *commit)
{
	unsigned n = commit_list_count(commit->parents);
	struct treesame_state *st = xcalloc(1, st_add(sizeof(*st), n));
	st->nparents = n;
	add_decoration(&revs->treesame, &commit->object, st);
	return st;
}

/*
 * Must be called immediately after removing the nth_parent from a commit's
 * parent list, if we are maintaining the per-parent treesame[] decoration.
 * This does not recalculate the master TREESAME flag - update_treesame()
 * should be called to update it after a sequence of treesame[] modifications
 * that may have affected it.
 */
static int compact_treesame(struct rev_info *revs, struct commit *commit, unsigned nth_parent)
{
	struct treesame_state *st;
	int old_same;

	if (!commit->parents) {
		/*
		 * Have just removed the only parent from a non-merge.
		 * Different handling, as we lack decoration.
		 */
		if (nth_parent != 0)
			die("compact_treesame %u", nth_parent);
		old_same = !!(commit->object.flags & TREESAME);
		if (rev_same_tree_as_empty(revs, commit, nth_parent))
			commit->object.flags |= TREESAME;
		else
			commit->object.flags &= ~TREESAME;
		return old_same;
	}

	st = lookup_decoration(&revs->treesame, &commit->object);
	if (!st || nth_parent >= st->nparents)
		die("compact_treesame %u", nth_parent);

	old_same = st->treesame[nth_parent];
	memmove(st->treesame + nth_parent,
		st->treesame + nth_parent + 1,
		st->nparents - nth_parent - 1);

	/*
	 * If we've just become a non-merge commit, update TREESAME
	 * immediately, and remove the no-longer-needed decoration.
	 * If still a merge, defer update until update_treesame().
	 */
	if (--st->nparents == 1) {
		if (commit->parents->next)
			die("compact_treesame parents mismatch");
		if (st->treesame[0] && revs->dense)
			commit->object.flags |= TREESAME;
		else
			commit->object.flags &= ~TREESAME;
		free(add_decoration(&revs->treesame, &commit->object, NULL));
	}

	return old_same;
}

static unsigned update_treesame(struct rev_info *revs, struct commit *commit)
{
	if (commit->parents && commit->parents->next) {
		unsigned n;
		struct treesame_state *st;
		struct commit_list *p;
		unsigned relevant_parents;
		unsigned relevant_change, irrelevant_change;

		st = lookup_decoration(&revs->treesame, &commit->object);
		if (!st)
			die("update_treesame %s", oid_to_hex(&commit->object.oid));
		relevant_parents = 0;
		relevant_change = irrelevant_change = 0;
		for (p = commit->parents, n = 0; p; n++, p = p->next) {
			if (relevant_commit(p->item)) {
				relevant_change |= !st->treesame[n];
				relevant_parents++;
			} else
				irrelevant_change |= !st->treesame[n];
		}
		if (relevant_parents ? relevant_change : irrelevant_change)
			commit->object.flags &= ~TREESAME;
		else
			commit->object.flags |= TREESAME;
	}

	return commit->object.flags & TREESAME;
}

static inline int limiting_can_increase_treesame(const struct rev_info *revs)
{
	/*
	 * TREESAME is irrelevant unless prune && dense;
	 * if simplify_history is set, we can't have a mixture of TREESAME and
	 *    !TREESAME INTERESTING parents (and we don't have treesame[]
	 *    decoration anyway);
	 * if first_parent_only is set, then the TREESAME flag is locked
	 *    against the first parent (and again we lack treesame[] decoration).
	 */
	return revs->prune && revs->dense &&
	       !revs->simplify_history &&
	       !revs->first_parent_only;
}

static void try_to_simplify_commit(struct rev_info *revs, struct commit *commit)
{
	struct commit_list **pp, *parent;
	struct treesame_state *ts = NULL;
	int relevant_change = 0, irrelevant_change = 0;
	int relevant_parents, nth_parent;

	/*
	 * If we don't do pruning, everything is interesting
	 */
	if (!revs->prune)
		return;

	if (!repo_get_commit_tree(the_repository, commit))
		return;

	if (!commit->parents) {
		/*
		 * Pretend as if we are comparing ourselves to the
		 * (non-existent) first parent of this commit object. Even
		 * though no such parent exists, its changed-path Bloom filter
		 * (if one exists) is relative to the empty tree, using Bloom
		 * filters is allowed here.
		 */
		if (rev_same_tree_as_empty(revs, commit, 0))
			commit->object.flags |= TREESAME;
		return;
	}

	/*
	 * Normal non-merge commit? If we don't want to make the
	 * history dense, we consider it always to be a change..
	 */
	if (!revs->dense && !commit->parents->next)
		return;

	for (pp = &commit->parents, nth_parent = 0, relevant_parents = 0;
	     (parent = *pp) != NULL;
	     pp = &parent->next, nth_parent++) {
		struct commit *p = parent->item;
		if (relevant_commit(p))
			relevant_parents++;

		if (nth_parent == 1) {
			/*
			 * This our second loop iteration - so we now know
			 * we're dealing with a merge.
			 *
			 * Do not compare with later parents when we care only about
			 * the first parent chain, in order to avoid derailing the
			 * traversal to follow a side branch that brought everything
			 * in the path we are limited to by the pathspec.
			 */
			if (revs->first_parent_only)
				break;
			/*
			 * If this will remain a potentially-simplifiable
			 * merge, remember per-parent treesame if needed.
			 * Initialise the array with the comparison from our
			 * first iteration.
			 */
			if (revs->treesame.name &&
			    !revs->simplify_history &&
			    !(commit->object.flags & UNINTERESTING)) {
				ts = initialise_treesame(revs, commit);
				if (!(irrelevant_change || relevant_change))
					ts->treesame[0] = 1;
			}
		}
		if (repo_parse_commit(revs->repo, p) < 0)
			die("cannot simplify commit %s (because of %s)",
			    oid_to_hex(&commit->object.oid),
			    oid_to_hex(&p->object.oid));
		switch (rev_compare_tree(revs, p, commit, nth_parent)) {
		case REV_TREE_SAME:
			if (!revs->simplify_history || !relevant_commit(p)) {
				/* Even if a merge with an uninteresting
				 * side branch brought the entire change
				 * we are interested in, we do not want
				 * to lose the other branches of this
				 * merge, so we just keep going.
				 */
				if (ts)
					ts->treesame[nth_parent] = 1;
				continue;
			}

			free_commit_list(parent->next);
			parent->next = NULL;
			while (commit->parents != parent)
				pop_commit(&commit->parents);
			commit->parents = parent;

			/*
			 * A merge commit is a "diversion" if it is not
			 * TREESAME to its first parent but is TREESAME
			 * to a later parent. In the simplified history,
			 * we "divert" the history walk to the later
			 * parent. These commits are shown when "show_pulls"
			 * is enabled, so do not mark the object as
			 * TREESAME here.
			 */
			if (!revs->show_pulls || !nth_parent)
				commit->object.flags |= TREESAME;

			return;

		case REV_TREE_NEW:
			if (revs->remove_empty_trees &&
			    rev_same_tree_as_empty(revs, p, nth_parent)) {
				/* We are adding all the specified
				 * paths from this parent, so the
				 * history beyond this parent is not
				 * interesting.  Remove its parents
				 * (they are grandparents for us).
				 * IOW, we pretend this parent is a
				 * "root" commit.
				 */
				if (repo_parse_commit(revs->repo, p) < 0)
					die("cannot simplify commit %s (invalid %s)",
					    oid_to_hex(&commit->object.oid),
					    oid_to_hex(&p->object.oid));
				free_commit_list(p->parents);
				p->parents = NULL;
			}
		/* fallthrough */
		case REV_TREE_OLD:
		case REV_TREE_DIFFERENT:
			if (relevant_commit(p))
				relevant_change = 1;
			else
				irrelevant_change = 1;

			if (!nth_parent)
				commit->object.flags |= PULL_MERGE;

			continue;
		}
		die("bad tree compare for commit %s", oid_to_hex(&commit->object.oid));
	}

	/*
	 * TREESAME is straightforward for single-parent commits. For merge
	 * commits, it is most useful to define it so that "irrelevant"
	 * parents cannot make us !TREESAME - if we have any relevant
	 * parents, then we only consider TREESAMEness with respect to them,
	 * allowing irrelevant merges from uninteresting branches to be
	 * simplified away. Only if we have only irrelevant parents do we
	 * base TREESAME on them. Note that this logic is replicated in
	 * update_treesame, which should be kept in sync.
	 */
	if (relevant_parents ? !relevant_change : !irrelevant_change)
		commit->object.flags |= TREESAME;
}

static int process_parents(struct rev_info *revs, struct commit *commit,
			   struct commit_list **list, struct prio_queue *queue)
{
	struct commit_list *parent = commit->parents;
	unsigned pass_flags;

	if (commit->object.flags & ADDED)
		return 0;
	if (revs->do_not_die_on_missing_objects &&
	    oidset_contains(&revs->missing_commits, &commit->object.oid))
		return 0;
	commit->object.flags |= ADDED;

	if (revs->include_check &&
	    !revs->include_check(commit, revs->include_check_data))
		return 0;

	/*
	 * If the commit is uninteresting, don't try to
	 * prune parents - we want the maximal uninteresting
	 * set.
	 *
	 * Normally we haven't parsed the parent
	 * yet, so we won't have a parent of a parent
	 * here. However, it may turn out that we've
	 * reached this commit some other way (where it
	 * wasn't uninteresting), in which case we need
	 * to mark its parents recursively too..
	 */
	if (commit->object.flags & UNINTERESTING) {
		while (parent) {
			struct commit *p = parent->item;
			parent = parent->next;
			if (p)
				p->object.flags |= UNINTERESTING;
			if (repo_parse_commit_gently(revs->repo, p, 1) < 0)
				continue;
			if (p->parents)
				mark_parents_uninteresting(revs, p);
			if (p->object.flags & SEEN)
				continue;
			p->object.flags |= (SEEN | NOT_USER_GIVEN);
			if (list)
				commit_list_insert_by_date(p, list);
			if (queue)
				prio_queue_put(queue, p);
			if (revs->exclude_first_parent_only)
				break;
		}
		return 0;
	}

	/*
	 * Ok, the commit wasn't uninteresting. Try to
	 * simplify the commit history and find the parent
	 * that has no differences in the path set if one exists.
	 */
	try_to_simplify_commit(revs, commit);

	if (revs->no_walk)
		return 0;

	pass_flags = (commit->object.flags & (SYMMETRIC_LEFT | ANCESTRY_PATH));

	for (parent = commit->parents; parent; parent = parent->next) {
		struct commit *p = parent->item;
		int gently = revs->ignore_missing_links ||
			     revs->exclude_promisor_objects ||
			     revs->do_not_die_on_missing_objects;
		if (repo_parse_commit_gently(revs->repo, p, gently) < 0) {
			if (revs->exclude_promisor_objects &&
			    is_promisor_object(revs->repo, &p->object.oid)) {
				if (revs->first_parent_only)
					break;
				continue;
			}

			if (revs->do_not_die_on_missing_objects)
				oidset_insert(&revs->missing_commits, &p->object.oid);
			else
				return -1; /* corrupt repository */
		}
		if (revs->sources) {
			char **slot = revision_sources_at(revs->sources, p);

			if (!*slot)
				*slot = *revision_sources_at(revs->sources, commit);
		}
		p->object.flags |= pass_flags;
		if (!(p->object.flags & SEEN)) {
			p->object.flags |= (SEEN | NOT_USER_GIVEN);
			if (list)
				commit_list_insert_by_date(p, list);
			if (queue)
				prio_queue_put(queue, p);
		}
		if (revs->first_parent_only)
			break;
	}
	return 0;
}

static void cherry_pick_list(struct commit_list *list, struct rev_info *revs)
{
	struct commit_list *p;
	int left_count = 0, right_count = 0;
	int left_first;
	struct patch_ids ids;
	unsigned cherry_flag;

	/* First count the commits on the left and on the right */
	for (p = list; p; p = p->next) {
		struct commit *commit = p->item;
		unsigned flags = commit->object.flags;
		if (flags & BOUNDARY)
			;
		else if (flags & SYMMETRIC_LEFT)
			left_count++;
		else
			right_count++;
	}

	if (!left_count || !right_count)
		return;

	left_first = left_count < right_count;
	init_patch_ids(revs->repo, &ids);
	ids.diffopts.pathspec = revs->diffopt.pathspec;

	/* Compute patch-ids for one side */
	for (p = list; p; p = p->next) {
		struct commit *commit = p->item;
		unsigned flags = commit->object.flags;

		if (flags & BOUNDARY)
			continue;
		/*
		 * If we have fewer left, left_first is set and we omit
		 * commits on the right branch in this loop.  If we have
		 * fewer right, we skip the left ones.
		 */
		if (left_first != !!(flags & SYMMETRIC_LEFT))
			continue;
		add_commit_patch_id(commit, &ids);
	}

	/* either cherry_mark or cherry_pick are true */
	cherry_flag = revs->cherry_mark ? PATCHSAME : SHOWN;

	/* Check the other side */
	for (p = list; p; p = p->next) {
		struct commit *commit = p->item;
		struct patch_id *id;
		unsigned flags = commit->object.flags;

		if (flags & BOUNDARY)
			continue;
		/*
		 * If we have fewer left, left_first is set and we omit
		 * commits on the left branch in this loop.
		 */
		if (left_first == !!(flags & SYMMETRIC_LEFT))
			continue;

		/*
		 * Have we seen the same patch id?
		 */
		id = patch_id_iter_first(commit, &ids);
		if (!id)
			continue;

		commit->object.flags |= cherry_flag;
		do {
			id->commit->object.flags |= cherry_flag;
		} while ((id = patch_id_iter_next(id, &ids)));
	}

	free_patch_ids(&ids);
}

/* How many extra uninteresting commits we want to see.. */
#define SLOP 5

static int still_interesting(struct commit_list *src, timestamp_t date, int slop,
			     struct commit **interesting_cache)
{
	/*
	 * No source list at all? We're definitely done..
	 */
	if (!src)
		return 0;

	/*
	 * Does the destination list contain entries with a date
	 * before the source list? Definitely _not_ done.
	 */
	if (date <= src->item->date)
		return SLOP;

	/*
	 * Does the source list still have interesting commits in
	 * it? Definitely not done..
	 */
	if (!everybody_uninteresting(src, interesting_cache))
		return SLOP;

	/* Ok, we're closing in.. */
	return slop-1;
}

/*
 * "rev-list --ancestry-path=C_0 [--ancestry-path=C_1 ...] A..B"
 * computes commits that are ancestors of B but not ancestors of A but
 * further limits the result to those that have any of C in their
 * ancestry path (i.e. are either ancestors of any of C, descendants
 * of any of C, or are any of C). If --ancestry-path is specified with
 * no commit, we use all bottom commits for C.
 *
 * Before this function is called, ancestors of C will have already
 * been marked with ANCESTRY_PATH previously.
 *
 * This takes the list of bottom commits and the result of "A..B"
 * without --ancestry-path, and limits the latter further to the ones
 * that have any of C in their ancestry path. Since the ancestors of C
 * have already been marked (a prerequisite of this function), we just
 * need to mark the descendants, then exclude any commit that does not
 * have any of these marks.
 */
static void limit_to_ancestry(struct commit_list *bottoms, struct commit_list *list)
{
	struct commit_list *p;
	struct commit_list *rlist = NULL;
	int made_progress;

	/*
	 * Reverse the list so that it will be likely that we would
	 * process parents before children.
	 */
	for (p = list; p; p = p->next)
		commit_list_insert(p->item, &rlist);

	for (p = bottoms; p; p = p->next)
		p->item->object.flags |= TMP_MARK;

	/*
	 * Mark the ones that can reach bottom commits in "list",
	 * in a bottom-up fashion.
	 */
	do {
		made_progress = 0;
		for (p = rlist; p; p = p->next) {
			struct commit *c = p->item;
			struct commit_list *parents;
			if (c->object.flags & (TMP_MARK | UNINTERESTING))
				continue;
			for (parents = c->parents;
			     parents;
			     parents = parents->next) {
				if (!(parents->item->object.flags & TMP_MARK))
					continue;
				c->object.flags |= TMP_MARK;
				made_progress = 1;
				break;
			}
		}
	} while (made_progress);

	/*
	 * NEEDSWORK: decide if we want to remove parents that are
	 * not marked with TMP_MARK from commit->parents for commits
	 * in the resulting list.  We may not want to do that, though.
	 */

	/*
	 * The ones that are not marked with either TMP_MARK or
	 * ANCESTRY_PATH are uninteresting
	 */
	for (p = list; p; p = p->next) {
		struct commit *c = p->item;
		if (c->object.flags & (TMP_MARK | ANCESTRY_PATH))
			continue;
		c->object.flags |= UNINTERESTING;
	}

	/* We are done with TMP_MARK and ANCESTRY_PATH */
	for (p = list; p; p = p->next)
		p->item->object.flags &= ~(TMP_MARK | ANCESTRY_PATH);
	for (p = bottoms; p; p = p->next)
		p->item->object.flags &= ~(TMP_MARK | ANCESTRY_PATH);
	free_commit_list(rlist);
}

/*
 * Before walking the history, add the set of "negative" refs the
 * caller has asked to exclude to the bottom list.
 *
 * This is used to compute "rev-list --ancestry-path A..B", as we need
 * to filter the result of "A..B" further to the ones that can actually
 * reach A.
 */
static void collect_bottom_commits(struct commit_list *list,
				   struct commit_list **bottom)
{
	struct commit_list *elem;
	for (elem = list; elem; elem = elem->next)
		if (elem->item->object.flags & BOTTOM)
			commit_list_insert(elem->item, bottom);
}

/* Assumes either left_only or right_only is set */
static void limit_left_right(struct commit_list *list, struct rev_info *revs)
{
	struct commit_list *p;

	for (p = list; p; p = p->next) {
		struct commit *commit = p->item;

		if (revs->right_only) {
			if (commit->object.flags & SYMMETRIC_LEFT)
				commit->object.flags |= SHOWN;
		} else	/* revs->left_only is set */
			if (!(commit->object.flags & SYMMETRIC_LEFT))
				commit->object.flags |= SHOWN;
	}
}

static int limit_list(struct rev_info *revs)
{
	int slop = SLOP;
	timestamp_t date = TIME_MAX;
	struct commit_list *original_list = revs->commits;
	struct commit_list *newlist = NULL;
	struct commit_list **p = &newlist;
	struct commit *interesting_cache = NULL;

	if (revs->ancestry_path_implicit_bottoms) {
		collect_bottom_commits(original_list,
				       &revs->ancestry_path_bottoms);
		if (!revs->ancestry_path_bottoms)
			die("--ancestry-path given but there are no bottom commits");
	}

	while (original_list) {
		struct commit *commit = pop_commit(&original_list);
		struct object *obj = &commit->object;
		show_early_output_fn_t show;

		if (commit == interesting_cache)
			interesting_cache = NULL;

		if (revs->max_age != -1 && (commit->date < revs->max_age))
			obj->flags |= UNINTERESTING;
		if (process_parents(revs, commit, &original_list, NULL) < 0)
			return -1;
		if (obj->flags & UNINTERESTING) {
			mark_parents_uninteresting(revs, commit);
			slop = still_interesting(original_list, date, slop, &interesting_cache);
			if (slop)
				continue;
			break;
		}
		if (revs->min_age != -1 && (commit->date > revs->min_age) &&
		    !revs->line_level_traverse)
			continue;
		if (revs->max_age_as_filter != -1 &&
			(commit->date < revs->max_age_as_filter) && !revs->line_level_traverse)
			continue;
		date = commit->date;
		p = &commit_list_insert(commit, p)->next;

		show = show_early_output;
		if (!show)
			continue;

		show(revs, newlist);
		show_early_output = NULL;
	}
	if (revs->cherry_pick || revs->cherry_mark)
		cherry_pick_list(newlist, revs);

	if (revs->left_only || revs->right_only)
		limit_left_right(newlist, revs);

	if (revs->ancestry_path)
		limit_to_ancestry(revs->ancestry_path_bottoms, newlist);

	/*
	 * Check if any commits have become TREESAME by some of their parents
	 * becoming UNINTERESTING.
	 */
	if (limiting_can_increase_treesame(revs)) {
		struct commit_list *list = NULL;
		for (list = newlist; list; list = list->next) {
			struct commit *c = list->item;
			if (c->object.flags & (UNINTERESTING | TREESAME))
				continue;
			update_treesame(revs, c);
		}
	}

	free_commit_list(original_list);
	revs->commits = newlist;
	return 0;
}

/*
 * Add an entry to refs->cmdline with the specified information.
 * *name is copied.
 */
static void add_rev_cmdline(struct rev_info *revs,
			    struct object *item,
			    const char *name,
			    int whence,
			    unsigned flags)
{
	struct rev_cmdline_info *info = &revs->cmdline;
	unsigned int nr = info->nr;

	ALLOC_GROW(info->rev, nr + 1, info->alloc);
	info->rev[nr].item = item;
	info->rev[nr].name = xstrdup(name);
	info->rev[nr].whence = whence;
	info->rev[nr].flags = flags;
	info->nr++;
}

static void add_rev_cmdline_list(struct rev_info *revs,
				 struct commit_list *commit_list,
				 int whence,
				 unsigned flags)
{
	while (commit_list) {
		struct object *object = &commit_list->item->object;
		add_rev_cmdline(revs, object, oid_to_hex(&object->oid),
				whence, flags);
		commit_list = commit_list->next;
	}
}

int ref_excluded(const struct ref_exclusions *exclusions, const char *path)
{
	const char *stripped_path = strip_namespace(path);
	struct string_list_item *item;

	for_each_string_list_item(item, &exclusions->excluded_refs) {
		if (!wildmatch(item->string, path, 0))
			return 1;
	}

	if (ref_is_hidden(stripped_path, path, &exclusions->hidden_refs))
		return 1;

	return 0;
}

void init_ref_exclusions(struct ref_exclusions *exclusions)
{
	struct ref_exclusions blank = REF_EXCLUSIONS_INIT;
	memcpy(exclusions, &blank, sizeof(*exclusions));
}

void clear_ref_exclusions(struct ref_exclusions *exclusions)
{
	string_list_clear(&exclusions->excluded_refs, 0);
	strvec_clear(&exclusions->hidden_refs);
	exclusions->hidden_refs_configured = 0;
}

void add_ref_exclusion(struct ref_exclusions *exclusions, const char *exclude)
{
	string_list_append(&exclusions->excluded_refs, exclude);
}

struct exclude_hidden_refs_cb {
	struct ref_exclusions *exclusions;
	const char *section;
};

static int hide_refs_config(const char *var, const char *value,
			    const struct config_context *ctx UNUSED,
			    void *cb_data)
{
	struct exclude_hidden_refs_cb *cb = cb_data;
	cb->exclusions->hidden_refs_configured = 1;
	return parse_hide_refs_config(var, value, cb->section,
				      &cb->exclusions->hidden_refs);
}

void exclude_hidden_refs(struct ref_exclusions *exclusions, const char *section)
{
	struct exclude_hidden_refs_cb cb;

	if (strcmp(section, "fetch") && strcmp(section, "receive") &&
			strcmp(section, "uploadpack"))
		die(_("unsupported section for hidden refs: %s"), section);

	if (exclusions->hidden_refs_configured)
		die(_("--exclude-hidden= passed more than once"));

	cb.exclusions = exclusions;
	cb.section = section;

	git_config(hide_refs_config, &cb);
}

struct all_refs_cb {
	int all_flags;
	int warned_bad_reflog;
	struct rev_info *all_revs;
	const char *name_for_errormsg;
	struct worktree *wt;
};

static int handle_one_ref(const char *path, const char *referent UNUSED, const struct object_id *oid,
			  int flag UNUSED,
			  void *cb_data)
{
	struct all_refs_cb *cb = cb_data;
	struct object *object;

	if (ref_excluded(&cb->all_revs->ref_excludes, path))
	    return 0;

	object = get_reference(cb->all_revs, path, oid, cb->all_flags);
	add_rev_cmdline(cb->all_revs, object, path, REV_CMD_REF, cb->all_flags);
	add_pending_object(cb->all_revs, object, path);
	return 0;
}

static void init_all_refs_cb(struct all_refs_cb *cb, struct rev_info *revs,
	unsigned flags)
{
	cb->all_revs = revs;
	cb->all_flags = flags;
	revs->rev_input_given = 1;
	cb->wt = NULL;
}

static void handle_refs(struct ref_store *refs,
			struct rev_info *revs, unsigned flags,
			int (*for_each)(struct ref_store *, each_ref_fn, void *))
{
	struct all_refs_cb cb;

	if (!refs) {
		/* this could happen with uninitialized submodules */
		return;
	}

	init_all_refs_cb(&cb, revs, flags);
	for_each(refs, handle_one_ref, &cb);
}

static void handle_one_reflog_commit(struct object_id *oid, void *cb_data)
{
	struct all_refs_cb *cb = cb_data;
	if (!is_null_oid(oid)) {
		struct object *o = parse_object(cb->all_revs->repo, oid);
		if (o) {
			o->flags |= cb->all_flags;
			/* ??? CMDLINEFLAGS ??? */
			add_pending_object(cb->all_revs, o, "");
		}
		else if (!cb->warned_bad_reflog) {
			warning("reflog of '%s' references pruned commits",
				cb->name_for_errormsg);
			cb->warned_bad_reflog = 1;
		}
	}
}

static int handle_one_reflog_ent(struct object_id *ooid, struct object_id *noid,
				 const char *email UNUSED,
				 timestamp_t timestamp UNUSED,
				 int tz UNUSED,
				 const char *message UNUSED,
				 void *cb_data)
{
	handle_one_reflog_commit(ooid, cb_data);
	handle_one_reflog_commit(noid, cb_data);
	return 0;
}

static int handle_one_reflog(const char *refname_in_wt, void *cb_data)
{
	struct all_refs_cb *cb = cb_data;
	struct strbuf refname = STRBUF_INIT;

	cb->warned_bad_reflog = 0;
	strbuf_worktree_ref(cb->wt, &refname, refname_in_wt);
	cb->name_for_errormsg = refname.buf;
	refs_for_each_reflog_ent(get_main_ref_store(the_repository),
				 refname.buf,
				 handle_one_reflog_ent, cb_data);
	strbuf_release(&refname);
	return 0;
}

static void add_other_reflogs_to_pending(struct all_refs_cb *cb)
{
	struct worktree **worktrees, **p;

	worktrees = get_worktrees();
	for (p = worktrees; *p; p++) {
		struct worktree *wt = *p;

		if (wt->is_current)
			continue;

		cb->wt = wt;
		refs_for_each_reflog(get_worktree_ref_store(wt),
				     handle_one_reflog,
				     cb);
	}
	free_worktrees(worktrees);
}

void add_reflogs_to_pending(struct rev_info *revs, unsigned flags)
{
	struct all_refs_cb cb;

	cb.all_revs = revs;
	cb.all_flags = flags;
	cb.wt = NULL;
	refs_for_each_reflog(get_main_ref_store(the_repository),
			     handle_one_reflog, &cb);

	if (!revs->single_worktree)
		add_other_reflogs_to_pending(&cb);
}

static void add_cache_tree(struct cache_tree *it, struct rev_info *revs,
			   struct strbuf *path, unsigned int flags)
{
	size_t baselen = path->len;
	int i;

	if (it->entry_count >= 0) {
		struct tree *tree = lookup_tree(revs->repo, &it->oid);
		tree->object.flags |= flags;
		add_pending_object_with_path(revs, &tree->object, "",
					     040000, path->buf);
	}

	for (i = 0; i < it->subtree_nr; i++) {
		struct cache_tree_sub *sub = it->down[i];
		strbuf_addf(path, "%s%s", baselen ? "/" : "", sub->name);
		add_cache_tree(sub->cache_tree, revs, path, flags);
		strbuf_setlen(path, baselen);
	}

}

static void add_resolve_undo_to_pending(struct index_state *istate, struct rev_info *revs)
{
	struct string_list_item *item;
	struct string_list *resolve_undo = istate->resolve_undo;

	if (!resolve_undo)
		return;

	for_each_string_list_item(item, resolve_undo) {
		const char *path = item->string;
		struct resolve_undo_info *ru = item->util;
		int i;

		if (!ru)
			continue;
		for (i = 0; i < 3; i++) {
			struct blob *blob;

			if (!ru->mode[i] || !S_ISREG(ru->mode[i]))
				continue;

			blob = lookup_blob(revs->repo, &ru->oid[i]);
			if (!blob) {
				warning(_("resolve-undo records `%s` which is missing"),
					oid_to_hex(&ru->oid[i]));
				continue;
			}
			add_pending_object_with_path(revs, &blob->object, "",
						     ru->mode[i], path);
		}
	}
}

static void do_add_index_objects_to_pending(struct rev_info *revs,
					    struct index_state *istate,
					    unsigned int flags)
{
	int i;

	/* TODO: audit for interaction with sparse-index. */
	ensure_full_index(istate);
	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		struct blob *blob;

		if (S_ISGITLINK(ce->ce_mode))
			continue;

		blob = lookup_blob(revs->repo, &ce->oid);
		if (!blob)
			die("unable to add index blob to traversal");
		blob->object.flags |= flags;
		add_pending_object_with_path(revs, &blob->object, "",
					     ce->ce_mode, ce->name);
	}

	if (istate->cache_tree) {
		struct strbuf path = STRBUF_INIT;
		add_cache_tree(istate->cache_tree, revs, &path, flags);
		strbuf_release(&path);
	}

	add_resolve_undo_to_pending(istate, revs);
}

void add_index_objects_to_pending(struct rev_info *revs, unsigned int flags)
{
	struct worktree **worktrees, **p;

	repo_read_index(revs->repo);
	do_add_index_objects_to_pending(revs, revs->repo->index, flags);

	if (revs->single_worktree)
		return;

	worktrees = get_worktrees();
	for (p = worktrees; *p; p++) {
		struct worktree *wt = *p;
		struct index_state istate = INDEX_STATE_INIT(revs->repo);
		char *wt_gitdir;

		if (wt->is_current)
			continue; /* current index already taken care of */

		wt_gitdir = get_worktree_git_dir(wt);

		if (read_index_from(&istate,
				    worktree_git_path(the_repository, wt, "index"),
				    wt_gitdir) > 0)
			do_add_index_objects_to_pending(revs, &istate, flags);

		discard_index(&istate);
		free(wt_gitdir);
	}
	free_worktrees(worktrees);
}

struct add_alternate_refs_data {
	struct rev_info *revs;
	unsigned int flags;
};

static void add_one_alternate_ref(const struct object_id *oid,
				  void *vdata)
{
	const char *name = ".alternate";
	struct add_alternate_refs_data *data = vdata;
	struct object *obj;

	obj = get_reference(data->revs, name, oid, data->flags);
	add_rev_cmdline(data->revs, obj, name, REV_CMD_REV, data->flags);
	add_pending_object(data->revs, obj, name);
}

static void add_alternate_refs_to_pending(struct rev_info *revs,
					  unsigned int flags)
{
	struct add_alternate_refs_data data;
	data.revs = revs;
	data.flags = flags;
	odb_for_each_alternate_ref(the_repository->objects,
				   add_one_alternate_ref, &data);
}

static int add_parents_only(struct rev_info *revs, const char *arg_, int flags,
			    int exclude_parent)
{
	struct object_id oid;
	struct object *it;
	struct commit *commit;
	struct commit_list *parents;
	int parent_number;
	const char *arg = arg_;

	if (*arg == '^') {
		flags ^= UNINTERESTING | BOTTOM;
		arg++;
	}
	if (repo_get_oid_committish(the_repository, arg, &oid))
		return 0;
	while (1) {
		it = get_reference(revs, arg, &oid, 0);
		if (!it && revs->ignore_missing)
			return 0;
		if (it->type != OBJ_TAG)
			break;
		if (!((struct tag*)it)->tagged)
			return 0;
		oidcpy(&oid, &((struct tag*)it)->tagged->oid);
	}
	if (it->type != OBJ_COMMIT)
		return 0;
	commit = (struct commit *)it;
	if (exclude_parent &&
	    exclude_parent > commit_list_count(commit->parents))
		return 0;
	for (parents = commit->parents, parent_number = 1;
	     parents;
	     parents = parents->next, parent_number++) {
		if (exclude_parent && parent_number != exclude_parent)
			continue;

		it = &parents->item->object;
		it->flags |= flags;
		add_rev_cmdline(revs, it, arg_, REV_CMD_PARENTS_ONLY, flags);
		add_pending_object(revs, it, arg);
	}
	return 1;
}

void repo_init_revisions(struct repository *r,
			 struct rev_info *revs,
			 const char *prefix)
{
	struct rev_info blank = REV_INFO_INIT;
	memcpy(revs, &blank, sizeof(*revs));

	revs->repo = r;
	revs->pruning.repo = r;
	revs->pruning.add_remove = file_add_remove;
	revs->pruning.change = file_change;
	revs->pruning.change_fn_data = revs;
	revs->prefix = prefix;

	grep_init(&revs->grep_filter, revs->repo);
	revs->grep_filter.status_only = 1;

	repo_diff_setup(revs->repo, &revs->diffopt);
	if (prefix && !revs->diffopt.prefix) {
		revs->diffopt.prefix = prefix;
		revs->diffopt.prefix_length = strlen(prefix);
	}

	init_display_notes(&revs->notes_opt);
	list_objects_filter_init(&revs->filter);
	init_ref_exclusions(&revs->ref_excludes);
	oidset_init(&revs->missing_commits, 0);
}

static void add_pending_commit_list(struct rev_info *revs,
				    struct commit_list *commit_list,
				    unsigned int flags)
{
	while (commit_list) {
		struct object *object = &commit_list->item->object;
		object->flags |= flags;
		add_pending_object(revs, object, oid_to_hex(&object->oid));
		commit_list = commit_list->next;
	}
}

static const char *lookup_other_head(struct object_id *oid)
{
	int i;
	static const char *const other_head[] = {
		"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "REBASE_HEAD"
	};

	for (i = 0; i < ARRAY_SIZE(other_head); i++)
		if (!refs_read_ref_full(get_main_ref_store(the_repository), other_head[i],
					RESOLVE_REF_READING | RESOLVE_REF_NO_RECURSE,
					oid, NULL)) {
			if (is_null_oid(oid))
				die(_("%s exists but is a symbolic ref"), other_head[i]);
			return other_head[i];
		}

	die(_("--merge requires one of the pseudorefs MERGE_HEAD, CHERRY_PICK_HEAD, REVERT_HEAD or REBASE_HEAD"));
}

static void prepare_show_merge(struct rev_info *revs)
{
	struct commit_list *bases = NULL;
	struct commit *head, *other;
	struct object_id oid;
	const char *other_name;
	const char **prune = NULL;
	int i, prune_num = 1; /* counting terminating NULL */
	struct index_state *istate = revs->repo->index;

	if (repo_get_oid(the_repository, "HEAD", &oid))
		die("--merge without HEAD?");
	head = lookup_commit_or_die(&oid, "HEAD");
	other_name = lookup_other_head(&oid);
	other = lookup_commit_or_die(&oid, other_name);
	add_pending_object(revs, &head->object, "HEAD");
	add_pending_object(revs, &other->object, other_name);
	if (repo_get_merge_bases(the_repository, head, other, &bases) < 0)
		exit(128);
	add_rev_cmdline_list(revs, bases, REV_CMD_MERGE_BASE, UNINTERESTING | BOTTOM);
	add_pending_commit_list(revs, bases, UNINTERESTING | BOTTOM);
	free_commit_list(bases);
	head->object.flags |= SYMMETRIC_LEFT;

	if (!istate->cache_nr)
		repo_read_index(revs->repo);
	for (i = 0; i < istate->cache_nr; i++) {
		const struct cache_entry *ce = istate->cache[i];
		if (!ce_stage(ce))
			continue;
		if (ce_path_match(istate, ce, &revs->prune_data, NULL)) {
			prune_num++;
			REALLOC_ARRAY(prune, prune_num);
			prune[prune_num-2] = ce->name;
			prune[prune_num-1] = NULL;
		}
		while ((i+1 < istate->cache_nr) &&
		       ce_same_name(ce, istate->cache[i+1]))
			i++;
	}
	clear_pathspec(&revs->prune_data);
	parse_pathspec(&revs->prune_data, PATHSPEC_ALL_MAGIC & ~PATHSPEC_LITERAL,
		       PATHSPEC_PREFER_FULL | PATHSPEC_LITERAL_PATH, "", prune);
	revs->limited = 1;
	free(prune);
}

static int dotdot_missing(const char *arg, char *dotdot,
			  struct rev_info *revs, int symmetric)
{
	if (revs->ignore_missing)
		return 0;
	/* de-munge so we report the full argument */
	*dotdot = '.';
	die(symmetric
	    ? "Invalid symmetric difference expression %s"
	    : "Invalid revision range %s", arg);
}

static int handle_dotdot_1(const char *arg, char *dotdot,
			   struct rev_info *revs, int flags,
			   int cant_be_filename,
			   struct object_context *a_oc,
			   struct object_context *b_oc)
{
	const char *a_name, *b_name;
	struct object_id a_oid, b_oid;
	struct object *a_obj, *b_obj;
	unsigned int a_flags, b_flags;
	int symmetric = 0;
	unsigned int flags_exclude = flags ^ (UNINTERESTING | BOTTOM);
	unsigned int oc_flags = GET_OID_COMMITTISH | GET_OID_RECORD_PATH;

	a_name = arg;
	if (!*a_name)
		a_name = "HEAD";

	b_name = dotdot + 2;
	if (*b_name == '.') {
		symmetric = 1;
		b_name++;
	}
	if (!*b_name)
		b_name = "HEAD";

	if (get_oid_with_context(revs->repo, a_name, oc_flags, &a_oid, a_oc) ||
	    get_oid_with_context(revs->repo, b_name, oc_flags, &b_oid, b_oc))
		return -1;

	if (!cant_be_filename) {
		*dotdot = '.';
		verify_non_filename(revs->prefix, arg);
		*dotdot = '\0';
	}

	a_obj = parse_object(revs->repo, &a_oid);
	b_obj = parse_object(revs->repo, &b_oid);
	if (!a_obj || !b_obj)
		return dotdot_missing(arg, dotdot, revs, symmetric);

	if (!symmetric) {
		/* just A..B */
		b_flags = flags;
		a_flags = flags_exclude;
	} else {
		/* A...B -- find merge bases between the two */
		struct commit *a, *b;
		struct commit_list *exclude = NULL;

		a = lookup_commit_reference(revs->repo, &a_obj->oid);
		b = lookup_commit_reference(revs->repo, &b_obj->oid);
		if (!a || !b)
			return dotdot_missing(arg, dotdot, revs, symmetric);

		if (repo_get_merge_bases(the_repository, a, b, &exclude) < 0) {
			free_commit_list(exclude);
			return -1;
		}
		add_rev_cmdline_list(revs, exclude, REV_CMD_MERGE_BASE,
				     flags_exclude);
		add_pending_commit_list(revs, exclude, flags_exclude);
		free_commit_list(exclude);

		b_flags = flags;
		a_flags = flags | SYMMETRIC_LEFT;
	}

	a_obj->flags |= a_flags;
	b_obj->flags |= b_flags;
	add_rev_cmdline(revs, a_obj, a_name, REV_CMD_LEFT, a_flags);
	add_rev_cmdline(revs, b_obj, b_name, REV_CMD_RIGHT, b_flags);
	add_pending_object_with_path(revs, a_obj, a_name, a_oc->mode, a_oc->path);
	add_pending_object_with_path(revs, b_obj, b_name, b_oc->mode, b_oc->path);
	return 0;
}

static int handle_dotdot(const char *arg,
			 struct rev_info *revs, int flags,
			 int cant_be_filename)
{
	struct object_context a_oc = {0}, b_oc = {0};
	char *dotdot = strstr(arg, "..");
	int ret;

	if (!dotdot)
		return -1;

	*dotdot = '\0';
	ret = handle_dotdot_1(arg, dotdot, revs, flags, cant_be_filename,
			      &a_oc, &b_oc);
	*dotdot = '.';

	object_context_release(&a_oc);
	object_context_release(&b_oc);
	return ret;
}

static int handle_revision_arg_1(const char *arg_, struct rev_info *revs, int flags, unsigned revarg_opt)
{
	struct object_context oc = {0};
	char *mark;
	struct object *object;
	struct object_id oid;
	int local_flags;
	const char *arg = arg_;
	int cant_be_filename = revarg_opt & REVARG_CANNOT_BE_FILENAME;
	unsigned get_sha1_flags = GET_OID_RECORD_PATH;
	int ret;

	flags = flags & UNINTERESTING ? flags | BOTTOM : flags & ~BOTTOM;

	if (!cant_be_filename && !strcmp(arg, "..")) {
		/*
		 * Just ".."?  That is not a range but the
		 * pathspec for the parent directory.
		 */
		ret = -1;
		goto out;
	}

	if (!handle_dotdot(arg, revs, flags, revarg_opt)) {
		ret = 0;
		goto out;
	}

	mark = strstr(arg, "^@");
	if (mark && !mark[2]) {
		*mark = 0;
		if (add_parents_only(revs, arg, flags, 0)) {
			ret = 0;
			goto out;
		}
		*mark = '^';
	}
	mark = strstr(arg, "^!");
	if (mark && !mark[2]) {
		*mark = 0;
		if (!add_parents_only(revs, arg, flags ^ (UNINTERESTING | BOTTOM), 0))
			*mark = '^';
	}
	mark = strstr(arg, "^-");
	if (mark) {
		int exclude_parent = 1;

		if (mark[2]) {
			if (strtol_i(mark + 2, 10, &exclude_parent) ||
			    exclude_parent < 1) {
				ret = -1;
				goto out;
			}
		}

		*mark = 0;
		if (!add_parents_only(revs, arg, flags ^ (UNINTERESTING | BOTTOM), exclude_parent))
			*mark = '^';
	}

	local_flags = 0;
	if (*arg == '^') {
		local_flags = UNINTERESTING | BOTTOM;
		arg++;
	}

	if (revarg_opt & REVARG_COMMITTISH)
		get_sha1_flags |= GET_OID_COMMITTISH;

	/*
	 * Even if revs->do_not_die_on_missing_objects is set, we
	 * should error out if we can't even get an oid, as
	 * `--missing=print` should be able to report missing oids.
	 */
	if (get_oid_with_context(revs->repo, arg, get_sha1_flags, &oid, &oc)) {
		ret = revs->ignore_missing ? 0 : -1;
		goto out;
	}
	if (!cant_be_filename)
		verify_non_filename(revs->prefix, arg);
	object = get_reference(revs, arg, &oid, flags ^ local_flags);
	if (!object) {
		ret = (revs->ignore_missing || revs->do_not_die_on_missing_objects) ? 0 : -1;
		goto out;
	}
	add_rev_cmdline(revs, object, arg_, REV_CMD_REV, flags ^ local_flags);
	add_pending_object_with_path(revs, object, arg, oc.mode, oc.path);

	ret = 0;

out:
	object_context_release(&oc);
	return ret;
}

int handle_revision_arg(const char *arg, struct rev_info *revs, int flags, unsigned revarg_opt)
{
	int ret = handle_revision_arg_1(arg, revs, flags, revarg_opt);
	if (!ret)
		revs->rev_input_given = 1;
	return ret;
}

static void read_pathspec_from_stdin(struct strbuf *sb,
				     struct strvec *prune)
{
	while (strbuf_getline(sb, stdin) != EOF)
		strvec_push(prune, sb->buf);
}

static void add_grep(struct rev_info *revs, const char *ptn, enum grep_pat_token what)
{
	append_grep_pattern(&revs->grep_filter, ptn, "command line", 0, what);
}

static void add_header_grep(struct rev_info *revs, enum grep_header_field field, const char *pattern)
{
	append_header_grep_pattern(&revs->grep_filter, field, pattern);
}

static void add_message_grep(struct rev_info *revs, const char *pattern)
{
	add_grep(revs, pattern, GREP_PATTERN_BODY);
}

static int parse_count(const char *arg)
{
	int count;

	if (strtol_i(arg, 10, &count) < 0)
		die("'%s': not an integer", arg);
	return count;
}

static timestamp_t parse_age(const char *arg)
{
	timestamp_t num;
	char *p;

	errno = 0;
	num = parse_timestamp(arg, &p, 10);
	if (errno || *p || p == arg)
		die("'%s': not a number of seconds since epoch", arg);
	return num;
}

static int handle_revision_opt(struct rev_info *revs, int argc, const char **argv,
			       int *unkc, const char **unkv,
			       const struct setup_revision_opt* opt)
{
	const char *arg = argv[0];
	const char *optarg = NULL;
	int argcount;
	const unsigned hexsz = the_hash_algo->hexsz;

	/* pseudo revision arguments */
	if (!strcmp(arg, "--all") || !strcmp(arg, "--branches") ||
	    !strcmp(arg, "--tags") || !strcmp(arg, "--remotes") ||
	    !strcmp(arg, "--reflog") || !strcmp(arg, "--not") ||
	    !strcmp(arg, "--no-walk") || !strcmp(arg, "--do-walk") ||
	    !strcmp(arg, "--bisect") || starts_with(arg, "--glob=") ||
	    !strcmp(arg, "--indexed-objects") ||
	    !strcmp(arg, "--alternate-refs") ||
	    starts_with(arg, "--exclude=") || starts_with(arg, "--exclude-hidden=") ||
	    starts_with(arg, "--branches=") || starts_with(arg, "--tags=") ||
	    starts_with(arg, "--remotes=") || starts_with(arg, "--no-walk="))
	{
		unkv[(*unkc)++] = arg;
		return 1;
	}

	if ((argcount = parse_long_opt("max-count", argv, &optarg))) {
		revs->max_count = parse_count(optarg);
		revs->no_walk = 0;
		return argcount;
	} else if ((argcount = parse_long_opt("skip", argv, &optarg))) {
		revs->skip_count = parse_count(optarg);
		return argcount;
	} else if ((*arg == '-') && isdigit(arg[1])) {
		/* accept -<digit>, like traditional "head" */
		revs->max_count = parse_count(arg + 1);
		revs->no_walk = 0;
	} else if (!strcmp(arg, "-n")) {
		if (argc <= 1)
			return error("-n requires an argument");
		revs->max_count = parse_count(argv[1]);
		revs->no_walk = 0;
		return 2;
	} else if (skip_prefix(arg, "-n", &optarg)) {
		revs->max_count = parse_count(optarg);
		revs->no_walk = 0;
	} else if ((argcount = parse_long_opt("max-age", argv, &optarg))) {
		revs->max_age = parse_age(optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("since", argv, &optarg))) {
		revs->max_age = approxidate(optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("since-as-filter", argv, &optarg))) {
		revs->max_age_as_filter = approxidate(optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("after", argv, &optarg))) {
		revs->max_age = approxidate(optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("min-age", argv, &optarg))) {
		revs->min_age = parse_age(optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("before", argv, &optarg))) {
		revs->min_age = approxidate(optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("until", argv, &optarg))) {
		revs->min_age = approxidate(optarg);
		return argcount;
	} else if (!strcmp(arg, "--first-parent")) {
		revs->first_parent_only = 1;
	} else if (!strcmp(arg, "--exclude-first-parent-only")) {
		revs->exclude_first_parent_only = 1;
	} else if (!strcmp(arg, "--ancestry-path")) {
		revs->ancestry_path = 1;
		revs->simplify_history = 0;
		revs->limited = 1;
		revs->ancestry_path_implicit_bottoms = 1;
	} else if (skip_prefix(arg, "--ancestry-path=", &optarg)) {
		struct commit *c;
		struct object_id oid;
		const char *msg = _("could not get commit for --ancestry-path argument %s");

		revs->ancestry_path = 1;
		revs->simplify_history = 0;
		revs->limited = 1;

		if (repo_get_oid_committish(revs->repo, optarg, &oid))
			return error(msg, optarg);
		get_reference(revs, optarg, &oid, ANCESTRY_PATH);
		c = lookup_commit_reference(revs->repo, &oid);
		if (!c)
			return error(msg, optarg);
		commit_list_insert(c, &revs->ancestry_path_bottoms);
	} else if (!strcmp(arg, "-g") || !strcmp(arg, "--walk-reflogs")) {
		init_reflog_walk(&revs->reflog_info);
	} else if (!strcmp(arg, "--default")) {
		if (argc <= 1)
			return error("bad --default argument");
		revs->def = argv[1];
		return 2;
	} else if (!strcmp(arg, "--merge")) {
		revs->show_merge = 1;
	} else if (!strcmp(arg, "--topo-order")) {
		revs->sort_order = REV_SORT_IN_GRAPH_ORDER;
		revs->topo_order = 1;
	} else if (!strcmp(arg, "--simplify-merges")) {
		revs->simplify_merges = 1;
		revs->topo_order = 1;
		revs->rewrite_parents = 1;
		revs->simplify_history = 0;
		revs->limited = 1;
	} else if (!strcmp(arg, "--simplify-by-decoration")) {
		revs->simplify_merges = 1;
		revs->topo_order = 1;
		revs->rewrite_parents = 1;
		revs->simplify_history = 0;
		revs->simplify_by_decoration = 1;
		revs->limited = 1;
		revs->prune = 1;
	} else if (!strcmp(arg, "--date-order")) {
		revs->sort_order = REV_SORT_BY_COMMIT_DATE;
		revs->topo_order = 1;
	} else if (!strcmp(arg, "--author-date-order")) {
		revs->sort_order = REV_SORT_BY_AUTHOR_DATE;
		revs->topo_order = 1;
	} else if (!strcmp(arg, "--early-output")) {
		revs->early_output = 100;
		revs->topo_order = 1;
	} else if (skip_prefix(arg, "--early-output=", &optarg)) {
		if (strtoul_ui(optarg, 10, &revs->early_output) < 0)
			die("'%s': not a non-negative integer", optarg);
		revs->topo_order = 1;
	} else if (!strcmp(arg, "--parents")) {
		revs->rewrite_parents = 1;
		revs->print_parents = 1;
	} else if (!strcmp(arg, "--dense")) {
		revs->dense = 1;
	} else if (!strcmp(arg, "--sparse")) {
		revs->dense = 0;
	} else if (!strcmp(arg, "--in-commit-order")) {
		revs->tree_blobs_in_commit_order = 1;
	} else if (!strcmp(arg, "--remove-empty")) {
		revs->remove_empty_trees = 1;
	} else if (!strcmp(arg, "--merges")) {
		revs->min_parents = 2;
	} else if (!strcmp(arg, "--no-merges")) {
		revs->max_parents = 1;
	} else if (skip_prefix(arg, "--min-parents=", &optarg)) {
		revs->min_parents = parse_count(optarg);
	} else if (!strcmp(arg, "--no-min-parents")) {
		revs->min_parents = 0;
	} else if (skip_prefix(arg, "--max-parents=", &optarg)) {
		revs->max_parents = parse_count(optarg);
	} else if (!strcmp(arg, "--no-max-parents")) {
		revs->max_parents = -1;
	} else if (!strcmp(arg, "--boundary")) {
		revs->boundary = 1;
	} else if (!strcmp(arg, "--left-right")) {
		revs->left_right = 1;
	} else if (!strcmp(arg, "--left-only")) {
		if (revs->right_only)
			die(_("options '%s' and '%s' cannot be used together"),
			    "--left-only", "--right-only/--cherry");
		revs->left_only = 1;
		revs->limited = 1;
	} else if (!strcmp(arg, "--right-only")) {
		if (revs->left_only)
			die(_("options '%s' and '%s' cannot be used together"), "--right-only", "--left-only");
		revs->right_only = 1;
		revs->limited = 1;
	} else if (!strcmp(arg, "--cherry")) {
		if (revs->left_only)
			die(_("options '%s' and '%s' cannot be used together"), "--cherry", "--left-only");
		revs->cherry_mark = 1;
		revs->right_only = 1;
		revs->max_parents = 1;
		revs->limited = 1;
	} else if (!strcmp(arg, "--count")) {
		revs->count = 1;
	} else if (!strcmp(arg, "--cherry-mark")) {
		if (revs->cherry_pick)
			die(_("options '%s' and '%s' cannot be used together"), "--cherry-mark", "--cherry-pick");
		revs->cherry_mark = 1;
		revs->limited = 1; /* needs limit_list() */
	} else if (!strcmp(arg, "--cherry-pick")) {
		if (revs->cherry_mark)
			die(_("options '%s' and '%s' cannot be used together"), "--cherry-pick", "--cherry-mark");
		revs->cherry_pick = 1;
		revs->limited = 1;
	} else if (!strcmp(arg, "--objects")) {
		revs->tag_objects = 1;
		revs->tree_objects = 1;
		revs->blob_objects = 1;
	} else if (!strcmp(arg, "--objects-edge")) {
		revs->tag_objects = 1;
		revs->tree_objects = 1;
		revs->blob_objects = 1;
		revs->edge_hint = 1;
	} else if (!strcmp(arg, "--objects-edge-aggressive")) {
		revs->tag_objects = 1;
		revs->tree_objects = 1;
		revs->blob_objects = 1;
		revs->edge_hint = 1;
		revs->edge_hint_aggressive = 1;
	} else if (!strcmp(arg, "--verify-objects")) {
		revs->tag_objects = 1;
		revs->tree_objects = 1;
		revs->blob_objects = 1;
		revs->verify_objects = 1;
		disable_commit_graph(revs->repo);
	} else if (!strcmp(arg, "--unpacked")) {
		revs->unpacked = 1;
	} else if (starts_with(arg, "--unpacked=")) {
		die(_("--unpacked=<packfile> no longer supported"));
	} else if (!strcmp(arg, "--no-kept-objects")) {
		revs->no_kept_objects = 1;
		revs->keep_pack_cache_flags |= IN_CORE_KEEP_PACKS;
		revs->keep_pack_cache_flags |= ON_DISK_KEEP_PACKS;
	} else if (skip_prefix(arg, "--no-kept-objects=", &optarg)) {
		revs->no_kept_objects = 1;
		if (!strcmp(optarg, "in-core"))
			revs->keep_pack_cache_flags |= IN_CORE_KEEP_PACKS;
		if (!strcmp(optarg, "on-disk"))
			revs->keep_pack_cache_flags |= ON_DISK_KEEP_PACKS;
	} else if (!strcmp(arg, "-r")) {
		revs->diff = 1;
		revs->diffopt.flags.recursive = 1;
	} else if (!strcmp(arg, "-t")) {
		revs->diff = 1;
		revs->diffopt.flags.recursive = 1;
		revs->diffopt.flags.tree_in_recursive = 1;
	} else if ((argcount = diff_merges_parse_opts(revs, argv))) {
		return argcount;
	} else if (!strcmp(arg, "-v")) {
		revs->verbose_header = 1;
	} else if (!strcmp(arg, "--pretty")) {
		revs->verbose_header = 1;
		revs->pretty_given = 1;
		get_commit_format(NULL, revs);
	} else if (skip_prefix(arg, "--pretty=", &optarg) ||
		   skip_prefix(arg, "--format=", &optarg)) {
		/*
		 * Detached form ("--pretty X" as opposed to "--pretty=X")
		 * not allowed, since the argument is optional.
		 */
		revs->verbose_header = 1;
		revs->pretty_given = 1;
		get_commit_format(optarg, revs);
	} else if (!strcmp(arg, "--expand-tabs")) {
		revs->expand_tabs_in_log = 8;
	} else if (!strcmp(arg, "--no-expand-tabs")) {
		revs->expand_tabs_in_log = 0;
	} else if (skip_prefix(arg, "--expand-tabs=", &arg)) {
		int val;
		if (strtol_i(arg, 10, &val) < 0 || val < 0)
			die("'%s': not a non-negative integer", arg);
		revs->expand_tabs_in_log = val;
	} else if (!strcmp(arg, "--show-notes") || !strcmp(arg, "--notes")) {
		enable_default_display_notes(&revs->notes_opt, &revs->show_notes);
		revs->show_notes_given = 1;
	} else if (!strcmp(arg, "--show-signature")) {
		revs->show_signature = 1;
	} else if (!strcmp(arg, "--no-show-signature")) {
		revs->show_signature = 0;
	} else if (!strcmp(arg, "--show-linear-break")) {
		revs->break_bar = "                    ..........";
		revs->track_linear = 1;
		revs->track_first_time = 1;
	} else if (skip_prefix(arg, "--show-linear-break=", &optarg)) {
		revs->break_bar = xstrdup(optarg);
		revs->track_linear = 1;
		revs->track_first_time = 1;
	} else if (!strcmp(arg, "--show-notes-by-default")) {
		revs->show_notes_by_default = 1;
	} else if (skip_prefix(arg, "--show-notes=", &optarg) ||
		   skip_prefix(arg, "--notes=", &optarg)) {
		if (starts_with(arg, "--show-notes=") &&
		    revs->notes_opt.use_default_notes < 0)
			revs->notes_opt.use_default_notes = 1;
		enable_ref_display_notes(&revs->notes_opt, &revs->show_notes, optarg);
		revs->show_notes_given = 1;
	} else if (!strcmp(arg, "--no-notes")) {
		disable_display_notes(&revs->notes_opt, &revs->show_notes);
		revs->show_notes_given = 1;
	} else if (!strcmp(arg, "--standard-notes")) {
		revs->show_notes_given = 1;
		revs->notes_opt.use_default_notes = 1;
	} else if (!strcmp(arg, "--no-standard-notes")) {
		revs->notes_opt.use_default_notes = 0;
	} else if (!strcmp(arg, "--oneline")) {
		revs->verbose_header = 1;
		get_commit_format("oneline", revs);
		revs->pretty_given = 1;
		revs->abbrev_commit = 1;
	} else if (!strcmp(arg, "--graph")) {
		graph_clear(revs->graph);
		revs->graph = graph_init(revs);
	} else if (!strcmp(arg, "--no-graph")) {
		graph_clear(revs->graph);
		revs->graph = NULL;
	} else if (!strcmp(arg, "--encode-email-headers")) {
		revs->encode_email_headers = 1;
	} else if (!strcmp(arg, "--no-encode-email-headers")) {
		revs->encode_email_headers = 0;
	} else if (!strcmp(arg, "--root")) {
		revs->show_root_diff = 1;
	} else if (!strcmp(arg, "--no-commit-id")) {
		revs->no_commit_id = 1;
	} else if (!strcmp(arg, "--always")) {
		revs->always_show_header = 1;
	} else if (!strcmp(arg, "--no-abbrev")) {
		revs->abbrev = 0;
	} else if (!strcmp(arg, "--abbrev")) {
		revs->abbrev = DEFAULT_ABBREV;
	} else if (skip_prefix(arg, "--abbrev=", &optarg)) {
		revs->abbrev = strtoul(optarg, NULL, 10);
		if (revs->abbrev < MINIMUM_ABBREV)
			revs->abbrev = MINIMUM_ABBREV;
		else if (revs->abbrev > hexsz)
			revs->abbrev = hexsz;
	} else if (!strcmp(arg, "--abbrev-commit")) {
		revs->abbrev_commit = 1;
		revs->abbrev_commit_given = 1;
	} else if (!strcmp(arg, "--no-abbrev-commit")) {
		revs->abbrev_commit = 0;
	} else if (!strcmp(arg, "--full-diff")) {
		revs->diff = 1;
		revs->full_diff = 1;
	} else if (!strcmp(arg, "--show-pulls")) {
		revs->show_pulls = 1;
	} else if (!strcmp(arg, "--full-history")) {
		revs->simplify_history = 0;
	} else if (!strcmp(arg, "--relative-date")) {
		revs->date_mode.type = DATE_RELATIVE;
		revs->date_mode_explicit = 1;
	} else if ((argcount = parse_long_opt("date", argv, &optarg))) {
		parse_date_format(optarg, &revs->date_mode);
		revs->date_mode_explicit = 1;
		return argcount;
	} else if (!strcmp(arg, "--log-size")) {
		revs->show_log_size = 1;
	}
	/*
	 * Grepping the commit log
	 */
	else if ((argcount = parse_long_opt("author", argv, &optarg))) {
		add_header_grep(revs, GREP_HEADER_AUTHOR, optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("committer", argv, &optarg))) {
		add_header_grep(revs, GREP_HEADER_COMMITTER, optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("grep-reflog", argv, &optarg))) {
		add_header_grep(revs, GREP_HEADER_REFLOG, optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("grep", argv, &optarg))) {
		add_message_grep(revs, optarg);
		return argcount;
	} else if (!strcmp(arg, "--basic-regexp")) {
		revs->grep_filter.pattern_type_option = GREP_PATTERN_TYPE_BRE;
	} else if (!strcmp(arg, "--extended-regexp") || !strcmp(arg, "-E")) {
		revs->grep_filter.pattern_type_option = GREP_PATTERN_TYPE_ERE;
	} else if (!strcmp(arg, "--regexp-ignore-case") || !strcmp(arg, "-i")) {
		revs->grep_filter.ignore_case = 1;
		revs->diffopt.pickaxe_opts |= DIFF_PICKAXE_IGNORE_CASE;
	} else if (!strcmp(arg, "--fixed-strings") || !strcmp(arg, "-F")) {
		revs->grep_filter.pattern_type_option = GREP_PATTERN_TYPE_FIXED;
	} else if (!strcmp(arg, "--perl-regexp") || !strcmp(arg, "-P")) {
		revs->grep_filter.pattern_type_option = GREP_PATTERN_TYPE_PCRE;
	} else if (!strcmp(arg, "--all-match")) {
		revs->grep_filter.all_match = 1;
	} else if (!strcmp(arg, "--invert-grep")) {
		revs->grep_filter.no_body_match = 1;
	} else if ((argcount = parse_long_opt("encoding", argv, &optarg))) {
		free(git_log_output_encoding);
		if (strcmp(optarg, "none"))
			git_log_output_encoding = xstrdup(optarg);
		else
			git_log_output_encoding = xstrdup("");
		return argcount;
	} else if (!strcmp(arg, "--reverse")) {
		revs->reverse ^= 1;
	} else if (!strcmp(arg, "--children")) {
		revs->children.name = "children";
		revs->limited = 1;
	} else if (!strcmp(arg, "--ignore-missing")) {
		revs->ignore_missing = 1;
	} else if (opt && opt->allow_exclude_promisor_objects &&
		   !strcmp(arg, "--exclude-promisor-objects")) {
		if (fetch_if_missing)
			BUG("exclude_promisor_objects can only be used when fetch_if_missing is 0");
		revs->exclude_promisor_objects = 1;
	} else {
		int opts = diff_opt_parse(&revs->diffopt, argv, argc, revs->prefix);
		if (!opts)
			unkv[(*unkc)++] = arg;
		return opts;
	}

	return 1;
}

void parse_revision_opt(struct rev_info *revs, struct parse_opt_ctx_t *ctx,
			const struct option *options,
			const char * const usagestr[])
{
	int n = handle_revision_opt(revs, ctx->argc, ctx->argv,
				    &ctx->cpidx, ctx->out, NULL);
	if (n <= 0) {
		error("unknown option `%s'", ctx->argv[0]);
		usage_with_options(usagestr, options);
	}
	ctx->argv += n;
	ctx->argc -= n;
}

void revision_opts_finish(struct rev_info *revs)
{
	if (revs->graph && revs->track_linear)
		die(_("options '%s' and '%s' cannot be used together"), "--show-linear-break", "--graph");

	if (revs->graph) {
		revs->topo_order = 1;
		revs->rewrite_parents = 1;
	}
}

static int for_each_bisect_ref(struct ref_store *refs, each_ref_fn fn,
			       void *cb_data, const char *term)
{
	struct strbuf bisect_refs = STRBUF_INIT;
	int status;
	strbuf_addf(&bisect_refs, "refs/bisect/%s", term);
	status = refs_for_each_fullref_in(refs, bisect_refs.buf, NULL, fn, cb_data);
	strbuf_release(&bisect_refs);
	return status;
}

static int for_each_bad_bisect_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return for_each_bisect_ref(refs, fn, cb_data, term_bad);
}

static int for_each_good_bisect_ref(struct ref_store *refs, each_ref_fn fn, void *cb_data)
{
	return for_each_bisect_ref(refs, fn, cb_data, term_good);
}

static int handle_revision_pseudo_opt(struct rev_info *revs,
				      const char **argv, int *flags)
{
	const char *arg = argv[0];
	const char *optarg;
	struct ref_store *refs;
	int argcount;

	if (revs->repo != the_repository) {
		/*
		 * We need some something like get_submodule_worktrees()
		 * before we can go through all worktrees of a submodule,
		 * .e.g with adding all HEADs from --all, which is not
		 * supported right now, so stick to single worktree.
		 */
		if (!revs->single_worktree)
			BUG("--single-worktree cannot be used together with submodule");
	}
	refs = get_main_ref_store(revs->repo);

	/*
	 * NOTE!
	 *
	 * Commands like "git shortlog" will not accept the options below
	 * unless parse_revision_opt queues them (as opposed to erroring
	 * out).
	 *
	 * When implementing your new pseudo-option, remember to
	 * register it in the list at the top of handle_revision_opt.
	 */
	if (!strcmp(arg, "--all")) {
		handle_refs(refs, revs, *flags, refs_for_each_ref);
		handle_refs(refs, revs, *flags, refs_head_ref);
		if (!revs->single_worktree) {
			struct all_refs_cb cb;

			init_all_refs_cb(&cb, revs, *flags);
			other_head_refs(handle_one_ref, &cb);
		}
		clear_ref_exclusions(&revs->ref_excludes);
	} else if (!strcmp(arg, "--branches")) {
		if (revs->ref_excludes.hidden_refs_configured)
			return error(_("options '%s' and '%s' cannot be used together"),
				     "--exclude-hidden", "--branches");
		handle_refs(refs, revs, *flags, refs_for_each_branch_ref);
		clear_ref_exclusions(&revs->ref_excludes);
	} else if (!strcmp(arg, "--bisect")) {
		read_bisect_terms(&term_bad, &term_good);
		handle_refs(refs, revs, *flags, for_each_bad_bisect_ref);
		handle_refs(refs, revs, *flags ^ (UNINTERESTING | BOTTOM),
			    for_each_good_bisect_ref);
		revs->bisect = 1;
	} else if (!strcmp(arg, "--tags")) {
		if (revs->ref_excludes.hidden_refs_configured)
			return error(_("options '%s' and '%s' cannot be used together"),
				     "--exclude-hidden", "--tags");
		handle_refs(refs, revs, *flags, refs_for_each_tag_ref);
		clear_ref_exclusions(&revs->ref_excludes);
	} else if (!strcmp(arg, "--remotes")) {
		if (revs->ref_excludes.hidden_refs_configured)
			return error(_("options '%s' and '%s' cannot be used together"),
				     "--exclude-hidden", "--remotes");
		handle_refs(refs, revs, *flags, refs_for_each_remote_ref);
		clear_ref_exclusions(&revs->ref_excludes);
	} else if ((argcount = parse_long_opt("glob", argv, &optarg))) {
		struct all_refs_cb cb;
		init_all_refs_cb(&cb, revs, *flags);
		refs_for_each_glob_ref(get_main_ref_store(the_repository),
				       handle_one_ref, optarg, &cb);
		clear_ref_exclusions(&revs->ref_excludes);
		return argcount;
	} else if ((argcount = parse_long_opt("exclude", argv, &optarg))) {
		add_ref_exclusion(&revs->ref_excludes, optarg);
		return argcount;
	} else if ((argcount = parse_long_opt("exclude-hidden", argv, &optarg))) {
		exclude_hidden_refs(&revs->ref_excludes, optarg);
		return argcount;
	} else if (skip_prefix(arg, "--branches=", &optarg)) {
		struct all_refs_cb cb;
		if (revs->ref_excludes.hidden_refs_configured)
			return error(_("options '%s' and '%s' cannot be used together"),
				     "--exclude-hidden", "--branches");
		init_all_refs_cb(&cb, revs, *flags);
		refs_for_each_glob_ref_in(get_main_ref_store(the_repository),
					  handle_one_ref, optarg,
					  "refs/heads/", &cb);
		clear_ref_exclusions(&revs->ref_excludes);
	} else if (skip_prefix(arg, "--tags=", &optarg)) {
		struct all_refs_cb cb;
		if (revs->ref_excludes.hidden_refs_configured)
			return error(_("options '%s' and '%s' cannot be used together"),
				     "--exclude-hidden", "--tags");
		init_all_refs_cb(&cb, revs, *flags);
		refs_for_each_glob_ref_in(get_main_ref_store(the_repository),
					  handle_one_ref, optarg,
					  "refs/tags/", &cb);
		clear_ref_exclusions(&revs->ref_excludes);
	} else if (skip_prefix(arg, "--remotes=", &optarg)) {
		struct all_refs_cb cb;
		if (revs->ref_excludes.hidden_refs_configured)
			return error(_("options '%s' and '%s' cannot be used together"),
				     "--exclude-hidden", "--remotes");
		init_all_refs_cb(&cb, revs, *flags);
		refs_for_each_glob_ref_in(get_main_ref_store(the_repository),
					  handle_one_ref, optarg,
					  "refs/remotes/", &cb);
		clear_ref_exclusions(&revs->ref_excludes);
	} else if (!strcmp(arg, "--reflog")) {
		add_reflogs_to_pending(revs, *flags);
	} else if (!strcmp(arg, "--indexed-objects")) {
		add_index_objects_to_pending(revs, *flags);
	} else if (!strcmp(arg, "--alternate-refs")) {
		add_alternate_refs_to_pending(revs, *flags);
	} else if (!strcmp(arg, "--not")) {
		*flags ^= UNINTERESTING | BOTTOM;
	} else if (!strcmp(arg, "--no-walk")) {
		revs->no_walk = 1;
	} else if (skip_prefix(arg, "--no-walk=", &optarg)) {
		/*
		 * Detached form ("--no-walk X" as opposed to "--no-walk=X")
		 * not allowed, since the argument is optional.
		 */
		revs->no_walk = 1;
		if (!strcmp(optarg, "sorted"))
			revs->unsorted_input = 0;
		else if (!strcmp(optarg, "unsorted"))
			revs->unsorted_input = 1;
		else
			return error("invalid argument to --no-walk");
	} else if (!strcmp(arg, "--do-walk")) {
		revs->no_walk = 0;
	} else if (!strcmp(arg, "--single-worktree")) {
		revs->single_worktree = 1;
	} else if (skip_prefix(arg, ("--filter="), &arg)) {
		parse_list_objects_filter(&revs->filter, arg);
	} else if (!strcmp(arg, ("--no-filter"))) {
		list_objects_filter_set_no_filter(&revs->filter);
	} else {
		return 0;
	}

	return 1;
}

static void read_revisions_from_stdin(struct rev_info *revs,
				      struct strvec *prune)
{
	struct strbuf sb;
	int seen_dashdash = 0;
	int seen_end_of_options = 0;
	int save_warning;
	int flags = 0;

	save_warning = warn_on_object_refname_ambiguity;
	warn_on_object_refname_ambiguity = 0;

	strbuf_init(&sb, 1000);
	while (strbuf_getline(&sb, stdin) != EOF) {
		if (!sb.len)
			break;

		if (!strcmp(sb.buf, "--")) {
			seen_dashdash = 1;
			break;
		}

		if (!seen_end_of_options && sb.buf[0] == '-') {
			const char *argv[] = { sb.buf, NULL };

			if (!strcmp(sb.buf, "--end-of-options")) {
				seen_end_of_options = 1;
				continue;
			}

			if (handle_revision_pseudo_opt(revs, argv, &flags) > 0)
				continue;

			die(_("invalid option '%s' in --stdin mode"), sb.buf);
		}

		if (handle_revision_arg(sb.buf, revs, flags,
					REVARG_CANNOT_BE_FILENAME))
			die("bad revision '%s'", sb.buf);
	}
	if (seen_dashdash)
		read_pathspec_from_stdin(&sb, prune);

	strbuf_release(&sb);
	warn_on_object_refname_ambiguity = save_warning;
}

static void NORETURN diagnose_missing_default(const char *def)
{
	int flags;
	const char *refname;

	refname = refs_resolve_ref_unsafe(get_main_ref_store(the_repository),
					  def, 0, NULL, &flags);
	if (!refname || !(flags & REF_ISSYMREF) || (flags & REF_ISBROKEN))
		die(_("your current branch appears to be broken"));

	skip_prefix(refname, "refs/heads/", &refname);
	die(_("your current branch '%s' does not have any commits yet"),
	    refname);
}

/*
 * Parse revision information, filling in the "rev_info" structure,
 * and removing the used arguments from the argument list.
 *
 * Returns the number of arguments left that weren't recognized
 * (which are also moved to the head of the argument list)
 */
int setup_revisions(int argc, const char **argv, struct rev_info *revs, struct setup_revision_opt *opt)
{
	int i, flags, left, seen_dashdash, revarg_opt;
	struct strvec prune_data = STRVEC_INIT;
	int seen_end_of_options = 0;

	/* First, search for "--" */
	if (opt && opt->assume_dashdash) {
		seen_dashdash = 1;
	} else {
		seen_dashdash = 0;
		for (i = 1; i < argc; i++) {
			const char *arg = argv[i];
			if (strcmp(arg, "--"))
				continue;
			if (opt && opt->free_removed_argv_elements)
				free((char *)argv[i]);
			argv[i] = NULL;
			argc = i;
			if (argv[i + 1])
				strvec_pushv(&prune_data, argv + i + 1);
			seen_dashdash = 1;
			break;
		}
	}

	/* Second, deal with arguments and options */
	flags = 0;
	revarg_opt = opt ? opt->revarg_opt : 0;
	if (seen_dashdash)
		revarg_opt |= REVARG_CANNOT_BE_FILENAME;
	for (left = i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!seen_end_of_options && *arg == '-') {
			int opts;

			opts = handle_revision_pseudo_opt(
						revs, argv + i,
						&flags);
			if (opts > 0) {
				i += opts - 1;
				continue;
			}

			if (!strcmp(arg, "--stdin")) {
				if (revs->disable_stdin) {
					argv[left++] = arg;
					continue;
				}
				if (revs->read_from_stdin++)
					die("--stdin given twice?");
				read_revisions_from_stdin(revs, &prune_data);
				continue;
			}

			if (!strcmp(arg, "--end-of-options")) {
				seen_end_of_options = 1;
				continue;
			}

			opts = handle_revision_opt(revs, argc - i, argv + i,
						   &left, argv, opt);
			if (opts > 0) {
				i += opts - 1;
				continue;
			}
			if (opts < 0)
				exit(128);
			continue;
		}


		if (handle_revision_arg(arg, revs, flags, revarg_opt)) {
			int j;
			if (seen_dashdash || *arg == '^')
				die("bad revision '%s'", arg);

			/* If we didn't have a "--":
			 * (1) all filenames must exist;
			 * (2) all rev-args must not be interpretable
			 *     as a valid filename.
			 * but the latter we have checked in the main loop.
			 */
			for (j = i; j < argc; j++)
				verify_filename(revs->prefix, argv[j], j == i);

			strvec_pushv(&prune_data, argv + i);
			break;
		}
	}
	revision_opts_finish(revs);

	if (prune_data.nr) {
		/*
		 * If we need to introduce the magic "a lone ':' means no
		 * pathspec whatsoever", here is the place to do so.
		 *
		 * if (prune_data.nr == 1 && !strcmp(prune_data[0], ":")) {
		 *	prune_data.nr = 0;
		 *	prune_data.alloc = 0;
		 *	free(prune_data.path);
		 *	prune_data.path = NULL;
		 * } else {
		 *	terminate prune_data.alloc with NULL and
		 *	call init_pathspec() to set revs->prune_data here.
		 * }
		 */
		parse_pathspec(&revs->prune_data, 0, 0,
			       revs->prefix, prune_data.v);
	}
	strvec_clear(&prune_data);

	if (!revs->def)
		revs->def = opt ? opt->def : NULL;
	if (opt && opt->tweak)
		opt->tweak(revs);
	if (revs->show_merge)
		prepare_show_merge(revs);
	if (revs->def && !revs->pending.nr && !revs->rev_input_given) {
		struct object_id oid;
		struct object *object;
		struct object_context oc;
		if (get_oid_with_context(revs->repo, revs->def, 0, &oid, &oc))
			diagnose_missing_default(revs->def);
		object = get_reference(revs, revs->def, &oid, 0);
		add_pending_object_with_mode(revs, object, revs->def, oc.mode);
		object_context_release(&oc);
	}

	/* Did the user ask for any diff output? Run the diff! */
	if (revs->diffopt.output_format & ~DIFF_FORMAT_NO_OUTPUT)
		revs->diff = 1;

	/* Pickaxe, diff-filter and rename following need diffs */
	if ((revs->diffopt.pickaxe_opts & DIFF_PICKAXE_KINDS_MASK) ||
	    revs->diffopt.filter ||
	    revs->diffopt.flags.follow_renames)
		revs->diff = 1;

	if (revs->diffopt.objfind)
		revs->simplify_history = 0;

	if (revs->line_level_traverse) {
		if (want_ancestry(revs))
			revs->limited = 1;
		revs->topo_order = 1;
	}

	if (revs->topo_order && !generation_numbers_enabled(the_repository))
		revs->limited = 1;

	if (revs->prune_data.nr) {
		copy_pathspec(&revs->pruning.pathspec, &revs->prune_data);
		/* Can't prune commits with rename following: the paths change.. */
		if (!revs->diffopt.flags.follow_renames)
			revs->prune = 1;
		if (!revs->full_diff)
			copy_pathspec(&revs->diffopt.pathspec,
				      &revs->prune_data);
	}

	diff_merges_setup_revs(revs);

	revs->diffopt.abbrev = revs->abbrev;

	diff_setup_done(&revs->diffopt);

	if (!is_encoding_utf8(get_log_output_encoding()))
		revs->grep_filter.ignore_locale = 1;
	compile_grep_patterns(&revs->grep_filter);

	if (revs->reflog_info && revs->limited)
		die("cannot combine --walk-reflogs with history-limiting options");
	if (revs->rewrite_parents && revs->children.name)
		die(_("options '%s' and '%s' cannot be used together"), "--parents", "--children");
	if (revs->filter.choice && !revs->blob_objects)
		die(_("object filtering requires --objects"));

	/*
	 * Limitations on the graph functionality
	 */
	die_for_incompatible_opt3(!!revs->graph, "--graph",
				  !!revs->reverse, "--reverse",
				  !!revs->reflog_info, "--walk-reflogs");

	if (revs->no_walk && revs->graph)
		die(_("options '%s' and '%s' cannot be used together"), "--no-walk", "--graph");
	if (!revs->reflog_info && revs->grep_filter.use_reflog_filter)
		die(_("the option '%s' requires '%s'"), "--grep-reflog", "--walk-reflogs");

	if (revs->line_level_traverse &&
	    (revs->diffopt.output_format & ~(DIFF_FORMAT_PATCH | DIFF_FORMAT_NO_OUTPUT)))
		die(_("-L does not yet support diff formats besides -p and -s"));

	if (revs->expand_tabs_in_log < 0)
		revs->expand_tabs_in_log = revs->expand_tabs_in_log_default;

	if (!revs->show_notes_given && revs->show_notes_by_default) {
		enable_default_display_notes(&revs->notes_opt, &revs->show_notes);
		revs->show_notes_given = 1;
	}

	return left;
}

static void release_revisions_cmdline(struct rev_cmdline_info *cmdline)
{
	unsigned int i;

	for (i = 0; i < cmdline->nr; i++)
		free((char *)cmdline->rev[i].name);
	free(cmdline->rev);
}

static void release_revisions_mailmap(struct string_list *mailmap)
{
	if (!mailmap)
		return;
	clear_mailmap(mailmap);
	free(mailmap);
}

static void release_revisions_topo_walk_info(struct topo_walk_info *info);

static void free_void_commit_list(void *list)
{
	free_commit_list(list);
}

void release_revisions(struct rev_info *revs)
{
	free_commit_list(revs->commits);
	free_commit_list(revs->ancestry_path_bottoms);
	release_display_notes(&revs->notes_opt);
	object_array_clear(&revs->pending);
	object_array_clear(&revs->boundary_commits);
	release_revisions_cmdline(&revs->cmdline);
	list_objects_filter_release(&revs->filter);
	clear_pathspec(&revs->prune_data);
	date_mode_release(&revs->date_mode);
	release_revisions_mailmap(revs->mailmap);
	free_grep_patterns(&revs->grep_filter);
	graph_clear(revs->graph);
	diff_free(&revs->diffopt);
	diff_free(&revs->pruning);
	reflog_walk_info_release(revs->reflog_info);
	release_revisions_topo_walk_info(revs->topo_walk_info);
	clear_decoration(&revs->children, free_void_commit_list);
	clear_decoration(&revs->merge_simplification, free);
	clear_decoration(&revs->treesame, free);
	line_log_free(revs);
	oidset_clear(&revs->missing_commits);

	for (int i = 0; i < revs->bloom_keys_nr; i++)
		clear_bloom_key(&revs->bloom_keys[i]);
	FREE_AND_NULL(revs->bloom_keys);
	revs->bloom_keys_nr = 0;
}

static void add_child(struct rev_info *revs, struct commit *parent, struct commit *child)
{
	struct commit_list *l = xcalloc(1, sizeof(*l));

	l->item = child;
	l->next = add_decoration(&revs->children, &parent->object, l);
}

static int remove_duplicate_parents(struct rev_info *revs, struct commit *commit)
{
	struct treesame_state *ts = lookup_decoration(&revs->treesame, &commit->object);
	struct commit_list **pp, *p;
	int surviving_parents;

	/* Examine existing parents while marking ones we have seen... */
	pp = &commit->parents;
	surviving_parents = 0;
	while ((p = *pp) != NULL) {
		struct commit *parent = p->item;
		if (parent->object.flags & TMP_MARK) {
			*pp = p->next;
			free(p);
			if (ts)
				compact_treesame(revs, commit, surviving_parents);
			continue;
		}
		parent->object.flags |= TMP_MARK;
		surviving_parents++;
		pp = &p->next;
	}
	/* clear the temporary mark */
	for (p = commit->parents; p; p = p->next) {
		p->item->object.flags &= ~TMP_MARK;
	}
	/* no update_treesame() - removing duplicates can't affect TREESAME */
	return surviving_parents;
}

struct merge_simplify_state {
	struct commit *simplified;
};

static struct merge_simplify_state *locate_simplify_state(struct rev_info *revs, struct commit *commit)
{
	struct merge_simplify_state *st;

	st = lookup_decoration(&revs->merge_simplification, &commit->object);
	if (!st) {
		CALLOC_ARRAY(st, 1);
		add_decoration(&revs->merge_simplification, &commit->object, st);
	}
	return st;
}

static int mark_redundant_parents(struct commit *commit)
{
	struct commit_list *h = reduce_heads(commit->parents);
	int i = 0, marked = 0;
	struct commit_list *po, *pn;

	/* Want these for sanity-checking only */
	int orig_cnt = commit_list_count(commit->parents);
	int cnt = commit_list_count(h);

	/*
	 * Not ready to remove items yet, just mark them for now, based
	 * on the output of reduce_heads(). reduce_heads outputs the reduced
	 * set in its original order, so this isn't too hard.
	 */
	po = commit->parents;
	pn = h;
	while (po) {
		if (pn && po->item == pn->item) {
			pn = pn->next;
			i++;
		} else {
			po->item->object.flags |= TMP_MARK;
			marked++;
		}
		po=po->next;
	}

	if (i != cnt || cnt+marked != orig_cnt)
		die("mark_redundant_parents %d %d %d %d", orig_cnt, cnt, i, marked);

	free_commit_list(h);

	return marked;
}

static int mark_treesame_root_parents(struct commit *commit)
{
	struct commit_list *p;
	int marked = 0;

	for (p = commit->parents; p; p = p->next) {
		struct commit *parent = p->item;
		if (!parent->parents && (parent->object.flags & TREESAME)) {
			parent->object.flags |= TMP_MARK;
			marked++;
		}
	}

	return marked;
}

/*
 * Awkward naming - this means one parent we are TREESAME to.
 * cf mark_treesame_root_parents: root parents that are TREESAME (to an
 * empty tree). Better name suggestions?
 */
static int leave_one_treesame_to_parent(struct rev_info *revs, struct commit *commit)
{
	struct treesame_state *ts = lookup_decoration(&revs->treesame, &commit->object);
	struct commit *unmarked = NULL, *marked = NULL;
	struct commit_list *p;
	unsigned n;

	for (p = commit->parents, n = 0; p; p = p->next, n++) {
		if (ts->treesame[n]) {
			if (p->item->object.flags & TMP_MARK) {
				if (!marked)
					marked = p->item;
			} else {
				if (!unmarked) {
					unmarked = p->item;
					break;
				}
			}
		}
	}

	/*
	 * If we are TREESAME to a marked-for-deletion parent, but not to any
	 * unmarked parents, unmark the first TREESAME parent. This is the
	 * parent that the default simplify_history==1 scan would have followed,
	 * and it doesn't make sense to omit that path when asking for a
	 * simplified full history. Retaining it improves the chances of
	 * understanding odd missed merges that took an old version of a file.
	 *
	 * Example:
	 *
	 *   I--------*X       A modified the file, but mainline merge X used
	 *    \       /        "-s ours", so took the version from I. X is
	 *     `-*A--'         TREESAME to I and !TREESAME to A.
	 *
	 * Default log from X would produce "I". Without this check,
	 * --full-history --simplify-merges would produce "I-A-X", showing
	 * the merge commit X and that it changed A, but not making clear that
	 * it had just taken the I version. With this check, the topology above
	 * is retained.
	 *
	 * Note that it is possible that the simplification chooses a different
	 * TREESAME parent from the default, in which case this test doesn't
	 * activate, and we _do_ drop the default parent. Example:
	 *
	 *   I------X         A modified the file, but it was reverted in B,
	 *    \    /          meaning mainline merge X is TREESAME to both
	 *    *A-*B           parents.
	 *
	 * Default log would produce "I" by following the first parent;
	 * --full-history --simplify-merges will produce "I-A-B". But this is a
	 * reasonable result - it presents a logical full history leading from
	 * I to X, and X is not an important merge.
	 */
	if (!unmarked && marked) {
		marked->object.flags &= ~TMP_MARK;
		return 1;
	}

	return 0;
}

static int remove_marked_parents(struct rev_info *revs, struct commit *commit)
{
	struct commit_list **pp, *p;
	int nth_parent, removed = 0;

	pp = &commit->parents;
	nth_parent = 0;
	while ((p = *pp) != NULL) {
		struct commit *parent = p->item;
		if (parent->object.flags & TMP_MARK) {
			parent->object.flags &= ~TMP_MARK;
			*pp = p->next;
			free(p);
			removed++;
			compact_treesame(revs, commit, nth_parent);
			continue;
		}
		pp = &p->next;
		nth_parent++;
	}

	/* Removing parents can only increase TREESAMEness */
	if (removed && !(commit->object.flags & TREESAME))
		update_treesame(revs, commit);

	return nth_parent;
}

static struct commit_list **simplify_one(struct rev_info *revs, struct commit *commit, struct commit_list **tail)
{
	struct commit_list *p;
	struct commit *parent;
	struct merge_simplify_state *st, *pst;
	int cnt;

	st = locate_simplify_state(revs, commit);

	/*
	 * Have we handled this one?
	 */
	if (st->simplified)
		return tail;

	/*
	 * An UNINTERESTING commit simplifies to itself, so does a
	 * root commit.  We do not rewrite parents of such commit
	 * anyway.
	 */
	if ((commit->object.flags & UNINTERESTING) || !commit->parents) {
		st->simplified = commit;
		return tail;
	}

	/*
	 * Do we know what commit all of our parents that matter
	 * should be rewritten to?  Otherwise we are not ready to
	 * rewrite this one yet.
	 */
	for (cnt = 0, p = commit->parents; p; p = p->next) {
		pst = locate_simplify_state(revs, p->item);
		if (!pst->simplified) {
			tail = &commit_list_insert(p->item, tail)->next;
			cnt++;
		}
		if (revs->first_parent_only)
			break;
	}
	if (cnt) {
		tail = &commit_list_insert(commit, tail)->next;
		return tail;
	}

	/*
	 * Rewrite our list of parents. Note that this cannot
	 * affect our TREESAME flags in any way - a commit is
	 * always TREESAME to its simplification.
	 */
	for (p = commit->parents; p; p = p->next) {
		pst = locate_simplify_state(revs, p->item);
		p->item = pst->simplified;
		if (revs->first_parent_only)
			break;
	}

	if (revs->first_parent_only)
		cnt = 1;
	else
		cnt = remove_duplicate_parents(revs, commit);

	/*
	 * It is possible that we are a merge and one side branch
	 * does not have any commit that touches the given paths;
	 * in such a case, the immediate parent from that branch
	 * will be rewritten to be the merge base.
	 *
	 *      o----X		X: the commit we are looking at;
	 *     /    /		o: a commit that touches the paths;
	 * ---o----'
	 *
	 * Further, a merge of an independent branch that doesn't
	 * touch the path will reduce to a treesame root parent:
	 *
	 *  ----o----X		X: the commit we are looking at;
	 *          /		o: a commit that touches the paths;
	 *         r		r: a root commit not touching the paths
	 *
	 * Detect and simplify both cases.
	 */
	if (1 < cnt) {
		int marked = mark_redundant_parents(commit);
		marked += mark_treesame_root_parents(commit);
		if (marked)
			marked -= leave_one_treesame_to_parent(revs, commit);
		if (marked)
			cnt = remove_marked_parents(revs, commit);
	}

	/*
	 * A commit simplifies to itself if it is a root, if it is
	 * UNINTERESTING, if it touches the given paths, or if it is a
	 * merge and its parents don't simplify to one relevant commit
	 * (the first two cases are already handled at the beginning of
	 * this function).
	 *
	 * Otherwise, it simplifies to what its sole relevant parent
	 * simplifies to.
	 */
	if (!cnt ||
	    (commit->object.flags & UNINTERESTING) ||
	    !(commit->object.flags & TREESAME) ||
	    (parent = one_relevant_parent(revs, commit->parents)) == NULL ||
	    (revs->show_pulls && (commit->object.flags & PULL_MERGE)))
		st->simplified = commit;
	else {
		pst = locate_simplify_state(revs, parent);
		st->simplified = pst->simplified;
	}
	return tail;
}

static void simplify_merges(struct rev_info *revs)
{
	struct commit_list *list, *next;
	struct commit_list *yet_to_do, **tail;
	struct commit *commit;

	if (!revs->prune)
		return;

	/* feed the list reversed */
	yet_to_do = NULL;
	for (list = revs->commits; list; list = next) {
		commit = list->item;
		next = list->next;
		/*
		 * Do not free(list) here yet; the original list
		 * is used later in this function.
		 */
		commit_list_insert(commit, &yet_to_do);
	}
	while (yet_to_do) {
		list = yet_to_do;
		yet_to_do = NULL;
		tail = &yet_to_do;
		while (list) {
			commit = pop_commit(&list);
			tail = simplify_one(revs, commit, tail);
		}
	}

	/* clean up the result, removing the simplified ones */
	list = revs->commits;
	revs->commits = NULL;
	tail = &revs->commits;
	while (list) {
		struct merge_simplify_state *st;

		commit = pop_commit(&list);
		st = locate_simplify_state(revs, commit);
		if (st->simplified == commit)
			tail = &commit_list_insert(commit, tail)->next;
	}
}

static void set_children(struct rev_info *revs)
{
	struct commit_list *l;
	for (l = revs->commits; l; l = l->next) {
		struct commit *commit = l->item;
		struct commit_list *p;

		for (p = commit->parents; p; p = p->next)
			add_child(revs, p->item, commit);
	}
}

void reset_revision_walk(void)
{
	clear_object_flags(the_repository,
			   SEEN | ADDED | SHOWN | TOPO_WALK_EXPLORED | TOPO_WALK_INDEGREE);
}

static int mark_uninteresting(const struct object_id *oid,
			      struct packed_git *pack UNUSED,
			      uint32_t pos UNUSED,
			      void *cb)
{
	struct rev_info *revs = cb;
	struct object *o = lookup_unknown_object(revs->repo, oid);
	o->flags |= UNINTERESTING | SEEN;
	return 0;
}

define_commit_slab(indegree_slab, int);
define_commit_slab(author_date_slab, timestamp_t);

struct topo_walk_info {
	timestamp_t min_generation;
	struct prio_queue explore_queue;
	struct prio_queue indegree_queue;
	struct prio_queue topo_queue;
	struct indegree_slab indegree;
	struct author_date_slab author_date;
};

static int topo_walk_atexit_registered;
static unsigned int count_explore_walked;
static unsigned int count_indegree_walked;
static unsigned int count_topo_walked;

static void trace2_topo_walk_statistics_atexit(void)
{
	struct json_writer jw = JSON_WRITER_INIT;

	jw_object_begin(&jw, 0);
	jw_object_intmax(&jw, "count_explore_walked", count_explore_walked);
	jw_object_intmax(&jw, "count_indegree_walked", count_indegree_walked);
	jw_object_intmax(&jw, "count_topo_walked", count_topo_walked);
	jw_end(&jw);

	trace2_data_json("topo_walk", the_repository, "statistics", &jw);

	jw_release(&jw);
}

static inline void test_flag_and_insert(struct prio_queue *q, struct commit *c, int flag)
{
	if (c->object.flags & flag)
		return;

	c->object.flags |= flag;
	prio_queue_put(q, c);
}

static void explore_walk_step(struct rev_info *revs)
{
	struct topo_walk_info *info = revs->topo_walk_info;
	struct commit_list *p;
	struct commit *c = prio_queue_get(&info->explore_queue);

	if (!c)
		return;

	if (repo_parse_commit_gently(revs->repo, c, 1) < 0)
		return;

	count_explore_walked++;

	if (revs->sort_order == REV_SORT_BY_AUTHOR_DATE)
		record_author_date(&info->author_date, c);

	if (revs->max_age != -1 && (c->date < revs->max_age))
		c->object.flags |= UNINTERESTING;

	if (process_parents(revs, c, NULL, NULL) < 0)
		return;

	if (c->object.flags & UNINTERESTING)
		mark_parents_uninteresting(revs, c);

	for (p = c->parents; p; p = p->next)
		test_flag_and_insert(&info->explore_queue, p->item, TOPO_WALK_EXPLORED);
}

static void explore_to_depth(struct rev_info *revs,
			     timestamp_t gen_cutoff)
{
	struct topo_walk_info *info = revs->topo_walk_info;
	struct commit *c;
	while ((c = prio_queue_peek(&info->explore_queue)) &&
	       commit_graph_generation(c) >= gen_cutoff)
		explore_walk_step(revs);
}

static void indegree_walk_step(struct rev_info *revs)
{
	struct commit_list *p;
	struct topo_walk_info *info = revs->topo_walk_info;
	struct commit *c = prio_queue_get(&info->indegree_queue);

	if (!c)
		return;

	if (repo_parse_commit_gently(revs->repo, c, 1) < 0)
		return;

	count_indegree_walked++;

	explore_to_depth(revs, commit_graph_generation(c));

	for (p = c->parents; p; p = p->next) {
		struct commit *parent = p->item;
		int *pi = indegree_slab_at(&info->indegree, parent);

		if (repo_parse_commit_gently(revs->repo, parent, 1) < 0)
			return;

		if (*pi)
			(*pi)++;
		else
			*pi = 2;

		test_flag_and_insert(&info->indegree_queue, parent, TOPO_WALK_INDEGREE);

		if (revs->first_parent_only)
			return;
	}
}

static void compute_indegrees_to_depth(struct rev_info *revs,
				       timestamp_t gen_cutoff)
{
	struct topo_walk_info *info = revs->topo_walk_info;
	struct commit *c;
	while ((c = prio_queue_peek(&info->indegree_queue)) &&
	       commit_graph_generation(c) >= gen_cutoff)
		indegree_walk_step(revs);
}

static void release_revisions_topo_walk_info(struct topo_walk_info *info)
{
	if (!info)
		return;
	clear_prio_queue(&info->explore_queue);
	clear_prio_queue(&info->indegree_queue);
	clear_prio_queue(&info->topo_queue);
	clear_indegree_slab(&info->indegree);
	clear_author_date_slab(&info->author_date);
	free(info);
}

static void reset_topo_walk(struct rev_info *revs)
{
	release_revisions_topo_walk_info(revs->topo_walk_info);
	revs->topo_walk_info = NULL;
}

static void init_topo_walk(struct rev_info *revs)
{
	struct topo_walk_info *info;
	struct commit_list *list;
	if (revs->topo_walk_info)
		reset_topo_walk(revs);

	revs->topo_walk_info = xmalloc(sizeof(struct topo_walk_info));
	info = revs->topo_walk_info;
	memset(info, 0, sizeof(struct topo_walk_info));

	init_indegree_slab(&info->indegree);
	memset(&info->explore_queue, 0, sizeof(info->explore_queue));
	memset(&info->indegree_queue, 0, sizeof(info->indegree_queue));
	memset(&info->topo_queue, 0, sizeof(info->topo_queue));

	switch (revs->sort_order) {
	default: /* REV_SORT_IN_GRAPH_ORDER */
		info->topo_queue.compare = NULL;
		break;
	case REV_SORT_BY_COMMIT_DATE:
		info->topo_queue.compare = compare_commits_by_commit_date;
		break;
	case REV_SORT_BY_AUTHOR_DATE:
		init_author_date_slab(&info->author_date);
		info->topo_queue.compare = compare_commits_by_author_date;
		info->topo_queue.cb_data = &info->author_date;
		break;
	}

	info->explore_queue.compare = compare_commits_by_gen_then_commit_date;
	info->indegree_queue.compare = compare_commits_by_gen_then_commit_date;

	info->min_generation = GENERATION_NUMBER_INFINITY;
	for (list = revs->commits; list; list = list->next) {
		struct commit *c = list->item;
		timestamp_t generation;

		if (repo_parse_commit_gently(revs->repo, c, 1))
			continue;

		test_flag_and_insert(&info->explore_queue, c, TOPO_WALK_EXPLORED);
		test_flag_and_insert(&info->indegree_queue, c, TOPO_WALK_INDEGREE);

		generation = commit_graph_generation(c);
		if (generation < info->min_generation)
			info->min_generation = generation;

		*(indegree_slab_at(&info->indegree, c)) = 1;

		if (revs->sort_order == REV_SORT_BY_AUTHOR_DATE)
			record_author_date(&info->author_date, c);
	}
	compute_indegrees_to_depth(revs, info->min_generation);

	for (list = revs->commits; list; list = list->next) {
		struct commit *c = list->item;

		if (*(indegree_slab_at(&info->indegree, c)) == 1)
			prio_queue_put(&info->topo_queue, c);
	}

	/*
	 * This is unfortunate; the initial tips need to be shown
	 * in the order given from the revision traversal machinery.
	 */
	if (revs->sort_order == REV_SORT_IN_GRAPH_ORDER)
		prio_queue_reverse(&info->topo_queue);

	if (trace2_is_enabled() && !topo_walk_atexit_registered) {
		atexit(trace2_topo_walk_statistics_atexit);
		topo_walk_atexit_registered = 1;
	}
}

static struct commit *next_topo_commit(struct rev_info *revs)
{
	struct commit *c;
	struct topo_walk_info *info = revs->topo_walk_info;

	/* pop next off of topo_queue */
	c = prio_queue_get(&info->topo_queue);

	if (c)
		*(indegree_slab_at(&info->indegree, c)) = 0;

	return c;
}

static void expand_topo_walk(struct rev_info *revs, struct commit *commit)
{
	struct commit_list *p;
	struct topo_walk_info *info = revs->topo_walk_info;
	if (process_parents(revs, commit, NULL, NULL) < 0) {
		if (!revs->ignore_missing_links)
			die("Failed to traverse parents of commit %s",
			    oid_to_hex(&commit->object.oid));
	}

	count_topo_walked++;

	for (p = commit->parents; p; p = p->next) {
		struct commit *parent = p->item;
		int *pi;
		timestamp_t generation;

		if (parent->object.flags & UNINTERESTING)
			continue;

		if (repo_parse_commit_gently(revs->repo, parent, 1) < 0)
			continue;

		generation = commit_graph_generation(parent);
		if (generation < info->min_generation) {
			info->min_generation = generation;
			compute_indegrees_to_depth(revs, info->min_generation);
		}

		pi = indegree_slab_at(&info->indegree, parent);

		(*pi)--;
		if (*pi == 1)
			prio_queue_put(&info->topo_queue, parent);

		if (revs->first_parent_only)
			return;
	}
}

int prepare_revision_walk(struct rev_info *revs)
{
	int i;
	struct object_array old_pending;
	struct commit_list **next = &revs->commits;

	memcpy(&old_pending, &revs->pending, sizeof(old_pending));
	revs->pending.nr = 0;
	revs->pending.alloc = 0;
	revs->pending.objects = NULL;
	for (i = 0; i < old_pending.nr; i++) {
		struct object_array_entry *e = old_pending.objects + i;
		struct commit *commit = handle_commit(revs, e);
		if (commit) {
			if (!(commit->object.flags & SEEN)) {
				commit->object.flags |= SEEN;
				next = commit_list_append(commit, next);
			}
		}
	}
	object_array_clear(&old_pending);

	/* Signal whether we need per-parent treesame decoration */
	if (revs->simplify_merges ||
	    (revs->limited && limiting_can_increase_treesame(revs)))
		revs->treesame.name = "treesame";

	if (revs->exclude_promisor_objects) {
		for_each_packed_object(revs->repo, mark_uninteresting, revs,
				       FOR_EACH_OBJECT_PROMISOR_ONLY);
	}

	if (!revs->reflog_info)
		prepare_to_use_bloom_filter(revs);
	if (!revs->unsorted_input)
		commit_list_sort_by_date(&revs->commits);
	if (revs->no_walk)
		return 0;
	if (revs->limited) {
		if (limit_list(revs) < 0)
			return -1;
		if (revs->topo_order)
			sort_in_topological_order(&revs->commits, revs->sort_order);
	} else if (revs->topo_order)
		init_topo_walk(revs);
	if (revs->line_level_traverse && want_ancestry(revs))
		/*
		 * At the moment we can only do line-level log with parent
		 * rewriting by performing this expensive pre-filtering step.
		 * If parent rewriting is not requested, then we rather
		 * perform the line-level log filtering during the regular
		 * history traversal.
		 */
		line_log_filter(revs);
	if (revs->simplify_merges)
		simplify_merges(revs);
	if (revs->children.name)
		set_children(revs);

	return 0;
}

static enum rewrite_result rewrite_one_1(struct rev_info *revs,
					 struct commit **pp,
					 struct prio_queue *queue)
{
	for (;;) {
		struct commit *p = *pp;
		if (!revs->limited)
			if (process_parents(revs, p, NULL, queue) < 0)
				return rewrite_one_error;
		if (p->object.flags & UNINTERESTING)
			return rewrite_one_ok;
		if (!(p->object.flags & TREESAME))
			return rewrite_one_ok;
		if (!p->parents)
			return rewrite_one_noparents;
		if (!(p = one_relevant_parent(revs, p->parents)))
			return rewrite_one_ok;
		*pp = p;
	}
}

static void merge_queue_into_list(struct prio_queue *q, struct commit_list **list)
{
	while (q->nr) {
		struct commit *item = prio_queue_peek(q);
		struct commit_list *p = *list;

		if (p && p->item->date >= item->date)
			list = &p->next;
		else {
			p = commit_list_insert(item, list);
			list = &p->next; /* skip newly added item */
			prio_queue_get(q); /* pop item */
		}
	}
}

static enum rewrite_result rewrite_one(struct rev_info *revs, struct commit **pp)
{
	struct prio_queue queue = { compare_commits_by_commit_date };
	enum rewrite_result ret = rewrite_one_1(revs, pp, &queue);
	merge_queue_into_list(&queue, &revs->commits);
	clear_prio_queue(&queue);
	return ret;
}

int rewrite_parents(struct rev_info *revs, struct commit *commit,
	rewrite_parent_fn_t rewrite_parent)
{
	struct commit_list **pp = &commit->parents;
	while (*pp) {
		struct commit_list *parent = *pp;
		switch (rewrite_parent(revs, &parent->item)) {
		case rewrite_one_ok:
			break;
		case rewrite_one_noparents:
			*pp = parent->next;
			free(parent);
			continue;
		case rewrite_one_error:
			return -1;
		}
		pp = &parent->next;
	}
	remove_duplicate_parents(revs, commit);
	return 0;
}

static int commit_match(struct commit *commit, struct rev_info *opt)
{
	int retval;
	const char *encoding;
	const char *message;
	struct strbuf buf = STRBUF_INIT;

	if (!opt->grep_filter.pattern_list && !opt->grep_filter.header_list)
		return 1;

	/* Prepend "fake" headers as needed */
	if (opt->grep_filter.use_reflog_filter) {
		strbuf_addstr(&buf, "reflog ");
		get_reflog_message(&buf, opt->reflog_info);
		strbuf_addch(&buf, '\n');
	}

	/*
	 * We grep in the user's output encoding, under the assumption that it
	 * is the encoding they are most likely to write their grep pattern
	 * for. In addition, it means we will match the "notes" encoding below,
	 * so we will not end up with a buffer that has two different encodings
	 * in it.
	 */
	encoding = get_log_output_encoding();
	message = repo_logmsg_reencode(the_repository, commit, NULL, encoding);

	/* Copy the commit to temporary if we are using "fake" headers */
	if (buf.len)
		strbuf_addstr(&buf, message);

	if (opt->grep_filter.header_list && opt->mailmap) {
		const char *commit_headers[] = { "author ", "committer ", NULL };

		if (!buf.len)
			strbuf_addstr(&buf, message);

		apply_mailmap_to_header(&buf, commit_headers, opt->mailmap);
	}

	/* Append "fake" message parts as needed */
	if (opt->show_notes) {
		if (!buf.len)
			strbuf_addstr(&buf, message);
		format_display_notes(&commit->object.oid, &buf, encoding, 1);
	}

	/*
	 * Find either in the original commit message, or in the temporary.
	 * Note that we cast away the constness of "message" here. It is
	 * const because it may come from the cached commit buffer. That's OK,
	 * because we know that it is modifiable heap memory, and that while
	 * grep_buffer may modify it for speed, it will restore any
	 * changes before returning.
	 */
	if (buf.len)
		retval = grep_buffer(&opt->grep_filter, buf.buf, buf.len);
	else
		retval = grep_buffer(&opt->grep_filter,
				     (char *)message, strlen(message));
	strbuf_release(&buf);
	repo_unuse_commit_buffer(the_repository, commit, message);
	return retval;
}

static inline int want_ancestry(const struct rev_info *revs)
{
	return (revs->rewrite_parents || revs->children.name);
}

/*
 * Return a timestamp to be used for --since/--until comparisons for this
 * commit, based on the revision options.
 */
static timestamp_t comparison_date(const struct rev_info *revs,
				   struct commit *commit)
{
	return revs->reflog_info ?
		get_reflog_timestamp(revs->reflog_info) :
		commit->date;
}

enum commit_action get_commit_action(struct rev_info *revs, struct commit *commit)
{
	if (commit->object.flags & SHOWN)
		return commit_ignore;
	if (revs->unpacked && has_object_pack(revs->repo, &commit->object.oid))
		return commit_ignore;
	if (revs->no_kept_objects) {
		if (has_object_kept_pack(revs->repo, &commit->object.oid,
					 revs->keep_pack_cache_flags))
			return commit_ignore;
	}
	if (commit->object.flags & UNINTERESTING)
		return commit_ignore;
	if (revs->line_level_traverse && !want_ancestry(revs)) {
		/*
		 * In case of line-level log with parent rewriting
		 * prepare_revision_walk() already took care of all line-level
		 * log filtering, and there is nothing left to do here.
		 *
		 * If parent rewriting was not requested, then this is the
		 * place to perform the line-level log filtering.  Notably,
		 * this check, though expensive, must come before the other,
		 * cheaper filtering conditions, because the tracked line
		 * ranges must be adjusted even when the commit will end up
		 * being ignored based on other conditions.
		 */
		if (!line_log_process_ranges_arbitrary_commit(revs, commit))
			return commit_ignore;
	}
	if (revs->min_age != -1 &&
	    comparison_date(revs, commit) > revs->min_age)
			return commit_ignore;
	if (revs->max_age_as_filter != -1 &&
	    comparison_date(revs, commit) < revs->max_age_as_filter)
			return commit_ignore;
	if (revs->min_parents || (revs->max_parents >= 0)) {
		int n = commit_list_count(commit->parents);
		if ((n < revs->min_parents) ||
		    ((revs->max_parents >= 0) && (n > revs->max_parents)))
			return commit_ignore;
	}
	if (!commit_match(commit, revs))
		return commit_ignore;
	if (revs->prune && revs->dense) {
		/* Commit without changes? */
		if (commit->object.flags & TREESAME) {
			int n;
			struct commit_list *p;
			/* drop merges unless we want parenthood */
			if (!want_ancestry(revs))
				return commit_ignore;

			if (revs->show_pulls && (commit->object.flags & PULL_MERGE))
				return commit_show;

			/*
			 * If we want ancestry, then need to keep any merges
			 * between relevant commits to tie together topology.
			 * For consistency with TREESAME and simplification
			 * use "relevant" here rather than just INTERESTING,
			 * to treat bottom commit(s) as part of the topology.
			 */
			for (n = 0, p = commit->parents; p; p = p->next)
				if (relevant_commit(p->item))
					if (++n >= 2)
						return commit_show;
			return commit_ignore;
		}
	}
	return commit_show;
}

define_commit_slab(saved_parents, struct commit_list *);

#define EMPTY_PARENT_LIST ((struct commit_list *)-1)

/*
 * You may only call save_parents() once per commit (this is checked
 * for non-root commits).
 */
static void save_parents(struct rev_info *revs, struct commit *commit)
{
	struct commit_list **pp;

	if (!revs->saved_parents_slab) {
		revs->saved_parents_slab = xmalloc(sizeof(struct saved_parents));
		init_saved_parents(revs->saved_parents_slab);
	}

	pp = saved_parents_at(revs->saved_parents_slab, commit);

	/*
	 * When walking with reflogs, we may visit the same commit
	 * several times: once for each appearance in the reflog.
	 *
	 * In this case, save_parents() will be called multiple times.
	 * We want to keep only the first set of parents.  We need to
	 * store a sentinel value for an empty (i.e., NULL) parent
	 * list to distinguish it from a not-yet-saved list, however.
	 */
	if (*pp)
		return;
	if (commit->parents)
		*pp = copy_commit_list(commit->parents);
	else
		*pp = EMPTY_PARENT_LIST;
}

static void free_saved_parent(struct commit_list **parents)
{
	if (*parents != EMPTY_PARENT_LIST)
		free_commit_list(*parents);
}

static void free_saved_parents(struct rev_info *revs)
{
	if (!revs->saved_parents_slab)
		return;
	deep_clear_saved_parents(revs->saved_parents_slab, free_saved_parent);
	FREE_AND_NULL(revs->saved_parents_slab);
}

struct commit_list *get_saved_parents(struct rev_info *revs, const struct commit *commit)
{
	struct commit_list *parents;

	if (!revs->saved_parents_slab)
		return commit->parents;

	parents = *saved_parents_at(revs->saved_parents_slab, commit);
	if (parents == EMPTY_PARENT_LIST)
		return NULL;
	return parents;
}

enum commit_action simplify_commit(struct rev_info *revs, struct commit *commit)
{
	enum commit_action action = get_commit_action(revs, commit);

	if (action == commit_show &&
	    revs->prune && revs->dense && want_ancestry(revs)) {
		/*
		 * --full-diff on simplified parents is no good: it
		 * will show spurious changes from the commits that
		 * were elided.  So we save the parents on the side
		 * when --full-diff is in effect.
		 */
		if (revs->full_diff)
			save_parents(revs, commit);
		if (rewrite_parents(revs, commit, rewrite_one) < 0)
			return commit_error;
	}
	return action;
}

static void track_linear(struct rev_info *revs, struct commit *commit)
{
	if (revs->track_first_time) {
		revs->linear = 1;
		revs->track_first_time = 0;
	} else {
		struct commit_list *p;
		for (p = revs->previous_parents; p; p = p->next)
			if (p->item == NULL || /* first commit */
			    oideq(&p->item->object.oid, &commit->object.oid))
				break;
		revs->linear = p != NULL;
	}
	if (revs->reverse) {
		if (revs->linear)
			commit->object.flags |= TRACK_LINEAR;
	}
	free_commit_list(revs->previous_parents);
	revs->previous_parents = copy_commit_list(commit->parents);
}

static struct commit *get_revision_1(struct rev_info *revs)
{
	while (1) {
		struct commit *commit;

		if (revs->reflog_info)
			commit = next_reflog_entry(revs->reflog_info);
		else if (revs->topo_walk_info)
			commit = next_topo_commit(revs);
		else
			commit = pop_commit(&revs->commits);

		if (!commit)
			return NULL;

		if (revs->reflog_info)
			commit->object.flags &= ~(ADDED | SEEN | SHOWN);

		/*
		 * If we haven't done the list limiting, we need to look at
		 * the parents here. We also need to do the date-based limiting
		 * that we'd otherwise have done in limit_list().
		 */
		if (!revs->limited) {
			if (revs->max_age != -1 &&
			    comparison_date(revs, commit) < revs->max_age)
				continue;

			if (revs->reflog_info)
				try_to_simplify_commit(revs, commit);
			else if (revs->topo_walk_info)
				expand_topo_walk(revs, commit);
			else if (process_parents(revs, commit, &revs->commits, NULL) < 0) {
				if (!revs->ignore_missing_links)
					die("Failed to traverse parents of commit %s",
						oid_to_hex(&commit->object.oid));
			}
		}

		switch (simplify_commit(revs, commit)) {
		case commit_ignore:
			continue;
		case commit_error:
			die("Failed to simplify parents of commit %s",
			    oid_to_hex(&commit->object.oid));
		default:
			if (revs->track_linear)
				track_linear(revs, commit);
			return commit;
		}
	}
}

/*
 * Return true for entries that have not yet been shown.  (This is an
 * object_array_each_func_t.)
 */
static int entry_unshown(struct object_array_entry *entry, void *cb_data UNUSED)
{
	return !(entry->item->flags & SHOWN);
}

/*
 * If array is on the verge of a realloc, garbage-collect any entries
 * that have already been shown to try to free up some space.
 */
static void gc_boundary(struct object_array *array)
{
	if (array->nr == array->alloc)
		object_array_filter(array, entry_unshown, NULL);
}

static void create_boundary_commit_list(struct rev_info *revs)
{
	unsigned i;
	struct commit *c;
	struct object_array *array = &revs->boundary_commits;
	struct object_array_entry *objects = array->objects;

	/*
	 * If revs->commits is non-NULL at this point, an error occurred in
	 * get_revision_1().  Ignore the error and continue printing the
	 * boundary commits anyway.  (This is what the code has always
	 * done.)
	 */
	free_commit_list(revs->commits);
	revs->commits = NULL;

	/*
	 * Put all of the actual boundary commits from revs->boundary_commits
	 * into revs->commits
	 */
	for (i = 0; i < array->nr; i++) {
		c = (struct commit *)(objects[i].item);
		if (!c)
			continue;
		if (!(c->object.flags & CHILD_SHOWN))
			continue;
		if (c->object.flags & (SHOWN | BOUNDARY))
			continue;
		c->object.flags |= BOUNDARY;
		commit_list_insert(c, &revs->commits);
	}

	/*
	 * If revs->topo_order is set, sort the boundary commits
	 * in topological order
	 */
	sort_in_topological_order(&revs->commits, revs->sort_order);
}

static struct commit *get_revision_internal(struct rev_info *revs)
{
	struct commit *c = NULL;
	struct commit_list *l;

	if (revs->boundary == 2) {
		/*
		 * All of the normal commits have already been returned,
		 * and we are now returning boundary commits.
		 * create_boundary_commit_list() has populated
		 * revs->commits with the remaining commits to return.
		 */
		c = pop_commit(&revs->commits);
		if (c)
			c->object.flags |= SHOWN;
		return c;
	}

	/*
	 * If our max_count counter has reached zero, then we are done. We
	 * don't simply return NULL because we still might need to show
	 * boundary commits. But we want to avoid calling get_revision_1, which
	 * might do a considerable amount of work finding the next commit only
	 * for us to throw it away.
	 *
	 * If it is non-zero, then either we don't have a max_count at all
	 * (-1), or it is still counting, in which case we decrement.
	 */
	if (revs->max_count) {
		c = get_revision_1(revs);
		if (c) {
			while (revs->skip_count > 0) {
				revs->skip_count--;
				c = get_revision_1(revs);
				if (!c)
					break;
				free_commit_buffer(revs->repo->parsed_objects, c);
			}
		}

		if (revs->max_count > 0)
			revs->max_count--;
	}

	if (c)
		c->object.flags |= SHOWN;

	if (!revs->boundary)
		return c;

	if (!c) {
		/*
		 * get_revision_1() runs out the commits, and
		 * we are done computing the boundaries.
		 * switch to boundary commits output mode.
		 */
		revs->boundary = 2;

		/*
		 * Update revs->commits to contain the list of
		 * boundary commits.
		 */
		create_boundary_commit_list(revs);

		return get_revision_internal(revs);
	}

	/*
	 * boundary commits are the commits that are parents of the
	 * ones we got from get_revision_1() but they themselves are
	 * not returned from get_revision_1().  Before returning
	 * 'c', we need to mark its parents that they could be boundaries.
	 */

	for (l = c->parents; l; l = l->next) {
		struct object *p;
		p = &(l->item->object);
		if (p->flags & (CHILD_SHOWN | SHOWN))
			continue;
		p->flags |= CHILD_SHOWN;
		gc_boundary(&revs->boundary_commits);
		add_object_array(p, NULL, &revs->boundary_commits);
	}

	return c;
}

struct commit *get_revision(struct rev_info *revs)
{
	struct commit *c;
	struct commit_list *reversed;

	if (revs->reverse) {
		reversed = NULL;
		while ((c = get_revision_internal(revs)))
			commit_list_insert(c, &reversed);
		free_commit_list(revs->commits);
		revs->commits = reversed;
		revs->reverse = 0;
		revs->reverse_output_stage = 1;
	}

	if (revs->reverse_output_stage) {
		c = pop_commit(&revs->commits);
		if (revs->track_linear)
			revs->linear = !!(c && c->object.flags & TRACK_LINEAR);
		return c;
	}

	c = get_revision_internal(revs);
	if (c && revs->graph)
		graph_update(revs->graph, c);
	if (!c) {
		free_saved_parents(revs);
		free_commit_list(revs->previous_parents);
		revs->previous_parents = NULL;
	}
	return c;
}

const char *get_revision_mark(const struct rev_info *revs, const struct commit *commit)
{
	if (commit->object.flags & BOUNDARY)
		return "-";
	else if (commit->object.flags & UNINTERESTING)
		return "^";
	else if (commit->object.flags & PATCHSAME)
		return "=";
	else if (!revs || revs->left_right) {
		if (commit->object.flags & SYMMETRIC_LEFT)
			return "<";
		else
			return ">";
	} else if (revs->graph)
		return "*";
	else if (revs->cherry_mark)
		return "+";
	return "";
}

void put_revision_mark(const struct rev_info *revs, const struct commit *commit)
{
	const char *mark = get_revision_mark(revs, commit);
	if (!strlen(mark))
		return;
	fputs(mark, stdout);
	putchar(' ');
}
