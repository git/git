#include "cache.h"
#include "refs.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "epoch.h"
#include "diff.h"

#define SEEN		(1u << 0)
#define INTERESTING	(1u << 1)
#define COUNTED		(1u << 2)
#define SHOWN		(1u << 3)
#define TREECHANGE	(1u << 4)

static const char rev_list_usage[] =
"git-rev-list [OPTION] <commit-id>... [ -- paths... ]\n"
"  limiting output:\n"
"    --max-count=nr\n"
"    --max-age=epoch\n"
"    --min-age=epoch\n"
"    --sparse\n"
"    --no-merges\n"
"    --all\n"
"  ordering output:\n"
"    --merge-order [ --show-breaks ]\n"
"    --topo-order\n"
"  formatting output:\n"
"    --parents\n"
"    --objects\n"
"    --unpacked\n"
"    --header | --pretty\n"
"  special purpose:\n"
"    --bisect"
;

static int dense = 1;
static int unpacked = 0;
static int bisect_list = 0;
static int tag_objects = 0;
static int tree_objects = 0;
static int blob_objects = 0;
static int verbose_header = 0;
static int show_parents = 0;
static int hdr_termination = 0;
static const char *commit_prefix = "";
static unsigned long max_age = -1;
static unsigned long min_age = -1;
static int max_count = -1;
static enum cmit_fmt commit_format = CMIT_FMT_RAW;
static int merge_order = 0;
static int show_breaks = 0;
static int stop_traversal = 0;
static int topo_order = 0;
static int no_merges = 0;
static const char **paths = NULL;

static void show_commit(struct commit *commit)
{
	commit->object.flags |= SHOWN;
	if (show_breaks) {
		commit_prefix = "| ";
		if (commit->object.flags & DISCONTINUITY) {
			commit_prefix = "^ ";     
		} else if (commit->object.flags & BOUNDARY) {
			commit_prefix = "= ";
		} 
        }        		
	printf("%s%s", commit_prefix, sha1_to_hex(commit->object.sha1));
	if (show_parents) {
		struct commit_list *parents = commit->parents;
		while (parents) {
			printf(" %s", sha1_to_hex(parents->item->object.sha1));
			parents = parents->next;
		}
	}
	if (commit_format == CMIT_FMT_ONELINE)
		putchar(' ');
	else
		putchar('\n');

	if (verbose_header) {
		static char pretty_header[16384];
		pretty_print_commit(commit_format, commit->buffer, ~0, pretty_header, sizeof(pretty_header));
		printf("%s%c", pretty_header, hdr_termination);
	}
	fflush(stdout);
}

static int rewrite_one(struct commit **pp)
{
	for (;;) {
		struct commit *p = *pp;
		if (p->object.flags & (TREECHANGE | UNINTERESTING))
			return 0;
		if (!p->parents)
			return -1;
		*pp = p->parents->item;
	}
}

static void rewrite_parents(struct commit *commit)
{
	struct commit_list **pp = &commit->parents;
	while (*pp) {
		struct commit_list *parent = *pp;
		if (rewrite_one(&parent->item) < 0) {
			*pp = parent->next;
			continue;
		}
		pp = &parent->next;
	}
}

static int filter_commit(struct commit * commit)
{
	if (stop_traversal && (commit->object.flags & BOUNDARY))
		return STOP;
	if (commit->object.flags & (UNINTERESTING|SHOWN))
		return CONTINUE;
	if (min_age != -1 && (commit->date > min_age))
		return CONTINUE;
	if (max_age != -1 && (commit->date < max_age)) {
		stop_traversal=1;
		return CONTINUE;
	}
	if (no_merges && (commit->parents && commit->parents->next))
		return CONTINUE;
	if (paths && dense) {
		if (!(commit->object.flags & TREECHANGE))
			return CONTINUE;
		rewrite_parents(commit);
	}
	return DO;
}

static int process_commit(struct commit * commit)
{
	int action=filter_commit(commit);

	if (action == STOP) {
		return STOP;
	}

	if (action == CONTINUE) {
		return CONTINUE;
	}

	if (max_count != -1 && !max_count--)
		return STOP;

	show_commit(commit);

	return CONTINUE;
}

static struct object_list **add_object(struct object *obj, struct object_list **p, const char *name)
{
	struct object_list *entry = xmalloc(sizeof(*entry));
	entry->item = obj;
	entry->next = *p;
	entry->name = name;
	*p = entry;
	return &entry->next;
}

static struct object_list **process_blob(struct blob *blob, struct object_list **p, const char *name)
{
	struct object *obj = &blob->object;

	if (!blob_objects)
		return p;
	if (obj->flags & (UNINTERESTING | SEEN))
		return p;
	obj->flags |= SEEN;
	return add_object(obj, p, name);
}

static struct object_list **process_tree(struct tree *tree, struct object_list **p, const char *name)
{
	struct object *obj = &tree->object;
	struct tree_entry_list *entry;

	if (!tree_objects)
		return p;
	if (obj->flags & (UNINTERESTING | SEEN))
		return p;
	if (parse_tree(tree) < 0)
		die("bad tree object %s", sha1_to_hex(obj->sha1));
	obj->flags |= SEEN;
	p = add_object(obj, p, name);
	entry = tree->entries;
	tree->entries = NULL;
	while (entry) {
		struct tree_entry_list *next = entry->next;
		if (entry->directory)
			p = process_tree(entry->item.tree, p, entry->name);
		else
			p = process_blob(entry->item.blob, p, entry->name);
		free(entry);
		entry = next;
	}
	return p;
}

static struct object_list *pending_objects = NULL;

static void show_commit_list(struct commit_list *list)
{
	struct object_list *objects = NULL, **p = &objects, *pending;
	while (list) {
		struct commit *commit = pop_most_recent_commit(&list, SEEN);

		p = process_tree(commit->tree, p, "");
		if (process_commit(commit) == STOP)
			break;
	}
	for (pending = pending_objects; pending; pending = pending->next) {
		struct object *obj = pending->item;
		const char *name = pending->name;
		if (obj->flags & (UNINTERESTING | SEEN))
			continue;
		if (obj->type == tag_type) {
			obj->flags |= SEEN;
			p = add_object(obj, p, name);
			continue;
		}
		if (obj->type == tree_type) {
			p = process_tree((struct tree *)obj, p, name);
			continue;
		}
		if (obj->type == blob_type) {
			p = process_blob((struct blob *)obj, p, name);
			continue;
		}
		die("unknown pending object %s (%s)", sha1_to_hex(obj->sha1), name);
	}
	while (objects) {
		/* An object with name "foo\n0000000000000000000000000000000000000000"
		 * can be used confuse downstream git-pack-objects very badly.
		 */
		const char *ep = strchr(objects->name, '\n');
		if (ep) {
			printf("%s %.*s\n", sha1_to_hex(objects->item->sha1),
			       (int) (ep - objects->name),
			       objects->name);
		}
		else
			printf("%s %s\n", sha1_to_hex(objects->item->sha1), objects->name);
		objects = objects->next;
	}
}

static void mark_blob_uninteresting(struct blob *blob)
{
	if (!blob_objects)
		return;
	if (blob->object.flags & UNINTERESTING)
		return;
	blob->object.flags |= UNINTERESTING;
}

static void mark_tree_uninteresting(struct tree *tree)
{
	struct object *obj = &tree->object;
	struct tree_entry_list *entry;

	if (!tree_objects)
		return;
	if (obj->flags & UNINTERESTING)
		return;
	obj->flags |= UNINTERESTING;
	if (!has_sha1_file(obj->sha1))
		return;
	if (parse_tree(tree) < 0)
		die("bad tree %s", sha1_to_hex(obj->sha1));
	entry = tree->entries;
	tree->entries = NULL;
	while (entry) {
		struct tree_entry_list *next = entry->next;
		if (entry->directory)
			mark_tree_uninteresting(entry->item.tree);
		else
			mark_blob_uninteresting(entry->item.blob);
		free(entry);
		entry = next;
	}
}

static void mark_parents_uninteresting(struct commit *commit)
{
	struct commit_list *parents = commit->parents;

	while (parents) {
		struct commit *commit = parents->item;
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
 * This is a truly stupid algorithm, but it's only
 * used for bisection, and we just don't care enough.
 *
 * We care just barely enough to avoid recursing for
 * non-merge entries.
 */
static int count_distance(struct commit_list *entry)
{
	int nr = 0;

	while (entry) {
		struct commit *commit = entry->item;
		struct commit_list *p;

		if (commit->object.flags & (UNINTERESTING | COUNTED))
			break;
		if (!paths || (commit->object.flags & TREECHANGE))
			nr++;
		commit->object.flags |= COUNTED;
		p = commit->parents;
		entry = p;
		if (p) {
			p = p->next;
			while (p) {
				nr += count_distance(p);
				p = p->next;
			}
		}
	}

	return nr;
}

static void clear_distance(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		commit->object.flags &= ~COUNTED;
		list = list->next;
	}
}

static struct commit_list *find_bisection(struct commit_list *list)
{
	int nr, closest;
	struct commit_list *p, *best;

	nr = 0;
	p = list;
	while (p) {
		if (!paths || (p->item->object.flags & TREECHANGE))
			nr++;
		p = p->next;
	}
	closest = 0;
	best = list;

	for (p = list; p; p = p->next) {
		int distance;

		if (paths && !(p->item->object.flags & TREECHANGE))
			continue;

		distance = count_distance(p);
		clear_distance(list);
		if (nr - distance < distance)
			distance = nr - distance;
		if (distance > closest) {
			best = p;
			closest = distance;
		}
	}
	if (best)
		best->next = NULL;
	return best;
}

static void mark_edges_uninteresting(struct commit_list *list)
{
	for ( ; list; list = list->next) {
		struct commit_list *parents = list->item->parents;

		for ( ; parents; parents = parents->next) {
			struct commit *commit = parents->item;
			if (commit->object.flags & UNINTERESTING)
				mark_tree_uninteresting(commit->tree);
		}
	}
}

static int is_different = 0;

static void file_add_remove(struct diff_options *options,
		    int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *base, const char *path)
{
	is_different = 1;
}

static void file_change(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *base, const char *path)
{
	is_different = 1;
}

static struct diff_options diff_opt = {
	.recursive = 1,
	.add_remove = file_add_remove,
	.change = file_change,
};

static int same_tree(struct tree *t1, struct tree *t2)
{
	is_different = 0;
	if (diff_tree_sha1(t1->object.sha1, t2->object.sha1, "", &diff_opt) < 0)
		return 0;
	return !is_different;
}

static int same_tree_as_empty(struct tree *t1)
{
	int retval;
	void *tree;
	struct tree_desc empty, real;

	if (!t1)
		return 0;

	tree = read_object_with_reference(t1->object.sha1, "tree", &real.size, NULL);
	if (!tree)
		return 0;
	real.buf = tree;

	empty.buf = "";
	empty.size = 0;

	is_different = 0;
	retval = diff_tree(&empty, &real, "", &diff_opt);
	free(tree);

	return retval >= 0 && !is_different;
}

static struct commit *try_to_simplify_merge(struct commit *commit, struct commit_list *parent)
{
	if (!commit->tree)
		return NULL;

	while (parent) {
		struct commit *p = parent->item;
		parent = parent->next;
		parse_commit(p);
		if (!p->tree)
			continue;
		if (same_tree(commit->tree, p->tree))
			return p;
	}
	return NULL;
}

static void add_parents_to_list(struct commit *commit, struct commit_list **list)
{
	struct commit_list *parent = commit->parents;

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
	 * Ok, the commit wasn't uninteresting. If it
	 * is a merge, try to find the parent that has
	 * no differences in the path set if one exists.
	 */
	if (paths && parent && parent->next) {
		struct commit *preferred;

		preferred = try_to_simplify_merge(commit, parent);
		if (preferred) {
			parent->item = preferred;
			parent->next = NULL;
		}
	}

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

static void compress_list(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		struct commit_list *parent = commit->parents;
		list = list->next;

		if (!parent) {
			if (!same_tree_as_empty(commit->tree))
				commit->object.flags |= TREECHANGE;
			continue;
		}

		/*
		 * Exactly one parent? Check if it leaves the tree
		 * unchanged
		 */
		if (!parent->next) {
			struct tree *t1 = commit->tree;
			struct tree *t2 = parent->item->tree;
			if (!t1 || !t2 || same_tree(t1, t2))
				continue;
		}
		commit->object.flags |= TREECHANGE;
	}
}

static struct commit_list *limit_list(struct commit_list *list)
{
	struct commit_list *newlist = NULL;
	struct commit_list **p = &newlist;
	while (list) {
		struct commit_list *entry = list;
		struct commit *commit = list->item;
		struct object *obj = &commit->object;

		list = list->next;
		free(entry);

		if (max_age != -1 && (commit->date < max_age))
			obj->flags |= UNINTERESTING;
		if (unpacked && has_sha1_pack(obj->sha1))
			obj->flags |= UNINTERESTING;
		add_parents_to_list(commit, &list);
		if (obj->flags & UNINTERESTING) {
			mark_parents_uninteresting(commit);
			if (everybody_uninteresting(list))
				break;
			continue;
		}
		if (min_age != -1 && (commit->date > min_age))
			continue;
		p = &commit_list_insert(commit, p)->next;
	}
	if (tree_objects)
		mark_edges_uninteresting(newlist);
	if (paths && dense)
		compress_list(newlist);
	if (bisect_list)
		newlist = find_bisection(newlist);
	return newlist;
}

static void add_pending_object(struct object *obj, const char *name)
{
	add_object(obj, &pending_objects, name);
}

static struct commit *get_commit_reference(const char *name, const unsigned char *sha1, unsigned int flags)
{
	struct object *object;

	object = parse_object(sha1);
	if (!object)
		die("bad object %s", name);

	/*
	 * Tag object? Look what it points to..
	 */
	while (object->type == tag_type) {
		struct tag *tag = (struct tag *) object;
		object->flags |= flags;
		if (tag_objects && !(object->flags & UNINTERESTING))
			add_pending_object(object, tag->tag);
		object = parse_object(tag->tagged->sha1);
		if (!object)
			die("bad object %s", sha1_to_hex(tag->tagged->sha1));
	}

	/*
	 * Commit object? Just return it, we'll do all the complex
	 * reachability crud.
	 */
	if (object->type == commit_type) {
		struct commit *commit = (struct commit *)object;
		object->flags |= flags;
		if (parse_commit(commit) < 0)
			die("unable to parse commit %s", name);
		if (flags & UNINTERESTING)
			mark_parents_uninteresting(commit);
		return commit;
	}

	/*
	 * Tree object? Either mark it uniniteresting, or add it
	 * to the list of objects to look at later..
	 */
	if (object->type == tree_type) {
		struct tree *tree = (struct tree *)object;
		if (!tree_objects)
			return NULL;
		if (flags & UNINTERESTING) {
			mark_tree_uninteresting(tree);
			return NULL;
		}
		add_pending_object(object, "");
		return NULL;
	}

	/*
	 * Blob object? You know the drill by now..
	 */
	if (object->type == blob_type) {
		struct blob *blob = (struct blob *)object;
		if (!blob_objects)
			return NULL;
		if (flags & UNINTERESTING) {
			mark_blob_uninteresting(blob);
			return NULL;
		}
		add_pending_object(object, "");
		return NULL;
	}
	die("%s is unknown object", name);
}

static void handle_one_commit(struct commit *com, struct commit_list **lst)
{
	if (!com || com->object.flags & SEEN)
		return;
	com->object.flags |= SEEN;
	commit_list_insert(com, lst);
}

/* for_each_ref() callback does not allow user data -- Yuck. */
static struct commit_list **global_lst;

static int include_one_commit(const char *path, const unsigned char *sha1)
{
	struct commit *com = get_commit_reference(path, sha1, 0);
	handle_one_commit(com, global_lst);
	return 0;
}

static void handle_all(struct commit_list **lst)
{
	global_lst = lst;
	for_each_ref(include_one_commit);
	global_lst = NULL;
}

int main(int argc, const char **argv)
{
	const char *prefix = setup_git_directory();
	struct commit_list *list = NULL;
	int i, limited = 0;

	for (i = 1 ; i < argc; i++) {
		int flags;
		const char *arg = argv[i];
		char *dotdot;
		struct commit *commit;
		unsigned char sha1[20];

		if (!strncmp(arg, "--max-count=", 12)) {
			max_count = atoi(arg + 12);
			continue;
		}
		if (!strncmp(arg, "--max-age=", 10)) {
			max_age = atoi(arg + 10);
			limited = 1;
			continue;
		}
		if (!strncmp(arg, "--min-age=", 10)) {
			min_age = atoi(arg + 10);
			limited = 1;
			continue;
		}
		if (!strcmp(arg, "--header")) {
			verbose_header = 1;
			continue;
		}
		if (!strncmp(arg, "--pretty", 8)) {
			commit_format = get_commit_format(arg+8);
			verbose_header = 1;
			hdr_termination = '\n';
			if (commit_format == CMIT_FMT_ONELINE)
				commit_prefix = "";
			else
				commit_prefix = "commit ";
			continue;
		}
		if (!strncmp(arg, "--no-merges", 11)) {
			no_merges = 1;
			continue;
		}
		if (!strcmp(arg, "--parents")) {
			show_parents = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect")) {
			bisect_list = 1;
			continue;
		}
		if (!strcmp(arg, "--all")) {
			handle_all(&list);
			continue;
		}
		if (!strcmp(arg, "--objects")) {
			tag_objects = 1;
			tree_objects = 1;
			blob_objects = 1;
			continue;
		}
		if (!strcmp(arg, "--unpacked")) {
			unpacked = 1;
			limited = 1;
			continue;
		}
		if (!strcmp(arg, "--merge-order")) {
		        merge_order = 1;
			continue;
		}
		if (!strcmp(arg, "--show-breaks")) {
			show_breaks = 1;
			continue;
		}
		if (!strcmp(arg, "--topo-order")) {
		        topo_order = 1;
		        limited = 1;
			continue;
		}
		if (!strcmp(arg, "--dense")) {
			dense = 1;
			continue;
		}
		if (!strcmp(arg, "--sparse")) {
			dense = 0;
			continue;
		}
		if (!strcmp(arg, "--")) {
			i++;
			break;
		}

		if (show_breaks && !merge_order)
			usage(rev_list_usage);

		flags = 0;
		dotdot = strstr(arg, "..");
		if (dotdot) {
			unsigned char from_sha1[20];
			char *next = dotdot + 2;
			*dotdot = 0;
			if (!*next)
				next = "HEAD";
			if (!get_sha1(arg, from_sha1) && !get_sha1(next, sha1)) {
				struct commit *exclude;
				struct commit *include;
				
				exclude = get_commit_reference(arg, from_sha1, UNINTERESTING);
				include = get_commit_reference(next, sha1, 0);
				if (!exclude || !include)
					die("Invalid revision range %s..%s", arg, next);
				limited = 1;
				handle_one_commit(exclude, &list);
				handle_one_commit(include, &list);
				continue;
			}
			*dotdot = '.';
		}
		if (*arg == '^') {
			flags = UNINTERESTING;
			arg++;
			limited = 1;
		}
		if (get_sha1(arg, sha1) < 0)
			break;
		commit = get_commit_reference(arg, sha1, flags);
		handle_one_commit(commit, &list);
	}

	if (!list &&
	    (!(tag_objects||tree_objects||blob_objects) && !pending_objects))
		usage(rev_list_usage);

	paths = get_pathspec(prefix, argv + i);
	if (paths) {
		limited = 1;
		diff_tree_setup_paths(paths);
	}

	save_commit_buffer = verbose_header;
	track_object_refs = 0;

	if (!merge_order) {		
		sort_by_date(&list);
		if (list && !limited && max_count == 1 &&
		    !tag_objects && !tree_objects && !blob_objects) {
			show_commit(list->item);
			return 0;
		}
	        if (limited)
			list = limit_list(list);
		if (topo_order)
			sort_in_topological_order(&list);
		show_commit_list(list);
	} else {
#ifndef NO_OPENSSL
		if (sort_list_in_merge_order(list, &process_commit)) {
			die("merge order sort failed\n");
		}
#else
		die("merge order sort unsupported, OpenSSL not linked");
#endif
	}

	return 0;
}
