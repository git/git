#include "cache.h"
#include "tag.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "diff.h"
#include "refs.h"
#include "revision.h"

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

struct object_list **add_object(struct object *obj,
				       struct object_list **p,
				       struct name_path *path,
				       const char *name)
{
	struct object_list *entry = xmalloc(sizeof(*entry));
	entry->item = obj;
	entry->next = *p;
	entry->name = path_name(path, name);
	*p = entry;
	return &entry->next;
}

static void mark_blob_uninteresting(struct blob *blob)
{
	if (blob->object.flags & UNINTERESTING)
		return;
	blob->object.flags |= UNINTERESTING;
}

void mark_tree_uninteresting(struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry entry;
	struct object *obj = &tree->object;

	if (obj->flags & UNINTERESTING)
		return;
	obj->flags |= UNINTERESTING;
	if (!has_sha1_file(obj->sha1))
		return;
	if (parse_tree(tree) < 0)
		die("bad tree %s", sha1_to_hex(obj->sha1));

	desc.buf = tree->buffer;
	desc.size = tree->size;
	while (tree_entry(&desc, &entry)) {
		if (S_ISDIR(entry.mode))
			mark_tree_uninteresting(lookup_tree(entry.sha1));
		else
			mark_blob_uninteresting(lookup_blob(entry.sha1));
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

static void add_pending_object(struct rev_info *revs, struct object *obj, const char *name)
{
	add_object(obj, &revs->pending_objects, NULL, name);
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
	while (object->type == TYPE_TAG) {
		struct tag *tag = (struct tag *) object;
		if (revs->tag_objects && !(flags & UNINTERESTING))
			add_pending_object(revs, object, tag->tag);
		object = parse_object(tag->tagged->sha1);
		if (!object)
			die("bad object %s", sha1_to_hex(tag->tagged->sha1));
	}

	/*
	 * Commit object? Just return it, we'll do all the complex
	 * reachability crud.
	 */
	if (object->type == TYPE_COMMIT) {
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
	if (object->type == TYPE_TREE) {
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
	if (object->type == TYPE_BLOB) {
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

static int tree_difference = REV_TREE_SAME;

static void file_add_remove(struct diff_options *options,
		    int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *base, const char *path)
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
}

static void file_change(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *base, const char *path)
{
	tree_difference = REV_TREE_DIFFERENT;
}

int rev_compare_tree(struct rev_info *revs, struct tree *t1, struct tree *t2)
{
	if (!t1)
		return REV_TREE_NEW;
	if (!t2)
		return REV_TREE_DIFFERENT;
	tree_difference = REV_TREE_SAME;
	if (diff_tree_sha1(t1->object.sha1, t2->object.sha1, "",
			   &revs->pruning) < 0)
		return REV_TREE_DIFFERENT;
	return tree_difference;
}

int rev_same_tree_as_empty(struct rev_info *revs, struct tree *t1)
{
	int retval;
	void *tree;
	struct tree_desc empty, real;

	if (!t1)
		return 0;

	tree = read_object_with_reference(t1->object.sha1, tree_type, &real.size, NULL);
	if (!tree)
		return 0;
	real.buf = tree;

	empty.buf = "";
	empty.size = 0;

	tree_difference = 0;
	retval = diff_tree(&empty, &real, "", &revs->pruning);
	free(tree);

	return retval >= 0 && !tree_difference;
}

static void try_to_simplify_commit(struct rev_info *revs, struct commit *commit)
{
	struct commit_list **pp, *parent;
	int tree_changed = 0;

	if (!commit->tree)
		return;

	if (!commit->parents) {
		if (!rev_same_tree_as_empty(revs, commit->tree))
			commit->object.flags |= TREECHANGE;
		return;
	}

	pp = &commit->parents;
	while ((parent = *pp) != NULL) {
		struct commit *p = parent->item;

		parse_commit(p);
		switch (rev_compare_tree(revs, p->tree, commit->tree)) {
		case REV_TREE_SAME:
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
				parse_commit(p);
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
	if (tree_changed)
		commit->object.flags |= TREECHANGE;
}

static void add_parents_to_list(struct rev_info *revs, struct commit *commit, struct commit_list **list)
{
	struct commit_list *parent = commit->parents;

	if (commit->object.flags & ADDED)
		return;
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
			parse_commit(p);
			p->object.flags |= UNINTERESTING;
			if (p->parents)
				mark_parents_uninteresting(p);
			if (p->object.flags & SEEN)
				continue;
			p->object.flags |= SEEN;
			insert_by_date(p, list);
		}
		return;
	}

	/*
	 * Ok, the commit wasn't uninteresting. Try to
	 * simplify the commit history and find the parent
	 * that has no differences in the path set if one exists.
	 */
	if (revs->prune_fn)
		revs->prune_fn(revs, commit);

	if (revs->no_walk)
		return;

	parent = commit->parents;
	while (parent) {
		struct commit *p = parent->item;

		parent = parent->next;

		parse_commit(p);
		if (p->object.flags & SEEN)
			continue;
		p->object.flags |= SEEN;
		insert_by_date(p, list);
	}
}

static void limit_list(struct rev_info *revs)
{
	struct commit_list *list = revs->commits;
	struct commit_list *newlist = NULL;
	struct commit_list **p = &newlist;

	while (list) {
		struct commit_list *entry = list;
		struct commit *commit = list->item;
		struct object *obj = &commit->object;

		list = list->next;
		free(entry);

		if (revs->max_age != -1 && (commit->date < revs->max_age))
			obj->flags |= UNINTERESTING;
		if (revs->unpacked && has_sha1_pack(obj->sha1))
			obj->flags |= UNINTERESTING;
		add_parents_to_list(revs, commit, &list);
		if (obj->flags & UNINTERESTING) {
			mark_parents_uninteresting(commit);
			if (everybody_uninteresting(list))
				break;
			continue;
		}
		if (revs->min_age != -1 && (commit->date > revs->min_age))
			continue;
		p = &commit_list_insert(commit, p)->next;
	}
	if (revs->boundary) {
		/* mark the ones that are on the result list first */
		for (list = newlist; list; list = list->next) {
			struct commit *commit = list->item;
			commit->object.flags |= TMP_MARK;
		}
		for (list = newlist; list; list = list->next) {
			struct commit *commit = list->item;
			struct object *obj = &commit->object;
			struct commit_list *parent;
			if (obj->flags & UNINTERESTING)
				continue;
			for (parent = commit->parents;
			     parent;
			     parent = parent->next) {
				struct commit *pcommit = parent->item;
				if (!(pcommit->object.flags & UNINTERESTING))
					continue;
				pcommit->object.flags |= BOUNDARY;
				if (pcommit->object.flags & TMP_MARK)
					continue;
				pcommit->object.flags |= TMP_MARK;
				p = &commit_list_insert(pcommit, p)->next;
			}
		}
		for (list = newlist; list; list = list->next) {
			struct commit *commit = list->item;
			commit->object.flags &= ~TMP_MARK;
		}
	}
	revs->commits = newlist;
}

static int all_flags;
static struct rev_info *all_revs;

static int handle_one_ref(const char *path, const unsigned char *sha1)
{
	struct object *object = get_reference(all_revs, path, sha1, all_flags);
	add_pending_object(all_revs, object, "");
	return 0;
}

static void handle_all(struct rev_info *revs, unsigned flags)
{
	all_revs = revs;
	all_flags = flags;
	for_each_ref(handle_one_ref);
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
		if (it->type != TYPE_TAG)
			break;
		memcpy(sha1, ((struct tag*)it)->tagged->sha1, 20);
	}
	if (it->type != TYPE_COMMIT)
		return 0;
	commit = (struct commit *)it;
	for (parents = commit->parents; parents; parents = parents->next) {
		it = &parents->item->object;
		it->flags |= flags;
		add_pending_object(revs, it, arg);
	}
	return 1;
}

void init_revisions(struct rev_info *revs)
{
	memset(revs, 0, sizeof(*revs));

	revs->abbrev = DEFAULT_ABBREV;
	revs->ignore_merges = 1;
	revs->simplify_history = 1;
	revs->pruning.recursive = 1;
	revs->pruning.add_remove = file_add_remove;
	revs->pruning.change = file_change;
	revs->lifo = 1;
	revs->dense = 1;
	revs->prefix = setup_git_directory();
	revs->max_age = -1;
	revs->min_age = -1;
	revs->max_count = -1;

	revs->prune_fn = NULL;
	revs->prune_data = NULL;

	revs->topo_setter = topo_sort_default_setter;
	revs->topo_getter = topo_sort_default_getter;

	revs->commit_format = CMIT_FMT_DEFAULT;

	diff_setup(&revs->diffopt);
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
	int i, flags, seen_dashdash;
	const char **unrecognized = argv + 1;
	int left = 1;

	/* First, search for "--" */
	seen_dashdash = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "--"))
			continue;
		argv[i] = NULL;
		argc = i;
		revs->prune_data = get_pathspec(revs->prefix, argv + i + 1);
		seen_dashdash = 1;
		break;
	}

	flags = 0;
	for (i = 1; i < argc; i++) {
		struct object *object;
		const char *arg = argv[i];
		unsigned char sha1[20];
		char *dotdot;
		int local_flags;

		if (*arg == '-') {
			int opts;
			if (!strncmp(arg, "--max-count=", 12)) {
				revs->max_count = atoi(arg + 12);
				continue;
			}
			/* accept -<digit>, like traditional "head" */
			if ((*arg == '-') && isdigit(arg[1])) {
				revs->max_count = atoi(arg + 1);
				continue;
			}
			if (!strcmp(arg, "-n")) {
				if (argc <= i + 1)
					die("-n requires an argument");
				revs->max_count = atoi(argv[++i]);
				continue;
			}
			if (!strncmp(arg,"-n",2)) {
				revs->max_count = atoi(arg + 2);
				continue;
			}
			if (!strncmp(arg, "--max-age=", 10)) {
				revs->max_age = atoi(arg + 10);
				continue;
			}
			if (!strncmp(arg, "--since=", 8)) {
				revs->max_age = approxidate(arg + 8);
				continue;
			}
			if (!strncmp(arg, "--after=", 8)) {
				revs->max_age = approxidate(arg + 8);
				continue;
			}
			if (!strncmp(arg, "--min-age=", 10)) {
				revs->min_age = atoi(arg + 10);
				continue;
			}
			if (!strncmp(arg, "--before=", 9)) {
				revs->min_age = approxidate(arg + 9);
				continue;
			}
			if (!strncmp(arg, "--until=", 8)) {
				revs->min_age = approxidate(arg + 8);
				continue;
			}
			if (!strcmp(arg, "--all")) {
				handle_all(revs, flags);
				continue;
			}
			if (!strcmp(arg, "--not")) {
				flags ^= UNINTERESTING;
				continue;
			}
			if (!strcmp(arg, "--default")) {
				if (++i >= argc)
					die("bad --default argument");
				def = argv[i];
				continue;
			}
			if (!strcmp(arg, "--topo-order")) {
				revs->topo_order = 1;
				continue;
			}
			if (!strcmp(arg, "--date-order")) {
				revs->lifo = 0;
				revs->topo_order = 1;
				continue;
			}
			if (!strcmp(arg, "--parents")) {
				revs->parents = 1;
				continue;
			}
			if (!strcmp(arg, "--dense")) {
				revs->dense = 1;
				continue;
			}
			if (!strcmp(arg, "--sparse")) {
				revs->dense = 0;
				continue;
			}
			if (!strcmp(arg, "--remove-empty")) {
				revs->remove_empty_trees = 1;
				continue;
			}
			if (!strcmp(arg, "--no-merges")) {
				revs->no_merges = 1;
				continue;
			}
			if (!strcmp(arg, "--boundary")) {
				revs->boundary = 1;
				continue;
			}
			if (!strcmp(arg, "--objects")) {
				revs->tag_objects = 1;
				revs->tree_objects = 1;
				revs->blob_objects = 1;
				continue;
			}
			if (!strcmp(arg, "--objects-edge")) {
				revs->tag_objects = 1;
				revs->tree_objects = 1;
				revs->blob_objects = 1;
				revs->edge_hint = 1;
				continue;
			}
			if (!strcmp(arg, "--unpacked")) {
				revs->unpacked = 1;
				continue;
			}
			if (!strcmp(arg, "-r")) {
				revs->diff = 1;
				revs->diffopt.recursive = 1;
				continue;
			}
			if (!strcmp(arg, "-t")) {
				revs->diff = 1;
				revs->diffopt.recursive = 1;
				revs->diffopt.tree_in_recursive = 1;
				continue;
			}
			if (!strcmp(arg, "-m")) {
				revs->ignore_merges = 0;
				continue;
			}
			if (!strcmp(arg, "-c")) {
				revs->diff = 1;
				revs->dense_combined_merges = 0;
				revs->combine_merges = 1;
				continue;
			}
			if (!strcmp(arg, "--cc")) {
				revs->diff = 1;
				revs->dense_combined_merges = 1;
				revs->combine_merges = 1;
				continue;
			}
			if (!strcmp(arg, "-v")) {
				revs->verbose_header = 1;
				continue;
			}
			if (!strncmp(arg, "--pretty", 8)) {
				revs->verbose_header = 1;
				revs->commit_format = get_commit_format(arg+8);
				continue;
			}
			if (!strcmp(arg, "--root")) {
				revs->show_root_diff = 1;
				continue;
			}
			if (!strcmp(arg, "--no-commit-id")) {
				revs->no_commit_id = 1;
				continue;
			}
			if (!strcmp(arg, "--always")) {
				revs->always_show_header = 1;
				continue;
			}
			if (!strcmp(arg, "--no-abbrev")) {
				revs->abbrev = 0;
				continue;
			}
			if (!strcmp(arg, "--abbrev")) {
				revs->abbrev = DEFAULT_ABBREV;
				continue;
			}
			if (!strncmp(arg, "--abbrev=", 9)) {
				revs->abbrev = strtoul(arg + 9, NULL, 10);
				if (revs->abbrev < MINIMUM_ABBREV)
					revs->abbrev = MINIMUM_ABBREV;
				else if (revs->abbrev > 40)
					revs->abbrev = 40;
				continue;
			}
			if (!strcmp(arg, "--abbrev-commit")) {
				revs->abbrev_commit = 1;
				continue;
			}
			if (!strcmp(arg, "--full-diff")) {
				revs->diff = 1;
				revs->full_diff = 1;
				continue;
			}
			if (!strcmp(arg, "--full-history")) {
				revs->simplify_history = 0;
				continue;
			}
			opts = diff_opt_parse(&revs->diffopt, argv+i, argc-i);
			if (opts > 0) {
				revs->diff = 1;
				i += opts - 1;
				continue;
			}
			*unrecognized++ = arg;
			left++;
			continue;
		}
		dotdot = strstr(arg, "..");
		if (dotdot) {
			unsigned char from_sha1[20];
			const char *next = dotdot + 2;
			const char *this = arg;
			*dotdot = 0;
			if (!*next)
				next = "HEAD";
			if (dotdot == arg)
				this = "HEAD";
			if (!get_sha1(this, from_sha1) &&
			    !get_sha1(next, sha1)) {
				struct object *exclude;
				struct object *include;

				exclude = get_reference(revs, this, from_sha1, flags ^ UNINTERESTING);
				include = get_reference(revs, next, sha1, flags);
				if (!exclude || !include)
					die("Invalid revision range %s..%s", arg, next);

				if (!seen_dashdash) {
					*dotdot = '.';
					verify_non_filename(revs->prefix, arg);
				}
				add_pending_object(revs, exclude, this);
				add_pending_object(revs, include, next);
				continue;
			}
			*dotdot = '.';
		}
		dotdot = strstr(arg, "^@");
		if (dotdot && !dotdot[2]) {
			*dotdot = 0;
			if (add_parents_only(revs, arg, flags))
				continue;
			*dotdot = '^';
		}
		local_flags = 0;
		if (*arg == '^') {
			local_flags = UNINTERESTING;
			arg++;
		}
		if (get_sha1(arg, sha1)) {
			int j;

			if (seen_dashdash || local_flags)
				die("bad revision '%s'", arg);

			/* If we didn't have a "--":
			 * (1) all filenames must exist;
			 * (2) all rev-args must not be interpretable
			 *     as a valid filename.
			 * but the latter we have checked in the main loop.
			 */
			for (j = i; j < argc; j++)
				verify_filename(revs->prefix, argv[j]);

			revs->prune_data = get_pathspec(revs->prefix, argv + i);
			break;
		}
		if (!seen_dashdash)
			verify_non_filename(revs->prefix, arg);
		object = get_reference(revs, arg, sha1, flags ^ local_flags);
		add_pending_object(revs, object, arg);
	}
	if (def && !revs->pending_objects) {
		unsigned char sha1[20];
		struct object *object;
		if (get_sha1(def, sha1))
			die("bad default revision '%s'", def);
		object = get_reference(revs, def, sha1, 0);
		add_pending_object(revs, object, def);
	}

	if (revs->topo_order || revs->unpacked)
		revs->limited = 1;

	if (revs->prune_data) {
		diff_tree_setup_paths(revs->prune_data, &revs->pruning);
		revs->prune_fn = try_to_simplify_commit;
		if (!revs->full_diff)
			diff_tree_setup_paths(revs->prune_data, &revs->diffopt);
	}
	if (revs->combine_merges) {
		revs->ignore_merges = 0;
		if (revs->dense_combined_merges &&
		    (revs->diffopt.output_format != DIFF_FORMAT_DIFFSTAT))
			revs->diffopt.output_format = DIFF_FORMAT_PATCH;
	}
	revs->diffopt.abbrev = revs->abbrev;
	diff_setup_done(&revs->diffopt);

	return left;
}

void prepare_revision_walk(struct rev_info *revs)
{
	struct object_list *list;

	list = revs->pending_objects;
	revs->pending_objects = NULL;
	while (list) {
		struct commit *commit = handle_commit(revs, list->item, list->name);
		if (commit) {
			if (!(commit->object.flags & SEEN)) {
				commit->object.flags |= SEEN;
				insert_by_date(commit, &revs->commits);
			}
		}
		list = list->next;
	}

	if (revs->no_walk)
		return;
	if (revs->limited)
		limit_list(revs);
	if (revs->topo_order)
		sort_in_topological_order_fn(&revs->commits, revs->lifo,
					     revs->topo_setter,
					     revs->topo_getter);
}

static int rewrite_one(struct rev_info *revs, struct commit **pp)
{
	for (;;) {
		struct commit *p = *pp;
		if (!revs->limited)
			add_parents_to_list(revs, p, &revs->commits);
		if (p->object.flags & (TREECHANGE | UNINTERESTING))
			return 0;
		if (!p->parents)
			return -1;
		*pp = p->parents->item;
	}
}

static void rewrite_parents(struct rev_info *revs, struct commit *commit)
{
	struct commit_list **pp = &commit->parents;
	while (*pp) {
		struct commit_list *parent = *pp;
		if (rewrite_one(revs, &parent->item) < 0) {
			*pp = parent->next;
			continue;
		}
		pp = &parent->next;
	}
}

static void mark_boundary_to_show(struct commit *commit)
{
	struct commit_list *p = commit->parents;
	while (p) {
		commit = p->item;
		p = p->next;
		if (commit->object.flags & BOUNDARY)
			commit->object.flags |= BOUNDARY_SHOW;
	}
}

struct commit *get_revision(struct rev_info *revs)
{
	struct commit_list *list = revs->commits;

	if (!list)
		return NULL;

	/* Check the max_count ... */
	switch (revs->max_count) {
	case -1:
		break;
	case 0:
		return NULL;
	default:
		revs->max_count--;
	}

	do {
		struct commit *commit = revs->commits->item;

		revs->commits = revs->commits->next;

		/*
		 * If we haven't done the list limiting, we need to look at
		 * the parents here. We also need to do the date-based limiting
		 * that we'd otherwise have done in limit_list().
		 */
		if (!revs->limited) {
			if ((revs->unpacked &&
			     has_sha1_pack(commit->object.sha1)) ||
			    (revs->max_age != -1 &&
			     (commit->date < revs->max_age)))
				continue;
			add_parents_to_list(revs, commit, &revs->commits);
		}
		if (commit->object.flags & SHOWN)
			continue;

		/* We want to show boundary commits only when their
		 * children are shown.  When path-limiter is in effect,
		 * rewrite_parents() drops some commits from getting shown,
		 * and there is no point showing boundary parents that
		 * are not shown.  After rewrite_parents() rewrites the
		 * parents of a commit that is shown, we mark the boundary
		 * parents with BOUNDARY_SHOW.
		 */
		if (commit->object.flags & BOUNDARY_SHOW) {
			commit->object.flags |= SHOWN;
			return commit;
		}
		if (commit->object.flags & UNINTERESTING)
			continue;
		if (revs->min_age != -1 && (commit->date > revs->min_age))
			continue;
		if (revs->no_merges &&
		    commit->parents && commit->parents->next)
			continue;
		if (revs->prune_fn && revs->dense) {
			if (!(commit->object.flags & TREECHANGE))
				continue;
			if (revs->parents)
				rewrite_parents(revs, commit);
		}
		if (revs->boundary)
			mark_boundary_to_show(commit);
		commit->object.flags |= SHOWN;
		return commit;
	} while (revs->commits);
	return NULL;
}
