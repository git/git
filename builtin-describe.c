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

struct commit_name {
	int prio; /* annotated tag = 2, tag = 1, head = 0 */
	char path[FLEX_ARRAY]; /* more */
};
static const char *prio_names[] = {
	"head", "lightweight", "annotated",
};

static void add_to_known_names(const char *path,
			       struct commit *commit,
			       int prio)
{
	struct commit_name *e = commit->util;
	if (!e || e->prio < prio) {
		size_t len = strlen(path)+1;
		free(e);
		e = xmalloc(sizeof(struct commit_name) + len);
		e->prio = prio;
		memcpy(e->path, path, len);
		commit->util = e;
	}
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

struct possible_tag {
	struct commit_name *name;
	int depth;
	int found_order;
	unsigned flag_within;
};

static int compare_pt(const void *a_, const void *b_)
{
	struct possible_tag *a = (struct possible_tag *)a_;
	struct possible_tag *b = (struct possible_tag *)b_;
	if (a->name->prio != b->name->prio)
		return b->name->prio - a->name->prio;
	if (a->depth != b->depth)
		return a->depth - b->depth;
	if (a->found_order != b->found_order)
		return a->found_order - b->found_order;
	return 0;
}

static void describe(const char *arg, int last_one)
{
	unsigned char sha1[20];
	struct commit *cmit, *gave_up_on = NULL;
	struct commit_list *list;
	static int initialized = 0;
	struct commit_name *n;
	struct possible_tag all_matches[MAX_TAGS];
	unsigned int match_cnt = 0, annotated_cnt = 0, cur_match;
	unsigned long seen_commits = 0;

	if (get_sha1(arg, sha1))
		die("Not a valid object name %s", arg);
	cmit = lookup_commit_reference(sha1);
	if (!cmit)
		die("%s is not a valid '%s' object", arg, commit_type);

	if (!initialized) {
		initialized = 1;
		for_each_ref(get_name, NULL);
	}

	n = cmit->util;
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
		n = c->util;
		if (n) {
			if (match_cnt < max_candidates) {
				struct possible_tag *t = &all_matches[match_cnt++];
				t->name = n;
				t->depth = seen_commits - 1;
				t->flag_within = 1u << match_cnt;
				t->found_order = match_cnt;
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

	qsort(all_matches, match_cnt, sizeof(all_matches[0]), compare_pt);
	if (debug) {
		for (cur_match = 0; cur_match < match_cnt; cur_match++) {
			struct possible_tag *t = &all_matches[cur_match];
			fprintf(stderr, " %-11s %8d %s\n",
				prio_names[t->name->prio],
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
	printf("%s-g%s\n", all_matches[0].name->path,
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
