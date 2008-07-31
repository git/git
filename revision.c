#include "cache.h"
#include "tag.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "diff.h"
#include "refs.h"
#include "revision.h"
#include "graph.h"
#include "grep.h"
#include "reflog-walk.h"
#include "patch-ids.h"
#include "decorate.h"

volatile show_early_output_fn_t show_early_output;

static char *path_name(struct name_path *path, const char *name)
{
	struct name_path *p;
	char *n, *m;
	int nlen = strlen(name);
	int len = nlen + 1;

	for (p = path; p; p = p->up) {
		if (p->elem_len)
			len += p->elem_len + 1;
	}
	n = xmalloc(len);
	m = n + len - (nlen + 1);
	strcpy(m, name);
	for (p = path; p; p = p->up) {
		if (p->elem_len) {
			m -= p->elem_len + 1;
			memcpy(m, p->elem, p->elem_len);
			m[p->elem_len] = '/';
		}
	}
	return n;
}

void add_object(struct object *obj,
		struct object_array *p,
		struct name_path *path,
		const char *name)
{
	add_object_array(obj, path_name(path, name), p);
}

static void mark_blob_uninteresting(struct blob *blob)
{
	if (!blob)
		return;
	if (blob->object.flags & UNINTERESTING)
		return;
	blob->object.flags |= UNINTERESTING;
}

void mark_tree_uninteresting(struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry entry;
	struct object *obj = &tree->object;

	if (!tree)
		return;
	if (obj->flags & UNINTERESTING)
		return;
	obj->flags |= UNINTERESTING;
	if (!has_sha1_file(obj->sha1))
		return;
	if (parse_tree(tree) < 0)
		die("bad tree %s", sha1_to_hex(obj->sha1));

	init_tree_desc(&desc, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			mark_tree_uninteresting(lookup_tree(entry.sha1));
			break;
		case OBJ_BLOB:
			mark_blob_uninteresting(lookup_blob(entry.sha1));
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
	free(tree->buffer);
	tree->buffer = NULL;
}

void mark_parents_uninteresting(struct commit *commit)
{
	struct commit_list *parents = commit->parents;

	while (parents) {
		struct commit *commit = parents->item;
		if (!(commit->object.flags & UNINTERESTING)) {
			commit->object.flags |= UNINTERESTING;

			/*
			 * Normally we haven't parsed the parent
			 * yet, so we won't have a parent of a parent
			 * here. However, it may turn out that we've
			 * reached this commit some other way (where it
			 * wasn't uninteresting), in which case we need
			 * to mark its parents recursively too..
			 */
			if (commit->parents)
				mark_parents_uninteresting(commit);
		}

		/*
		 * A missing commit is ok iff its parent is marked
		 * uninteresting.
		 *
		 * We just mark such a thing parsed, so that when
		 * it is popped next time around, we won't be trying
		 * to parse it and get an error.
		 */
		if (!has_sha1_file(commit->object.sha1))
			commit->object.parsed = 1;
		parents = parents->next;
	}
}

static void add_pending_object_with_mode(struct rev_info *revs, struct object *obj, const char *name, unsigned mode)
{
	if (revs->no_walk && (obj->flags & UNINTERESTING))
		die("object ranges do not make sense when not walking revisions");
	if (revs->reflog_info && obj->type == OBJ_COMMIT &&
			add_reflog_for_walk(revs->reflog_info,
				(struct commit *)obj, name))
		return;
	add_object_array_with_mode(obj, name, &revs->pending, mode);
}

void add_pending_object(struct rev_info *revs, struct object *obj, const char *name)
{
	add_pending_object_with_mode(revs, obj, name, S_IFINVALID);
}

void add_head_to_pending(struct rev_info *revs)
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

static struct object *get_reference(struct rev_info *revs, const char *name, const unsigned char *sha1, unsigned int flags)
{
	struct object *object;

	object = parse_object(sha1);
	if (!object)
		die("bad object %s", name);
	object->flags |= flags;
	return object;
}

static struct commit *handle_commit(struct rev_info *revs, struct object *object, const char *name)
{
	unsigned long flags = object->flags;

	/*
	 * Tag object? Look what it points to..
	 */
	while (object->type == OBJ_TAG) {
		struct tag *tag = (struct tag *) object;
		if (revs->tag_objects && !(flags & UNINTERESTING))
			add_pending_object(revs, object, tag->tag);
		if (!tag->tagged)
			die("bad tag");
		object = parse_object(tag->tagged->sha1);
		if (!object)
			die("bad object %s", sha1_to_hex(tag->tagged->sha1));
	}

	/*
	 * Commit object? Just return it, we'll do all the complex
	 * reachability crud.
	 */
	if (object->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *)object;
		if (parse_commit(commit) < 0)
			die("unable to parse commit %s", name);
		if (flags & UNINTERESTING) {
			commit->object.flags |= UNINTERESTING;
			mark_parents_uninteresting(commit);
			revs->limited = 1;
		}
		return commit;
	}

	/*
	 * Tree object? Either mark it uniniteresting, or add it
	 * to the list of objects to look at later..
	 */
	if (object->type == OBJ_TREE) {
		struct tree *tree = (struct tree *)object;
		if (!revs->tree_objects)
			return NULL;
		if (flags & UNINTERESTING) {
			mark_tree_uninteresting(tree);
			return NULL;
		}
		add_pending_object(revs, object, "");
		return NULL;
	}

	/*
	 * Blob object? You know the drill by now..
	 */
	if (object->type == OBJ_BLOB) {
		struct blob *blob = (struct blob *)object;
		if (!revs->blob_objects)
			return NULL;
		if (flags & UNINTERESTING) {
			mark_blob_uninteresting(blob);
			return NULL;
		}
		add_pending_object(revs, object, "");
		return NULL;
	}
	die("%s is unknown object", name);
}

static int everybody_uninteresting(struct commit_list *orig)
{
	struct commit_list *list = orig;
	while (list) {
		struct commit *commit = list->item;
		list = list->next;
		if (commit->object.flags & UNINTERESTING)
			continue;
		return 0;
	}
	return 1;
}

/*
 * The goal is to get REV_TREE_NEW as the result only if the
 * diff consists of all '+' (and no other changes), and
 * REV_TREE_DIFFERENT otherwise (of course if the trees are
 * the same we want REV_TREE_SAME).  That means that once we
 * get to REV_TREE_DIFFERENT, we do not have to look any further.
 */
static int tree_difference = REV_TREE_SAME;

static void file_add_remove(struct diff_options *options,
		    int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *fullpath)
{
	int diff = REV_TREE_DIFFERENT;

	/*
	 * Is it an add of a new file? It means that the old tree
	 * didn't have it at all, so we will turn "REV_TREE_SAME" ->
	 * "REV_TREE_NEW", but leave any "REV_TREE_DIFFERENT" alone
	 * (and if it already was "REV_TREE_NEW", we'll keep it
	 * "REV_TREE_NEW" of course).
	 */
	if (addremove == '+') {
		diff = tree_difference;
		if (diff != REV_TREE_SAME)
			return;
		diff = REV_TREE_NEW;
	}
	tree_difference = diff;
	if (tree_difference == REV_TREE_DIFFERENT)
		DIFF_OPT_SET(options, HAS_CHANGES);
}

static void file_change(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *fullpath)
{
	tree_difference = REV_TREE_DIFFERENT;
	DIFF_OPT_SET(options, HAS_CHANGES);
}

static int rev_compare_tree(struct rev_info *revs, struct tree *t1, struct tree *t2)
{
	if (!t1)
		return REV_TREE_NEW;
	if (!t2)
		return REV_TREE_DIFFERENT;
	tree_difference = REV_TREE_SAME;
	DIFF_OPT_CLR(&revs->pruning, HAS_CHANGES);
	if (diff_tree_sha1(t1->object.sha1, t2->object.sha1, "",
			   &revs->pruning) < 0)
		return REV_TREE_DIFFERENT;
	return tree_difference;
}

static int rev_same_tree_as_empty(struct rev_info *revs, struct tree *t1)
{
	int retval;
	void *tree;
	unsigned long size;
	struct tree_desc empty, real;

	if (!t1)
		return 0;

	tree = read_object_with_reference(t1->object.sha1, tree_type, &size, NULL);
	if (!tree)
		return 0;
	init_tree_desc(&real, tree, size);
	init_tree_desc(&empty, "", 0);

	tree_difference = REV_TREE_SAME;
	DIFF_OPT_CLR(&revs->pruning, HAS_CHANGES);
	retval = diff_tree(&empty, &real, "", &revs->pruning);
	free(tree);

	return retval >= 0 && (tree_difference == REV_TREE_SAME);
}

static void try_to_simplify_commit(struct rev_info *revs, struct commit *commit)
{
	struct commit_list **pp, *parent;
	int tree_changed = 0, tree_same = 0;

	/*
	 * If we don't do pruning, everything is interesting
	 */
	if (!revs->prune)
		return;

	if (!commit->tree)
		return;

	if (!commit->parents) {
		if (rev_same_tree_as_empty(revs, commit->tree))
			commit->object.flags |= TREESAME;
		return;
	}

	/*
	 * Normal non-merge commit? If we don't want to make the
	 * history dense, we consider it always to be a change..
	 */
	if (!revs->dense && !commit->parents->next)
		return;

	pp = &commit->parents;
	while ((parent = *pp) != NULL) {
		struct commit *p = parent->item;

		if (parse_commit(p) < 0)
			die("cannot simplify commit %s (because of %s)",
			    sha1_to_hex(commit->object.sha1),
			    sha1_to_hex(p->object.sha1));
		switch (rev_compare_tree(revs, p->tree, commit->tree)) {
		case REV_TREE_SAME:
			tree_same = 1;
			if (!revs->simplify_history || (p->object.flags & UNINTERESTING)) {
				/* Even if a merge with an uninteresting
				 * side branch brought the entire change
				 * we are interested in, we do not want
				 * to lose the other branches of this
				 * merge, so we just keep going.
				 */
				pp = &parent->next;
				continue;
			}
			parent->next = NULL;
			commit->parents = parent;
			commit->object.flags |= TREESAME;
			return;

		case REV_TREE_NEW:
			if (revs->remove_empty_trees &&
			    rev_same_tree_as_empty(revs, p->tree)) {
				/* We are adding all the specified
				 * paths from this parent, so the
				 * history beyond this parent is not
				 * interesting.  Remove its parents
				 * (they are grandparents for us).
				 * IOW, we pretend this parent is a
				 * "root" commit.
				 */
				if (parse_commit(p) < 0)
					die("cannot simplify commit %s (invalid %s)",
					    sha1_to_hex(commit->object.sha1),
					    sha1_to_hex(p->object.sha1));
				p->parents = NULL;
			}
		/* fallthrough */
		case REV_TREE_DIFFERENT:
			tree_changed = 1;
			pp = &parent->next;
			continue;
		}
		die("bad tree compare for commit %s", sha1_to_hex(commit->object.sha1));
	}
	if (tree_changed && !tree_same)
		return;
	commit->object.flags |= TREESAME;
}

static void insert_by_date_cached(struct commit *p, struct commit_list **head,
		    struct commit_list *cached_base, struct commit_list **cache)
{
	struct commit_list *new_entry;

	if (cached_base && p->date < cached_base->item->date)
		new_entry = insert_by_date(p, &cached_base->next);
	else
		new_entry = insert_by_date(p, head);

	if (cache && (!*cache || p->date < (*cache)->item->date))
		*cache = new_entry;
}

static int add_parents_to_list(struct rev_info *revs, struct commit *commit,
		    struct commit_list **list, struct commit_list **cache_ptr)
{
	struct commit_list *parent = commit->parents;
	unsigned left_flag;
	struct commit_list *cached_base = cache_ptr ? *cache_ptr : NULL;

	if (commit->object.flags & ADDED)
		return 0;
	commit->object.flags |= ADDED;

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
			if (parse_commit(p) < 0)
				return -1;
			p->object.flags |= UNINTERESTING;
			if (p->parents)
				mark_parents_uninteresting(p);
			if (p->object.flags & SEEN)
				continue;
			p->object.flags |= SEEN;
			insert_by_date_cached(p, list, cached_base, cache_ptr);
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

	left_flag = (commit->object.flags & SYMMETRIC_LEFT);

	for (parent = commit->parents; parent; parent = parent->next) {
		struct commit *p = parent->item;

		if (parse_commit(p) < 0)
			return -1;
		p->object.flags |= left_flag;
		if (!(p->object.flags & SEEN)) {
			p->object.flags |= SEEN;
			insert_by_date_cached(p, list, cached_base, cache_ptr);
		}
		if(revs->first_parent_only)
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

	left_first = left_count < right_count;
	init_patch_ids(&ids);
	if (revs->diffopt.nr_paths) {
		ids.diffopts.nr_paths = revs->diffopt.nr_paths;
		ids.diffopts.paths = revs->diffopt.paths;
		ids.diffopts.pathlens = revs->diffopt.pathlens;
	}

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
		commit->util = add_commit_patch_id(commit, &ids);
	}

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
		id = has_commit_patch_id(commit, &ids);
		if (!id)
			continue;
		id->seen = 1;
		commit->object.flags |= SHOWN;
	}

	/* Now check the original side for seen ones */
	for (p = list; p; p = p->next) {
		struct commit *commit = p->item;
		struct patch_id *ent;

		ent = commit->util;
		if (!ent)
			continue;
		if (ent->seen)
			commit->object.flags |= SHOWN;
		commit->util = NULL;
	}

	free_patch_ids(&ids);
}

/* How many extra uninteresting commits we want to see.. */
#define SLOP 5

static int still_interesting(struct commit_list *src, unsigned long date, int slop)
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
	if (date < src->item->date)
		return SLOP;

	/*
	 * Does the source list still have interesting commits in
	 * it? Definitely not done..
	 */
	if (!everybody_uninteresting(src))
		return SLOP;

	/* Ok, we're closing in.. */
	return slop-1;
}

static int limit_list(struct rev_info *revs)
{
	int slop = SLOP;
	unsigned long date = ~0ul;
	struct commit_list *list = revs->commits;
	struct commit_list *newlist = NULL;
	struct commit_list **p = &newlist;

	while (list) {
		struct commit_list *entry = list;
		struct commit *commit = list->item;
		struct object *obj = &commit->object;
		show_early_output_fn_t show;

		list = list->next;
		free(entry);

		if (revs->max_age != -1 && (commit->date < revs->max_age))
			obj->flags |= UNINTERESTING;
		if (add_parents_to_list(revs, commit, &list, NULL) < 0)
			return -1;
		if (obj->flags & UNINTERESTING) {
			mark_parents_uninteresting(commit);
			if (revs->show_all)
				p = &commit_list_insert(commit, p)->next;
			slop = still_interesting(list, date, slop);
			if (slop)
				continue;
			/* If showing all, add the whole pending list to the end */
			if (revs->show_all)
				*p = list;
			break;
		}
		if (revs->min_age != -1 && (commit->date > revs->min_age))
			continue;
		date = commit->date;
		p = &commit_list_insert(commit, p)->next;

		show = show_early_output;
		if (!show)
			continue;

		show(revs, newlist);
		show_early_output = NULL;
	}
	if (revs->cherry_pick)
		cherry_pick_list(newlist, revs);

	revs->commits = newlist;
	return 0;
}

struct all_refs_cb {
	int all_flags;
	int warned_bad_reflog;
	struct rev_info *all_revs;
	const char *name_for_errormsg;
};

static int handle_one_ref(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct all_refs_cb *cb = cb_data;
	struct object *object = get_reference(cb->all_revs, path, sha1,
					      cb->all_flags);
	add_pending_object(cb->all_revs, object, path);
	return 0;
}

static void handle_refs(struct rev_info *revs, unsigned flags,
		int (*for_each)(each_ref_fn, void *))
{
	struct all_refs_cb cb;
	cb.all_revs = revs;
	cb.all_flags = flags;
	for_each(handle_one_ref, &cb);
}

static void handle_one_reflog_commit(unsigned char *sha1, void *cb_data)
{
	struct all_refs_cb *cb = cb_data;
	if (!is_null_sha1(sha1)) {
		struct object *o = parse_object(sha1);
		if (o) {
			o->flags |= cb->all_flags;
			add_pending_object(cb->all_revs, o, "");
		}
		else if (!cb->warned_bad_reflog) {
			warning("reflog of '%s' references pruned commits",
				cb->name_for_errormsg);
			cb->warned_bad_reflog = 1;
		}
	}
}

static int handle_one_reflog_ent(unsigned char *osha1, unsigned char *nsha1,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	handle_one_reflog_commit(osha1, cb_data);
	handle_one_reflog_commit(nsha1, cb_data);
	return 0;
}

static int handle_one_reflog(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct all_refs_cb *cb = cb_data;
	cb->warned_bad_reflog = 0;
	cb->name_for_errormsg = path;
	for_each_reflog_ent(path, handle_one_reflog_ent, cb_data);
	return 0;
}

static void handle_reflog(struct rev_info *revs, unsigned flags)
{
	struct all_refs_cb cb;
	cb.all_revs = revs;
	cb.all_flags = flags;
	for_each_reflog(handle_one_reflog, &cb);
}

static int add_parents_only(struct rev_info *revs, const char *arg, int flags)
{
	unsigned char sha1[20];
	struct object *it;
	struct commit *commit;
	struct commit_list *parents;

	if (*arg == '^') {
		flags ^= UNINTERESTING;
		arg++;
	}
	if (get_sha1(arg, sha1))
		return 0;
	while (1) {
		it = get_reference(revs, arg, sha1, 0);
		if (it->type != OBJ_TAG)
			break;
		if (!((struct tag*)it)->tagged)
			return 0;
		hashcpy(sha1, ((struct tag*)it)->tagged->sha1);
	}
	if (it->type != OBJ_COMMIT)
		return 0;
	commit = (struct commit *)it;
	for (parents = commit->parents; parents; parents = parents->next) {
		it = &parents->item->object;
		it->flags |= flags;
		add_pending_object(revs, it, arg);
	}
	return 1;
}

void init_revisions(struct rev_info *revs, const char *prefix)
{
	memset(revs, 0, sizeof(*revs));

	revs->abbrev = DEFAULT_ABBREV;
	revs->ignore_merges = 1;
	revs->simplify_history = 1;
	DIFF_OPT_SET(&revs->pruning, RECURSIVE);
	DIFF_OPT_SET(&revs->pruning, QUIET);
	revs->pruning.add_remove = file_add_remove;
	revs->pruning.change = file_change;
	revs->lifo = 1;
	revs->dense = 1;
	revs->prefix = prefix;
	revs->max_age = -1;
	revs->min_age = -1;
	revs->skip_count = -1;
	revs->max_count = -1;

	revs->commit_format = CMIT_FMT_DEFAULT;

	diff_setup(&revs->diffopt);
	if (prefix && !revs->diffopt.prefix) {
		revs->diffopt.prefix = prefix;
		revs->diffopt.prefix_length = strlen(prefix);
	}
}

static void add_pending_commit_list(struct rev_info *revs,
                                    struct commit_list *commit_list,
                                    unsigned int flags)
{
	while (commit_list) {
		struct object *object = &commit_list->item->object;
		object->flags |= flags;
		add_pending_object(revs, object, sha1_to_hex(object->sha1));
		commit_list = commit_list->next;
	}
}

static void prepare_show_merge(struct rev_info *revs)
{
	struct commit_list *bases;
	struct commit *head, *other;
	unsigned char sha1[20];
	const char **prune = NULL;
	int i, prune_num = 1; /* counting terminating NULL */

	if (get_sha1("HEAD", sha1) || !(head = lookup_commit(sha1)))
		die("--merge without HEAD?");
	if (get_sha1("MERGE_HEAD", sha1) || !(other = lookup_commit(sha1)))
		die("--merge without MERGE_HEAD?");
	add_pending_object(revs, &head->object, "HEAD");
	add_pending_object(revs, &other->object, "MERGE_HEAD");
	bases = get_merge_bases(head, other, 1);
	add_pending_commit_list(revs, bases, UNINTERESTING);
	free_commit_list(bases);
	head->object.flags |= SYMMETRIC_LEFT;

	if (!active_nr)
		read_cache();
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!ce_stage(ce))
			continue;
		if (ce_path_match(ce, revs->prune_data)) {
			prune_num++;
			prune = xrealloc(prune, sizeof(*prune) * prune_num);
			prune[prune_num-2] = ce->name;
			prune[prune_num-1] = NULL;
		}
		while ((i+1 < active_nr) &&
		       ce_same_name(ce, active_cache[i+1]))
			i++;
	}
	revs->prune_data = prune;
	revs->limited = 1;
}

int handle_revision_arg(const char *arg, struct rev_info *revs,
			int flags,
			int cant_be_filename)
{
	unsigned mode;
	char *dotdot;
	struct object *object;
	unsigned char sha1[20];
	int local_flags;

	dotdot = strstr(arg, "..");
	if (dotdot) {
		unsigned char from_sha1[20];
		const char *next = dotdot + 2;
		const char *this = arg;
		int symmetric = *next == '.';
		unsigned int flags_exclude = flags ^ UNINTERESTING;

		*dotdot = 0;
		next += symmetric;

		if (!*next)
			next = "HEAD";
		if (dotdot == arg)
			this = "HEAD";
		if (!get_sha1(this, from_sha1) &&
		    !get_sha1(next, sha1)) {
			struct commit *a, *b;
			struct commit_list *exclude;

			a = lookup_commit_reference(from_sha1);
			b = lookup_commit_reference(sha1);
			if (!a || !b) {
				die(symmetric ?
				    "Invalid symmetric difference expression %s...%s" :
				    "Invalid revision range %s..%s",
				    arg, next);
			}

			if (!cant_be_filename) {
				*dotdot = '.';
				verify_non_filename(revs->prefix, arg);
			}

			if (symmetric) {
				exclude = get_merge_bases(a, b, 1);
				add_pending_commit_list(revs, exclude,
							flags_exclude);
				free_commit_list(exclude);
				a->object.flags |= flags | SYMMETRIC_LEFT;
			} else
				a->object.flags |= flags_exclude;
			b->object.flags |= flags;
			add_pending_object(revs, &a->object, this);
			add_pending_object(revs, &b->object, next);
			return 0;
		}
		*dotdot = '.';
	}
	dotdot = strstr(arg, "^@");
	if (dotdot && !dotdot[2]) {
		*dotdot = 0;
		if (add_parents_only(revs, arg, flags))
			return 0;
		*dotdot = '^';
	}
	dotdot = strstr(arg, "^!");
	if (dotdot && !dotdot[2]) {
		*dotdot = 0;
		if (!add_parents_only(revs, arg, flags ^ UNINTERESTING))
			*dotdot = '^';
	}

	local_flags = 0;
	if (*arg == '^') {
		local_flags = UNINTERESTING;
		arg++;
	}
	if (get_sha1_with_mode(arg, sha1, &mode))
		return -1;
	if (!cant_be_filename)
		verify_non_filename(revs->prefix, arg);
	object = get_reference(revs, arg, sha1, flags ^ local_flags);
	add_pending_object_with_mode(revs, object, arg, mode);
	return 0;
}

void read_revisions_from_stdin(struct rev_info *revs)
{
	char line[1000];

	while (fgets(line, sizeof(line), stdin) != NULL) {
		int len = strlen(line);
		if (len && line[len - 1] == '\n')
			line[--len] = '\0';
		if (!len)
			break;
		if (line[0] == '-')
			die("options not supported in --stdin mode");
		if (handle_revision_arg(line, revs, 0, 1))
			die("bad revision '%s'", line);
	}
}

static void add_grep(struct rev_info *revs, const char *ptn, enum grep_pat_token what)
{
	if (!revs->grep_filter) {
		struct grep_opt *opt = xcalloc(1, sizeof(*opt));
		opt->status_only = 1;
		opt->pattern_tail = &(opt->pattern_list);
		opt->regflags = REG_NEWLINE;
		revs->grep_filter = opt;
	}
	append_grep_pattern(revs->grep_filter, ptn,
			    "command line", 0, what);
}

static void add_header_grep(struct rev_info *revs, const char *field, const char *pattern)
{
	char *pat;
	const char *prefix;
	int patlen, fldlen;

	fldlen = strlen(field);
	patlen = strlen(pattern);
	pat = xmalloc(patlen + fldlen + 10);
	prefix = ".*";
	if (*pattern == '^') {
		prefix = "";
		pattern++;
	}
	sprintf(pat, "^%s %s%s", field, prefix, pattern);
	add_grep(revs, pat, GREP_PATTERN_HEAD);
}

static void add_message_grep(struct rev_info *revs, const char *pattern)
{
	add_grep(revs, pattern, GREP_PATTERN_BODY);
}

static void add_ignore_packed(struct rev_info *revs, const char *name)
{
	int num = ++revs->num_ignore_packed;

	revs->ignore_packed = xrealloc(revs->ignore_packed,
				       sizeof(const char **) * (num + 1));
	revs->ignore_packed[num-1] = name;
	revs->ignore_packed[num] = NULL;
}

static int handle_revision_opt(struct rev_info *revs, int argc, const char **argv,
			       int *unkc, const char **unkv)
{
	const char *arg = argv[0];

	/* pseudo revision arguments */
	if (!strcmp(arg, "--all") || !strcmp(arg, "--branches") ||
	    !strcmp(arg, "--tags") || !strcmp(arg, "--remotes") ||
	    !strcmp(arg, "--reflog") || !strcmp(arg, "--not") ||
	    !strcmp(arg, "--no-walk") || !strcmp(arg, "--do-walk"))
	{
		unkv[(*unkc)++] = arg;
		return 1;
	}

	if (!prefixcmp(arg, "--max-count=")) {
		revs->max_count = atoi(arg + 12);
	} else if (!prefixcmp(arg, "--skip=")) {
		revs->skip_count = atoi(arg + 7);
	} else if ((*arg == '-') && isdigit(arg[1])) {
	/* accept -<digit>, like traditional "head" */
		revs->max_count = atoi(arg + 1);
	} else if (!strcmp(arg, "-n")) {
		if (argc <= 1)
			return error("-n requires an argument");
		revs->max_count = atoi(argv[1]);
		return 2;
	} else if (!prefixcmp(arg, "-n")) {
		revs->max_count = atoi(arg + 2);
	} else if (!prefixcmp(arg, "--max-age=")) {
		revs->max_age = atoi(arg + 10);
	} else if (!prefixcmp(arg, "--since=")) {
		revs->max_age = approxidate(arg + 8);
	} else if (!prefixcmp(arg, "--after=")) {
		revs->max_age = approxidate(arg + 8);
	} else if (!prefixcmp(arg, "--min-age=")) {
		revs->min_age = atoi(arg + 10);
	} else if (!prefixcmp(arg, "--before=")) {
		revs->min_age = approxidate(arg + 9);
	} else if (!prefixcmp(arg, "--until=")) {
		revs->min_age = approxidate(arg + 8);
	} else if (!strcmp(arg, "--first-parent")) {
		revs->first_parent_only = 1;
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
		revs->lifo = 1;
		revs->topo_order = 1;
	} else if (!strcmp(arg, "--date-order")) {
		revs->lifo = 0;
		revs->topo_order = 1;
	} else if (!prefixcmp(arg, "--early-output")) {
		int count = 100;
		switch (arg[14]) {
		case '=':
			count = atoi(arg+15);
			/* Fallthrough */
		case 0:
			revs->topo_order = 1;
		       revs->early_output = count;
		}
	} else if (!strcmp(arg, "--parents")) {
		revs->rewrite_parents = 1;
		revs->print_parents = 1;
	} else if (!strcmp(arg, "--dense")) {
		revs->dense = 1;
	} else if (!strcmp(arg, "--sparse")) {
		revs->dense = 0;
	} else if (!strcmp(arg, "--show-all")) {
		revs->show_all = 1;
	} else if (!strcmp(arg, "--remove-empty")) {
		revs->remove_empty_trees = 1;
	} else if (!strcmp(arg, "--no-merges")) {
		revs->no_merges = 1;
	} else if (!strcmp(arg, "--boundary")) {
		revs->boundary = 1;
	} else if (!strcmp(arg, "--left-right")) {
		revs->left_right = 1;
	} else if (!strcmp(arg, "--cherry-pick")) {
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
	} else if (!strcmp(arg, "--unpacked")) {
		revs->unpacked = 1;
		free(revs->ignore_packed);
		revs->ignore_packed = NULL;
		revs->num_ignore_packed = 0;
	} else if (!prefixcmp(arg, "--unpacked=")) {
		revs->unpacked = 1;
		add_ignore_packed(revs, arg+11);
	} else if (!strcmp(arg, "-r")) {
		revs->diff = 1;
		DIFF_OPT_SET(&revs->diffopt, RECURSIVE);
	} else if (!strcmp(arg, "-t")) {
		revs->diff = 1;
		DIFF_OPT_SET(&revs->diffopt, RECURSIVE);
		DIFF_OPT_SET(&revs->diffopt, TREE_IN_RECURSIVE);
	} else if (!strcmp(arg, "-m")) {
		revs->ignore_merges = 0;
	} else if (!strcmp(arg, "-c")) {
		revs->diff = 1;
		revs->dense_combined_merges = 0;
		revs->combine_merges = 1;
	} else if (!strcmp(arg, "--cc")) {
		revs->diff = 1;
		revs->dense_combined_merges = 1;
		revs->combine_merges = 1;
	} else if (!strcmp(arg, "-v")) {
		revs->verbose_header = 1;
	} else if (!strcmp(arg, "--pretty")) {
		revs->verbose_header = 1;
		get_commit_format(arg+8, revs);
	} else if (!prefixcmp(arg, "--pretty=")) {
		revs->verbose_header = 1;
		get_commit_format(arg+9, revs);
	} else if (!strcmp(arg, "--graph")) {
		revs->topo_order = 1;
		revs->rewrite_parents = 1;
		revs->graph = graph_init(revs);
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
	} else if (!prefixcmp(arg, "--abbrev=")) {
		revs->abbrev = strtoul(arg + 9, NULL, 10);
		if (revs->abbrev < MINIMUM_ABBREV)
			revs->abbrev = MINIMUM_ABBREV;
		else if (revs->abbrev > 40)
			revs->abbrev = 40;
	} else if (!strcmp(arg, "--abbrev-commit")) {
		revs->abbrev_commit = 1;
	} else if (!strcmp(arg, "--full-diff")) {
		revs->diff = 1;
		revs->full_diff = 1;
	} else if (!strcmp(arg, "--full-history")) {
		revs->simplify_history = 0;
	} else if (!strcmp(arg, "--relative-date")) {
		revs->date_mode = DATE_RELATIVE;
	} else if (!strncmp(arg, "--date=", 7)) {
		revs->date_mode = parse_date_format(arg + 7);
	} else if (!strcmp(arg, "--log-size")) {
		revs->show_log_size = 1;
	}
	/*
	 * Grepping the commit log
	 */
	else if (!prefixcmp(arg, "--author=")) {
		add_header_grep(revs, "author", arg+9);
	} else if (!prefixcmp(arg, "--committer=")) {
		add_header_grep(revs, "committer", arg+12);
	} else if (!prefixcmp(arg, "--grep=")) {
		add_message_grep(revs, arg+7);
	} else if (!strcmp(arg, "--extended-regexp") || !strcmp(arg, "-E")) {
		if (revs->grep_filter)
			revs->grep_filter->regflags |= REG_EXTENDED;
	} else if (!strcmp(arg, "--regexp-ignore-case") || !strcmp(arg, "-i")) {
		if (revs->grep_filter)
			revs->grep_filter->regflags |= REG_ICASE;
	} else if (!strcmp(arg, "--fixed-strings") || !strcmp(arg, "-F")) {
		if (revs->grep_filter)
			revs->grep_filter->fixed = 1;
	} else if (!strcmp(arg, "--all-match")) {
		if (revs->grep_filter)
			revs->grep_filter->all_match = 1;
	} else if (!prefixcmp(arg, "--encoding=")) {
		arg += 11;
		if (strcmp(arg, "none"))
			git_log_output_encoding = xstrdup(arg);
		else
			git_log_output_encoding = "";
	} else if (!strcmp(arg, "--reverse")) {
		revs->reverse ^= 1;
	} else if (!strcmp(arg, "--children")) {
		revs->children.name = "children";
		revs->limited = 1;
	} else {
		int opts = diff_opt_parse(&revs->diffopt, argv, argc);
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
				    &ctx->cpidx, ctx->out);
	if (n <= 0) {
		error("unknown option `%s'", ctx->argv[0]);
		usage_with_options(usagestr, options);
	}
	ctx->argv += n;
	ctx->argc -= n;
}

/*
 * Parse revision information, filling in the "rev_info" structure,
 * and removing the used arguments from the argument list.
 *
 * Returns the number of arguments left that weren't recognized
 * (which are also moved to the head of the argument list)
 */
int setup_revisions(int argc, const char **argv, struct rev_info *revs, const char *def)
{
	int i, flags, left, seen_dashdash;

	/* First, search for "--" */
	seen_dashdash = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "--"))
			continue;
		argv[i] = NULL;
		argc = i;
		if (argv[i + 1])
			revs->prune_data = get_pathspec(revs->prefix, argv + i + 1);
		seen_dashdash = 1;
		break;
	}

	/* Second, deal with arguments and options */
	flags = 0;
	for (left = i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (*arg == '-') {
			int opts;

			if (!strcmp(arg, "--all")) {
				handle_refs(revs, flags, for_each_ref);
				continue;
			}
			if (!strcmp(arg, "--branches")) {
				handle_refs(revs, flags, for_each_branch_ref);
				continue;
			}
			if (!strcmp(arg, "--tags")) {
				handle_refs(revs, flags, for_each_tag_ref);
				continue;
			}
			if (!strcmp(arg, "--remotes")) {
				handle_refs(revs, flags, for_each_remote_ref);
				continue;
			}
			if (!strcmp(arg, "--reflog")) {
				handle_reflog(revs, flags);
				continue;
			}
			if (!strcmp(arg, "--not")) {
				flags ^= UNINTERESTING;
				continue;
			}
			if (!strcmp(arg, "--no-walk")) {
				revs->no_walk = 1;
				continue;
			}
			if (!strcmp(arg, "--do-walk")) {
				revs->no_walk = 0;
				continue;
			}

			opts = handle_revision_opt(revs, argc - i, argv + i, &left, argv);
			if (opts > 0) {
				i += opts - 1;
				continue;
			}
			if (opts < 0)
				exit(128);
			continue;
		}

		if (handle_revision_arg(arg, revs, flags, seen_dashdash)) {
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
				verify_filename(revs->prefix, argv[j]);

			revs->prune_data = get_pathspec(revs->prefix,
							argv + i);
			break;
		}
	}

	if (revs->def == NULL)
		revs->def = def;
	if (revs->show_merge)
		prepare_show_merge(revs);
	if (revs->def && !revs->pending.nr) {
		unsigned char sha1[20];
		struct object *object;
		unsigned mode;
		if (get_sha1_with_mode(revs->def, sha1, &mode))
			die("bad default revision '%s'", revs->def);
		object = get_reference(revs, revs->def, sha1, 0);
		add_pending_object_with_mode(revs, object, revs->def, mode);
	}

	/* Did the user ask for any diff output? Run the diff! */
	if (revs->diffopt.output_format & ~DIFF_FORMAT_NO_OUTPUT)
		revs->diff = 1;

	/* Pickaxe, diff-filter and rename following need diffs */
	if (revs->diffopt.pickaxe ||
	    revs->diffopt.filter ||
	    DIFF_OPT_TST(&revs->diffopt, FOLLOW_RENAMES))
		revs->diff = 1;

	if (revs->topo_order)
		revs->limited = 1;

	if (revs->prune_data) {
		diff_tree_setup_paths(revs->prune_data, &revs->pruning);
		/* Can't prune commits with rename following: the paths change.. */
		if (!DIFF_OPT_TST(&revs->diffopt, FOLLOW_RENAMES))
			revs->prune = 1;
		if (!revs->full_diff)
			diff_tree_setup_paths(revs->prune_data, &revs->diffopt);
	}
	if (revs->combine_merges) {
		revs->ignore_merges = 0;
		if (revs->dense_combined_merges && !revs->diffopt.output_format)
			revs->diffopt.output_format = DIFF_FORMAT_PATCH;
	}
	revs->diffopt.abbrev = revs->abbrev;
	if (diff_setup_done(&revs->diffopt) < 0)
		die("diff_setup_done failed");

	if (revs->grep_filter) {
		compile_grep_patterns(revs->grep_filter);
	}

	if (revs->reverse && revs->reflog_info)
		die("cannot combine --reverse with --walk-reflogs");
	if (revs->rewrite_parents && revs->children.name)
		die("cannot combine --parents and --children");

	/*
	 * Limitations on the graph functionality
	 */
	if (revs->reverse && revs->graph)
		die("cannot combine --reverse with --graph");

	if (revs->reflog_info && revs->graph)
		die("cannot combine --walk-reflogs with --graph");

	return left;
}

static void add_child(struct rev_info *revs, struct commit *parent, struct commit *child)
{
	struct commit_list *l = xcalloc(1, sizeof(*l));

	l->item = child;
	l->next = add_decoration(&revs->children, &parent->object, l);
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

int prepare_revision_walk(struct rev_info *revs)
{
	int nr = revs->pending.nr;
	struct object_array_entry *e, *list;

	e = list = revs->pending.objects;
	revs->pending.nr = 0;
	revs->pending.alloc = 0;
	revs->pending.objects = NULL;
	while (--nr >= 0) {
		struct commit *commit = handle_commit(revs, e->item, e->name);
		if (commit) {
			if (!(commit->object.flags & SEEN)) {
				commit->object.flags |= SEEN;
				insert_by_date(commit, &revs->commits);
			}
		}
		e++;
	}
	free(list);

	if (revs->no_walk)
		return 0;
	if (revs->limited)
		if (limit_list(revs) < 0)
			return -1;
	if (revs->topo_order)
		sort_in_topological_order(&revs->commits, revs->lifo);
	if (revs->children.name)
		set_children(revs);
	return 0;
}

enum rewrite_result {
	rewrite_one_ok,
	rewrite_one_noparents,
	rewrite_one_error,
};

static enum rewrite_result rewrite_one(struct rev_info *revs, struct commit **pp)
{
	struct commit_list *cache = NULL;

	for (;;) {
		struct commit *p = *pp;
		if (!revs->limited)
			if (add_parents_to_list(revs, p, &revs->commits, &cache) < 0)
				return rewrite_one_error;
		if (p->parents && p->parents->next)
			return rewrite_one_ok;
		if (p->object.flags & UNINTERESTING)
			return rewrite_one_ok;
		if (!(p->object.flags & TREESAME))
			return rewrite_one_ok;
		if (!p->parents)
			return rewrite_one_noparents;
		*pp = p->parents->item;
	}
}

static void remove_duplicate_parents(struct commit *commit)
{
	struct commit_list **pp, *p;

	/* Examine existing parents while marking ones we have seen... */
	pp = &commit->parents;
	while ((p = *pp) != NULL) {
		struct commit *parent = p->item;
		if (parent->object.flags & TMP_MARK) {
			*pp = p->next;
			continue;
		}
		parent->object.flags |= TMP_MARK;
		pp = &p->next;
	}
	/* ... and clear the temporary mark */
	for (p = commit->parents; p; p = p->next)
		p->item->object.flags &= ~TMP_MARK;
}

static int rewrite_parents(struct rev_info *revs, struct commit *commit)
{
	struct commit_list **pp = &commit->parents;
	while (*pp) {
		struct commit_list *parent = *pp;
		switch (rewrite_one(revs, &parent->item)) {
		case rewrite_one_ok:
			break;
		case rewrite_one_noparents:
			*pp = parent->next;
			continue;
		case rewrite_one_error:
			return -1;
		}
		pp = &parent->next;
	}
	remove_duplicate_parents(commit);
	return 0;
}

static int commit_match(struct commit *commit, struct rev_info *opt)
{
	if (!opt->grep_filter)
		return 1;
	return grep_buffer(opt->grep_filter,
			   NULL, /* we say nothing, not even filename */
			   commit->buffer, strlen(commit->buffer));
}

static inline int want_ancestry(struct rev_info *revs)
{
	return (revs->rewrite_parents || revs->children.name);
}

enum commit_action simplify_commit(struct rev_info *revs, struct commit *commit)
{
	if (commit->object.flags & SHOWN)
		return commit_ignore;
	if (revs->unpacked && has_sha1_pack(commit->object.sha1, revs->ignore_packed))
		return commit_ignore;
	if (revs->show_all)
		return commit_show;
	if (commit->object.flags & UNINTERESTING)
		return commit_ignore;
	if (revs->min_age != -1 && (commit->date > revs->min_age))
		return commit_ignore;
	if (revs->no_merges && commit->parents && commit->parents->next)
		return commit_ignore;
	if (!commit_match(commit, revs))
		return commit_ignore;
	if (revs->prune && revs->dense) {
		/* Commit without changes? */
		if (commit->object.flags & TREESAME) {
			/* drop merges unless we want parenthood */
			if (!want_ancestry(revs))
				return commit_ignore;
			/* non-merge - always ignore it */
			if (!commit->parents || !commit->parents->next)
				return commit_ignore;
		}
		if (want_ancestry(revs) && rewrite_parents(revs, commit) < 0)
			return commit_error;
	}
	return commit_show;
}

static struct commit *get_revision_1(struct rev_info *revs)
{
	if (!revs->commits)
		return NULL;

	do {
		struct commit_list *entry = revs->commits;
		struct commit *commit = entry->item;

		revs->commits = entry->next;
		free(entry);

		if (revs->reflog_info)
			fake_reflog_parent(revs->reflog_info, commit);

		/*
		 * If we haven't done the list limiting, we need to look at
		 * the parents here. We also need to do the date-based limiting
		 * that we'd otherwise have done in limit_list().
		 */
		if (!revs->limited) {
			if (revs->max_age != -1 &&
			    (commit->date < revs->max_age))
				continue;
			if (add_parents_to_list(revs, commit, &revs->commits, NULL) < 0)
				return NULL;
		}

		switch (simplify_commit(revs, commit)) {
		case commit_ignore:
			continue;
		case commit_error:
			return NULL;
		default:
			return commit;
		}
	} while (revs->commits);
	return NULL;
}

static void gc_boundary(struct object_array *array)
{
	unsigned nr = array->nr;
	unsigned alloc = array->alloc;
	struct object_array_entry *objects = array->objects;

	if (alloc <= nr) {
		unsigned i, j;
		for (i = j = 0; i < nr; i++) {
			if (objects[i].item->flags & SHOWN)
				continue;
			if (i != j)
				objects[j] = objects[i];
			j++;
		}
		for (i = j; i < nr; i++)
			objects[i].item = NULL;
		array->nr = j;
	}
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
	if (revs->commits) {
		free_commit_list(revs->commits);
		revs->commits = NULL;
	}

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
	sort_in_topological_order(&revs->commits, revs->lifo);
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

	if (revs->reverse) {
		int limit = -1;

		if (0 <= revs->max_count) {
			limit = revs->max_count;
			if (0 < revs->skip_count)
				limit += revs->skip_count;
		}
		l = NULL;
		while ((c = get_revision_1(revs))) {
			commit_list_insert(c, &l);
			if ((0 < limit) && !--limit)
				break;
		}
		revs->commits = l;
		revs->reverse = 0;
		revs->max_count = -1;
		c = NULL;
	}

	/*
	 * Now pick up what they want to give us
	 */
	c = get_revision_1(revs);
	if (c) {
		while (0 < revs->skip_count) {
			revs->skip_count--;
			c = get_revision_1(revs);
			if (!c)
				break;
		}
	}

	/*
	 * Check the max_count.
	 */
	switch (revs->max_count) {
	case -1:
		break;
	case 0:
		c = NULL;
		break;
	default:
		revs->max_count--;
	}

	if (c)
		c->object.flags |= SHOWN;

	if (!revs->boundary) {
		return c;
	}

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
	struct commit *c = get_revision_internal(revs);
	if (c && revs->graph)
		graph_update(revs->graph, c);
	return c;
}
