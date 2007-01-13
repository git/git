#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "builtin.h"

static const char describe_usage[] =
"git-describe [--all] [--tags] [--abbrev=<n>] <committish>*";

static int all;	/* Default to annotated tags only */
static int tags;	/* But allow any tags if --tags is specified */

static int abbrev = DEFAULT_ABBREV;

static int names, allocs;
static struct commit_name {
	struct commit *commit;
	int prio; /* annotated tag = 2, tag = 1, head = 0 */
	char path[FLEX_ARRAY]; /* more */
} **name_array = NULL;

static struct commit_name *match(struct commit *cmit)
{
	int i = names;
	struct commit_name **p = name_array;

	while (i-- > 0) {
		struct commit_name *n = *p++;
		if (n->commit == cmit)
			return n;
	}
	return NULL;
}

static void add_to_known_names(const char *path,
			       struct commit *commit,
			       int prio)
{
	int idx;
	int len = strlen(path)+1;
	struct commit_name *name = xmalloc(sizeof(struct commit_name) + len);

	name->commit = commit;
	name->prio = prio;
	memcpy(name->path, path, len);
	idx = names;
	if (idx >= allocs) {
		allocs = (idx + 50) * 3 / 2;
		name_array = xrealloc(name_array, allocs*sizeof(*name_array));
	}
	name_array[idx] = name;
	names = ++idx;
}

static int get_name(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct commit *commit = lookup_commit_reference_gently(sha1, 1);
	struct object *object;
	int prio;

	if (!commit)
		return 0;
	object = parse_object(sha1);
	/* If --all, then any refs are used.
	 * If --tags, then any tags are used.
	 * Otherwise only annotated tags are used.
	 */
	if (!strncmp(path, "refs/tags/", 10)) {
		if (object->type == OBJ_TAG)
			prio = 2;
		else
			prio = 1;
	}
	else
		prio = 0;

	if (!all) {
		if (!prio)
			return 0;
		if (!tags && prio < 2)
			return 0;
	}
	add_to_known_names(all ? path + 5 : path + 10, commit, prio);
	return 0;
}

static int compare_names(const void *_a, const void *_b)
{
	struct commit_name *a = *(struct commit_name **)_a;
	struct commit_name *b = *(struct commit_name **)_b;
	unsigned long a_date = a->commit->date;
	unsigned long b_date = b->commit->date;

	if (a->prio != b->prio)
		return b->prio - a->prio;
	return (a_date > b_date) ? -1 : (a_date == b_date) ? 0 : 1;
}

struct possible_tag {
	struct possible_tag *next;
	struct commit_name *name;
	unsigned long depth;
};

static void describe(const char *arg, int last_one)
{
	unsigned char sha1[20];
	struct commit *cmit;
	struct commit_list *list;
	static int initialized = 0;
	struct commit_name *n;
	struct possible_tag *all_matches, *min_match, *cur_match;

	if (get_sha1(arg, sha1))
		die("Not a valid object name %s", arg);
	cmit = lookup_commit_reference(sha1);
	if (!cmit)
		die("%s is not a valid '%s' object", arg, commit_type);

	if (!initialized) {
		initialized = 1;
		for_each_ref(get_name, NULL);
		qsort(name_array, names, sizeof(*name_array), compare_names);
	}

	n = match(cmit);
	if (n) {
		printf("%s\n", n->path);
		return;
	}

	list = NULL;
	all_matches = NULL;
	cur_match = NULL;
	commit_list_insert(cmit, &list);
	while (list) {
		struct commit *c = pop_commit(&list);
		struct commit_list *parents = c->parents;
		n = match(c);
		if (n) {
			struct possible_tag *p = xmalloc(sizeof(*p));
			p->name = n;
			p->next = NULL;
			if (cur_match)
				cur_match->next = p;
			else
				all_matches = p;
			cur_match = p;
			if (n->prio == 2)
				continue;
		}
		while (parents) {
			struct commit *p = parents->item;
			parse_commit(p);
			if (!(p->object.flags & SEEN)) {
				p->object.flags |= SEEN;
				insert_by_date(p, &list);
			}
			parents = parents->next;
		}
	}

	if (!all_matches)
		die("cannot describe '%s'", sha1_to_hex(cmit->object.sha1));

	min_match = NULL;
	for (cur_match = all_matches; cur_match; cur_match = cur_match->next) {
		struct rev_info revs;
		struct commit *tagged = cur_match->name->commit;

		clear_commit_marks(cmit, -1);
		init_revisions(&revs, NULL);
		tagged->object.flags |= UNINTERESTING;
		add_pending_object(&revs, &tagged->object, NULL);
		add_pending_object(&revs, &cmit->object, NULL);

		prepare_revision_walk(&revs);
		cur_match->depth = 0;
		while ((!min_match || cur_match->depth < min_match->depth)
			&& get_revision(&revs))
			cur_match->depth++;
		if (!min_match || (cur_match->depth < min_match->depth
			&& cur_match->name->prio >= min_match->name->prio))
			min_match = cur_match;
		free_commit_list(revs.commits);
	}
	printf("%s-g%s\n", min_match->name->path,
		   find_unique_abbrev(cmit->object.sha1, abbrev));

	if (!last_one) {
		for (cur_match = all_matches; cur_match; cur_match = min_match) {
			min_match = cur_match->next;
			free(cur_match);
		}
		clear_commit_marks(cmit, SEEN);
	}
}

int cmd_describe(int argc, const char **argv, const char *prefix)
{
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg != '-')
			break;
		else if (!strcmp(arg, "--all"))
			all = 1;
		else if (!strcmp(arg, "--tags"))
			tags = 1;
		else if (!strncmp(arg, "--abbrev=", 9)) {
			abbrev = strtoul(arg + 9, NULL, 10);
			if (abbrev < MINIMUM_ABBREV || 40 < abbrev)
				abbrev = DEFAULT_ABBREV;
		}
		else
			usage(describe_usage);
	}

	save_commit_buffer = 0;

	if (argc <= i)
		describe("HEAD", 1);
	else
		while (i < argc) {
			describe(argv[i], (i == argc - 1));
			i++;
		}

	return 0;
}
