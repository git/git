#include "cache.h"
#include "tag.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
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
	struct object *obj = &tree->object;
	struct tree_entry_list *entry;

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

void mark_parents_uninteresting(struct commit *commit)
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

static void add_pending_object(struct rev_info *revs, struct object *obj, const char *name)
{
	add_object(obj, &revs->pending_objects, NULL, name);
}

static struct commit *get_commit_reference(struct rev_info *revs, const char *name, const unsigned char *sha1, unsigned int flags)
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
		if (revs->tag_objects && !(object->flags & UNINTERESTING))
			add_pending_object(revs, object, tag->tag);
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
	if (object->type == blob_type) {
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

static void add_one_commit(struct commit *commit, struct rev_info *revs)
{
	if (!commit || (commit->object.flags & SEEN))
		return;
	commit->object.flags |= SEEN;
	commit_list_insert(commit, &revs->commits);
}

static int all_flags;
static struct rev_info *all_revs;

static int handle_one_ref(const char *path, const unsigned char *sha1)
{
	struct commit *commit = get_commit_reference(all_revs, path, sha1, all_flags);
	add_one_commit(commit, all_revs);
	return 0;
}

static void handle_all(struct rev_info *revs, unsigned flags)
{
	all_revs = revs;
	all_flags = flags;
	for_each_ref(handle_one_ref);
}

/*
 * Parse revision information, filling in the "rev_info" structure,
 * and removing the used arguments from the argument list.
 *
 * Returns the number of arguments left ("new argc").
 */
int setup_revisions(int argc, const char **argv, struct rev_info *revs)
{
	int i, flags, seen_dashdash;
	const char *def = NULL;
	const char **unrecognized = argv+1;
	int left = 1;

	memset(revs, 0, sizeof(*revs));
	revs->lifo = 1;
	revs->dense = 1;
	revs->prefix = setup_git_directory();
	revs->max_age = -1;
	revs->min_age = -1;
	revs->max_count = -1;

	/* First, search for "--" */
	seen_dashdash = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "--"))
			continue;
		argv[i] = NULL;
		argc = i;
		revs->paths = get_pathspec(revs->prefix, argv + i + 1);
		seen_dashdash = 1;
		break;
	}

	flags = 0;
	for (i = 1; i < argc; i++) {
		struct commit *commit;
		const char *arg = argv[i];
		unsigned char sha1[20];
		char *dotdot;
		int local_flags;

		if (*arg == '-') {
			if (!strncmp(arg, "--max-count=", 12)) {
				revs->max_count = atoi(arg + 12);
				continue;
			}
			if (!strncmp(arg, "--max-age=", 10)) {
				revs->max_age = atoi(arg + 10);
				continue;
			}
			if (!strncmp(arg, "--min-age=", 10)) {
				revs->min_age = atoi(arg + 10);
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
			*unrecognized++ = arg;
			left++;
			continue;
		}
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

				exclude = get_commit_reference(revs, arg, from_sha1, flags ^ UNINTERESTING);
				include = get_commit_reference(revs, next, sha1, flags);
				if (!exclude || !include)
					die("Invalid revision range %s..%s", arg, next);
				add_one_commit(exclude, revs);
				add_one_commit(include, revs);
				continue;
			}
			*dotdot = '.';
		}
		local_flags = 0;
		if (*arg == '^') {
			local_flags = UNINTERESTING;
			arg++;
		}
		if (get_sha1(arg, sha1) < 0) {
			struct stat st;
			int j;

			if (seen_dashdash || local_flags)
				die("bad revision '%s'", arg);

			/* If we didn't have a "--", all filenames must exist */
			for (j = i; j < argc; j++) {
				if (lstat(argv[j], &st) < 0)
					die("'%s': %s", arg, strerror(errno));
			}
			revs->paths = get_pathspec(revs->prefix, argv + i);
			break;
		}
		commit = get_commit_reference(revs, arg, sha1, flags ^ local_flags);
		add_one_commit(commit, revs);
	}
	if (def && !revs->commits) {
		unsigned char sha1[20];
		struct commit *commit;
		if (get_sha1(def, sha1) < 0)
			die("bad default revision '%s'", def);
		commit = get_commit_reference(revs, def, sha1, 0);
		add_one_commit(commit, revs);
	}
	*unrecognized = NULL;
	return left;
}
