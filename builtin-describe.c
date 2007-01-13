#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"
#include "builtin.h"

#define SEEN		(1u<<0)
#define MAX_TAGS	(FLAG_BITS - 1)

static const char describe_usage[] =
"git-describe [--all] [--tags] [--abbrev=<n>] <committish>*";

static int debug;	/* Display lots of verbose info */
static int all;	/* Default to annotated tags only */
static int tags;	/* But allow any tags if --tags is specified */
static int abbrev = DEFAULT_ABBREV;
static int max_candidates = 10;

static unsigned int names[256], allocs[256];
static struct commit_name {
	struct commit *commit;
	int prio; /* annotated tag = 2, tag = 1, head = 0 */
	char path[FLEX_ARRAY]; /* more */
} **name_array[256];

static struct commit_name *match(struct commit *cmit)
{
	unsigned char level0 = cmit->object.sha1[0];
	struct commit_name **p = name_array[level0];
	unsigned int hi = names[level0];
	unsigned int lo = 0;

	while (lo < hi) {
		unsigned int mi = (lo + hi) / 2;
		int cmp = hashcmp(p[mi]->commit->object.sha1,
			cmit->object.sha1);
		if (!cmp) {
			while (mi && p[mi - 1]->commit == cmit)
				mi--;
			return p[mi];
		}
		if (cmp > 0)
			hi = mi;
		else
			lo = mi+1;
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
	unsigned char m = commit->object.sha1[0];

	name->commit = commit;
	name->prio = prio;
	memcpy(name->path, path, len);
	idx = names[m];
	if (idx >= allocs[m]) {
		allocs[m] = (idx + 50) * 3 / 2;
		name_array[m] = xrealloc(name_array[m],
			allocs[m] * sizeof(*name_array));
	}
	name_array[m][idx] = name;
	names[m] = ++idx;
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
	int cmp = hashcmp(a->commit->object.sha1, b->commit->object.sha1);

	if (cmp)
		return cmp;
	if (a->prio != b->prio)
		return b->prio - a->prio;
	return (a_date > b_date) ? -1 : (a_date == b_date) ? 0 : 1;
}

struct possible_tag {
	struct commit_name *name;
	unsigned long depth;
	unsigned flag_within;
};

static void describe(const char *arg, int last_one)
{
	unsigned char sha1[20];
	struct commit *cmit, *gave_up_on = NULL;
	struct commit_list *list;
	static int initialized = 0;
	struct commit_name *n;
	struct possible_tag all_matches[MAX_TAGS], *min_match;
	unsigned int match_cnt = 0, annotated_cnt = 0, cur_match;
	unsigned long seen_commits = 0;

	if (get_sha1(arg, sha1))
		die("Not a valid object name %s", arg);
	cmit = lookup_commit_reference(sha1);
	if (!cmit)
		die("%s is not a valid '%s' object", arg, commit_type);

	if (!initialized) {
		unsigned int m;
		initialized = 1;
		for_each_ref(get_name, NULL);
		for (m = 0; m < ARRAY_SIZE(name_array); m++)
			qsort(name_array[m], names[m],
				sizeof(*name_array[m]), compare_names);
	}

	n = match(cmit);
	if (n) {
		printf("%s\n", n->path);
		return;
	}

	if (debug)
		fprintf(stderr, "searching to describe %s\n", arg);

	list = NULL;
	cmit->object.flags = SEEN;
	commit_list_insert(cmit, &list);
	while (list) {
		struct commit *c = pop_commit(&list);
		struct commit_list *parents = c->parents;
		seen_commits++;
		n = match(c);
		if (n) {
			if (match_cnt < max_candidates) {
				struct possible_tag *t = &all_matches[match_cnt++];
				t->name = n;
				t->depth = seen_commits - 1;
				t->flag_within = 1u << match_cnt;
				c->object.flags |= t->flag_within;
				if (n->prio == 2)
					annotated_cnt++;
			}
			else {
				gave_up_on = c;
				break;
			}
		}
		for (cur_match = 0; cur_match < match_cnt; cur_match++) {
			struct possible_tag *t = &all_matches[cur_match];
			if (!(c->object.flags & t->flag_within))
				t->depth++;
		}
		if (annotated_cnt && !list) {
			if (debug)
				fprintf(stderr, "finished search at %s\n",
					sha1_to_hex(c->object.sha1));
			break;
		}
		while (parents) {
			struct commit *p = parents->item;
			parse_commit(p);
			if (!(p->object.flags & SEEN))
				insert_by_date(p, &list);
			p->object.flags |= c->object.flags;
			parents = parents->next;
		}
	}
	free_commit_list(list);

	if (!match_cnt)
		die("cannot describe '%s'", sha1_to_hex(cmit->object.sha1));

	min_match = &all_matches[0];
	for (cur_match = 1; cur_match < match_cnt; cur_match++) {
		struct possible_tag *t = &all_matches[cur_match];
		if (t->depth < min_match->depth
			&& t->name->prio >= min_match->name->prio)
			min_match = t;
	}
	if (debug) {
		for (cur_match = 0; cur_match < match_cnt; cur_match++) {
			struct possible_tag *t = &all_matches[cur_match];
			fprintf(stderr, " %c %8lu %s\n",
				min_match == t ? '*' : ' ',
				t->depth, t->name->path);
		}
		fprintf(stderr, "traversed %lu commits\n", seen_commits);
		if (gave_up_on) {
			fprintf(stderr,
				"more than %i tags found; listed %i most recent\n"
				"gave up search at %s\n",
				max_candidates, max_candidates,
				sha1_to_hex(gave_up_on->object.sha1));
		}
	}
	printf("%s-g%s\n", min_match->name->path,
		   find_unique_abbrev(cmit->object.sha1, abbrev));

	if (!last_one)
		clear_commit_marks(cmit, -1);
}

int cmd_describe(int argc, const char **argv, const char *prefix)
{
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg != '-')
			break;
		else if (!strcmp(arg, "--debug"))
			debug = 1;
		else if (!strcmp(arg, "--all"))
			all = 1;
		else if (!strcmp(arg, "--tags"))
			tags = 1;
		else if (!strncmp(arg, "--abbrev=", 9)) {
			abbrev = strtoul(arg + 9, NULL, 10);
			if (abbrev < MINIMUM_ABBREV || 40 < abbrev)
				abbrev = DEFAULT_ABBREV;
		}
		else if (!strncmp(arg, "--candidates=", 13)) {
			max_candidates = strtoul(arg + 13, NULL, 10);
			if (max_candidates < 1)
				max_candidates = 1;
			else if (max_candidates > MAX_TAGS)
				max_candidates = MAX_TAGS;
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
