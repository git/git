#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "refs.h"
#include "builtin.h"
#include "exec_cmd.h"
#include "parse-options.h"
#include "diff.h"

#define SEEN		(1u<<0)
#define MAX_TAGS	(FLAG_BITS - 1)

static const char * const describe_usage[] = {
	"git describe [options] <committish>*",
	"git describe [options] --dirty",
	NULL
};

static int debug;	/* Display lots of verbose info */
static int all;	/* Any valid ref can be used */
static int tags;	/* Allow lightweight tags */
static int longformat;
static int abbrev = DEFAULT_ABBREV;
static int max_candidates = 10;
static int found_names;
static const char *pattern;
static int always;
static const char *dirty;

/* diff-index command arguments to check if working tree is dirty. */
static const char *diff_index_args[] = {
	"diff-index", "--quiet", "HEAD", "--", NULL
};


struct commit_name {
	struct tag *tag;
	unsigned prio:2; /* annotated tag = 2, tag = 1, head = 0 */
	unsigned name_checked:1;
	unsigned char sha1[20];
	char path[FLEX_ARRAY]; /* more */
};
static const char *prio_names[] = {
	"head", "lightweight", "annotated",
};

static int replace_name(struct commit_name *e,
			       int prio,
			       const unsigned char *sha1,
			       struct tag **tag)
{
	if (!e || e->prio < prio)
		return 1;

	if (e->prio == 2 && prio == 2) {
		/* Multiple annotated tags point to the same commit.
		 * Select one to keep based upon their tagger date.
		 */
		struct tag *t;

		if (!e->tag) {
			t = lookup_tag(e->sha1);
			if (!t || parse_tag(t))
				return 1;
			e->tag = t;
		}

		t = lookup_tag(sha1);
		if (!t || parse_tag(t))
			return 0;
		*tag = t;

		if (e->tag->date < t->date)
			return 1;
	}

	return 0;
}

static void add_to_known_names(const char *path,
			       struct commit *commit,
			       int prio,
			       const unsigned char *sha1)
{
	struct commit_name *e = commit->util;
	struct tag *tag = NULL;
	if (replace_name(e, prio, sha1, &tag)) {
		size_t len = strlen(path)+1;
		free(e);
		e = xmalloc(sizeof(struct commit_name) + len);
		e->tag = tag;
		e->prio = prio;
		e->name_checked = 0;
		hashcpy(e->sha1, sha1);
		memcpy(e->path, path, len);
		commit->util = e;
	}
	found_names = 1;
}

static int get_name(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	int might_be_tag = !prefixcmp(path, "refs/tags/");
	struct commit *commit;
	struct object *object;
	unsigned char peeled[20];
	int is_tag, prio;

	if (!all && !might_be_tag)
		return 0;

	if (!peel_ref(path, peeled) && !is_null_sha1(peeled)) {
		commit = lookup_commit_reference_gently(peeled, 1);
		if (!commit)
			return 0;
		is_tag = !!hashcmp(sha1, commit->object.sha1);
	} else {
		commit = lookup_commit_reference_gently(sha1, 1);
		object = parse_object(sha1);
		if (!commit || !object)
			return 0;
		is_tag = object->type == OBJ_TAG;
	}

	/* If --all, then any refs are used.
	 * If --tags, then any tags are used.
	 * Otherwise only annotated tags are used.
	 */
	if (might_be_tag) {
		if (is_tag)
			prio = 2;
		else
			prio = 1;

		if (pattern && fnmatch(pattern, path + 10, 0))
			prio = 0;
	}
	else
		prio = 0;

	if (!all) {
		if (!prio)
			return 0;
	}
	add_to_known_names(all ? path + 5 : path + 10, commit, prio, sha1);
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
	if (a->depth != b->depth)
		return a->depth - b->depth;
	if (a->found_order != b->found_order)
		return a->found_order - b->found_order;
	return 0;
}

static unsigned long finish_depth_computation(
	struct commit_list **list,
	struct possible_tag *best)
{
	unsigned long seen_commits = 0;
	while (*list) {
		struct commit *c = pop_commit(list);
		struct commit_list *parents = c->parents;
		seen_commits++;
		if (c->object.flags & best->flag_within) {
			struct commit_list *a = *list;
			while (a) {
				struct commit *i = a->item;
				if (!(i->object.flags & best->flag_within))
					break;
				a = a->next;
			}
			if (!a)
				break;
		} else
			best->depth++;
		while (parents) {
			struct commit *p = parents->item;
			parse_commit(p);
			if (!(p->object.flags & SEEN))
				insert_by_date(p, list);
			p->object.flags |= c->object.flags;
			parents = parents->next;
		}
	}
	return seen_commits;
}

static void display_name(struct commit_name *n)
{
	if (n->prio == 2 && !n->tag) {
		n->tag = lookup_tag(n->sha1);
		if (!n->tag || parse_tag(n->tag))
			die("annotated tag %s not available", n->path);
	}
	if (n->tag && !n->name_checked) {
		if (!n->tag->tag)
			die("annotated tag %s has no embedded name", n->path);
		if (strcmp(n->tag->tag, all ? n->path + 5 : n->path))
			warning("tag '%s' is really '%s' here", n->tag->tag, n->path);
		n->name_checked = 1;
	}

	if (n->tag)
		printf("%s", n->tag->tag);
	else
		printf("%s", n->path);
}

static void show_suffix(int depth, const unsigned char *sha1)
{
	printf("-%d-g%s", depth, find_unique_abbrev(sha1, abbrev));
}

static void describe(const char *arg, int last_one)
{
	unsigned char sha1[20];
	struct commit *cmit, *gave_up_on = NULL;
	struct commit_list *list;
	struct commit_name *n;
	struct possible_tag all_matches[MAX_TAGS];
	unsigned int match_cnt = 0, annotated_cnt = 0, cur_match;
	unsigned long seen_commits = 0;
	unsigned int unannotated_cnt = 0;

	if (get_sha1(arg, sha1))
		die("Not a valid object name %s", arg);
	cmit = lookup_commit_reference(sha1);
	if (!cmit)
		die("%s is not a valid '%s' object", arg, commit_type);

	n = cmit->util;
	if (n && (tags || all || n->prio == 2)) {
		/*
		 * Exact match to an existing ref.
		 */
		display_name(n);
		if (longformat)
			show_suffix(0, n->tag ? n->tag->tagged->sha1 : sha1);
		if (dirty)
			printf("%s", dirty);
		printf("\n");
		return;
	}

	if (!max_candidates)
		die("no tag exactly matches '%s'", sha1_to_hex(cmit->object.sha1));
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
			if (!tags && !all && n->prio < 2) {
				unannotated_cnt++;
			} else if (match_cnt < max_candidates) {
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

	if (!match_cnt) {
		const unsigned char *sha1 = cmit->object.sha1;
		if (always) {
			printf("%s", find_unique_abbrev(sha1, abbrev));
			if (dirty)
				printf("%s", dirty);
			printf("\n");
			return;
		}
		if (unannotated_cnt)
			die("No annotated tags can describe '%s'.\n"
			    "However, there were unannotated tags: try --tags.",
			    sha1_to_hex(sha1));
		else
			die("No tags can describe '%s'.\n"
			    "Try --always, or create some tags.",
			    sha1_to_hex(sha1));
	}

	qsort(all_matches, match_cnt, sizeof(all_matches[0]), compare_pt);

	if (gave_up_on) {
		insert_by_date(gave_up_on, &list);
		seen_commits--;
	}
	seen_commits += finish_depth_computation(&list, &all_matches[0]);
	free_commit_list(list);

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

	display_name(all_matches[0].name);
	if (abbrev)
		show_suffix(all_matches[0].depth, cmit->object.sha1);
	if (dirty)
		printf("%s", dirty);
	printf("\n");

	if (!last_one)
		clear_commit_marks(cmit, -1);
}

int cmd_describe(int argc, const char **argv, const char *prefix)
{
	int contains = 0;
	struct option options[] = {
		OPT_BOOLEAN(0, "contains",   &contains, "find the tag that comes after the commit"),
		OPT_BOOLEAN(0, "debug",      &debug, "debug search strategy on stderr"),
		OPT_BOOLEAN(0, "all",        &all, "use any ref in .git/refs"),
		OPT_BOOLEAN(0, "tags",       &tags, "use any tag in .git/refs/tags"),
		OPT_BOOLEAN(0, "long",       &longformat, "always use long format"),
		OPT__ABBREV(&abbrev),
		OPT_SET_INT(0, "exact-match", &max_candidates,
			    "only output exact matches", 0),
		OPT_INTEGER(0, "candidates", &max_candidates,
			    "consider <n> most recent tags (default: 10)"),
		OPT_STRING(0, "match",       &pattern, "pattern",
			   "only consider tags matching <pattern>"),
		OPT_BOOLEAN(0, "always",     &always,
			   "show abbreviated commit object as fallback"),
		{OPTION_STRING, 0, "dirty",  &dirty, "mark",
			   "append <mark> on dirty working tree (default: \"-dirty\")",
		 PARSE_OPT_OPTARG, NULL, (intptr_t) "-dirty"},
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, describe_usage, 0);
	if (max_candidates < 0)
		max_candidates = 0;
	else if (max_candidates > MAX_TAGS)
		max_candidates = MAX_TAGS;

	save_commit_buffer = 0;

	if (longformat && abbrev == 0)
		die("--long is incompatible with --abbrev=0");

	if (contains) {
		const char **args = xmalloc((7 + argc) * sizeof(char *));
		int i = 0;
		args[i++] = "name-rev";
		args[i++] = "--name-only";
		args[i++] = "--no-undefined";
		if (always)
			args[i++] = "--always";
		if (!all) {
			args[i++] = "--tags";
			if (pattern) {
				char *s = xmalloc(strlen("--refs=refs/tags/") + strlen(pattern) + 1);
				sprintf(s, "--refs=refs/tags/%s", pattern);
				args[i++] = s;
			}
		}
		memcpy(args + i, argv, argc * sizeof(char *));
		args[i + argc] = NULL;
		return cmd_name_rev(i + argc, args, prefix);
	}

	for_each_ref(get_name, NULL);
	if (!found_names && !always)
		die("No names found, cannot describe anything.");

	if (argc == 0) {
		if (dirty && !cmd_diff_index(ARRAY_SIZE(diff_index_args) - 1, diff_index_args, prefix))
			dirty = NULL;
		describe("HEAD", 1);
	} else if (dirty) {
		die("--dirty is incompatible with committishes");
	} else {
		while (argc-- > 0) {
			describe(*argv++, argc == 0);
		}
	}
	return 0;
}
