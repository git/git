#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"
#include "config.h"
#include "lockfile.h"
#include "commit.h"
#include "tag.h"
#include "blob.h"
#include "refs.h"
#include "builtin.h"
#include "exec-cmd.h"
#include "parse-options.h"
#include "revision.h"
#include "diff.h"
#include "hashmap.h"
#include "argv-array.h"
#include "run-command.h"
#include "object-store.h"
#include "list-objects.h"
#include "commit-slab.h"

#define MAX_TAGS	(FLAG_BITS - 1)

define_commit_slab(commit_names, struct commit_name *);

static const char * const describe_usage[] = {
	N_("git describe [<options>] [<commit-ish>...]"),
	N_("git describe [<options>] --dirty"),
	NULL
};

static int debug;	/* Display lots of verbose info */
static int all;	/* Any valid ref can be used */
static int tags;	/* Allow lightweight tags */
static int longformat;
static int first_parent;
static int abbrev = -1; /* unspecified */
static int max_candidates = 10;
static struct hashmap names;
static int have_util;
static struct string_list patterns = STRING_LIST_INIT_NODUP;
static struct string_list exclude_patterns = STRING_LIST_INIT_NODUP;
static int always;
static const char *suffix, *dirty, *broken;
static struct commit_names commit_names;

/* diff-index command arguments to check if working tree is dirty. */
static const char *diff_index_args[] = {
	"diff-index", "--quiet", "HEAD", "--", NULL
};

struct commit_name {
	struct hashmap_entry entry;
	struct object_id peeled;
	struct tag *tag;
	unsigned prio:2; /* annotated tag = 2, tag = 1, head = 0 */
	unsigned name_checked:1;
	struct object_id oid;
	char *path;
};

static const char *prio_names[] = {
	N_("head"), N_("lightweight"), N_("annotated"),
};

static int commit_name_neq(const void *unused_cmp_data,
			   const struct hashmap_entry *eptr,
			   const struct hashmap_entry *entry_or_key,
			   const void *peeled)
{
	const struct commit_name *cn1, *cn2;

	cn1 = container_of(eptr, const struct commit_name, entry);
	cn2 = container_of(entry_or_key, const struct commit_name, entry);

	return !oideq(&cn1->peeled, peeled ? peeled : &cn2->peeled);
}

static inline struct commit_name *find_commit_name(const struct object_id *peeled)
{
	return hashmap_get_entry_from_hash(&names, oidhash(peeled), peeled,
						struct commit_name, entry);
}

static int replace_name(struct commit_name *e,
			       int prio,
			       const struct object_id *oid,
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
			t = lookup_tag(the_repository, &e->oid);
			if (!t || parse_tag(t))
				return 1;
			e->tag = t;
		}

		t = lookup_tag(the_repository, oid);
		if (!t || parse_tag(t))
			return 0;
		*tag = t;

		if (e->tag->date < t->date)
			return 1;
	}

	return 0;
}

static void add_to_known_names(const char *path,
			       const struct object_id *peeled,
			       int prio,
			       const struct object_id *oid)
{
	struct commit_name *e = find_commit_name(peeled);
	struct tag *tag = NULL;
	if (replace_name(e, prio, oid, &tag)) {
		if (!e) {
			e = xmalloc(sizeof(struct commit_name));
			oidcpy(&e->peeled, peeled);
			hashmap_entry_init(&e->entry, oidhash(peeled));
			hashmap_add(&names, &e->entry);
			e->path = NULL;
		}
		e->tag = tag;
		e->prio = prio;
		e->name_checked = 0;
		oidcpy(&e->oid, oid);
		free(e->path);
		e->path = xstrdup(path);
	}
}

static int get_name(const char *path, const struct object_id *oid, int flag, void *cb_data)
{
	int is_tag = 0;
	struct object_id peeled;
	int is_annotated, prio;
	const char *path_to_match = NULL;

	if (skip_prefix(path, "refs/tags/", &path_to_match)) {
		is_tag = 1;
	} else if (all) {
		if ((exclude_patterns.nr || patterns.nr) &&
		    !skip_prefix(path, "refs/heads/", &path_to_match) &&
		    !skip_prefix(path, "refs/remotes/", &path_to_match)) {
			/* Only accept reference of known type if there are match/exclude patterns */
			return 0;
		}
	} else {
		/* Reject anything outside refs/tags/ unless --all */
		return 0;
	}

	/*
	 * If we're given exclude patterns, first exclude any tag which match
	 * any of the exclude pattern.
	 */
	if (exclude_patterns.nr) {
		struct string_list_item *item;

		for_each_string_list_item(item, &exclude_patterns) {
			if (!wildmatch(item->string, path_to_match, 0))
				return 0;
		}
	}

	/*
	 * If we're given patterns, accept only tags which match at least one
	 * pattern.
	 */
	if (patterns.nr) {
		int found = 0;
		struct string_list_item *item;

		for_each_string_list_item(item, &patterns) {
			if (!wildmatch(item->string, path_to_match, 0)) {
				found = 1;
				break;
			}
		}

		if (!found)
			return 0;
	}

	/* Is it annotated? */
	if (!peel_ref(path, &peeled)) {
		is_annotated = !oideq(oid, &peeled);
	} else {
		oidcpy(&peeled, oid);
		is_annotated = 0;
	}

	/*
	 * By default, we only use annotated tags, but with --tags
	 * we fall back to lightweight ones (even without --tags,
	 * we still remember lightweight ones, only to give hints
	 * in an error message).  --all allows any refs to be used.
	 */
	if (is_annotated)
		prio = 2;
	else if (is_tag)
		prio = 1;
	else
		prio = 0;

	add_to_known_names(all ? path + 5 : path + 10, &peeled, prio, oid);
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
				commit_list_insert_by_date(p, list);
			p->object.flags |= c->object.flags;
			parents = parents->next;
		}
	}
	return seen_commits;
}

static void append_name(struct commit_name *n, struct strbuf *dst)
{
	if (n->prio == 2 && !n->tag) {
		n->tag = lookup_tag(the_repository, &n->oid);
		if (!n->tag || parse_tag(n->tag))
			die(_("annotated tag %s not available"), n->path);
	}
	if (n->tag && !n->name_checked) {
		if (!n->tag->tag)
			die(_("annotated tag %s has no embedded name"), n->path);
		if (strcmp(n->tag->tag, all ? n->path + 5 : n->path))
			warning(_("tag '%s' is really '%s' here"), n->tag->tag, n->path);
		n->name_checked = 1;
	}

	if (n->tag) {
		if (all)
			strbuf_addstr(dst, "tags/");
		strbuf_addstr(dst, n->tag->tag);
	} else {
		strbuf_addstr(dst, n->path);
	}
}

static void append_suffix(int depth, const struct object_id *oid, struct strbuf *dst)
{
	strbuf_addf(dst, "-%d-g%s", depth, find_unique_abbrev(oid, abbrev));
}

static void describe_commit(struct object_id *oid, struct strbuf *dst)
{
	struct commit *cmit, *gave_up_on = NULL;
	struct commit_list *list;
	struct commit_name *n;
	struct possible_tag all_matches[MAX_TAGS];
	unsigned int match_cnt = 0, annotated_cnt = 0, cur_match;
	unsigned long seen_commits = 0;
	unsigned int unannotated_cnt = 0;

	cmit = lookup_commit_reference(the_repository, oid);

	n = find_commit_name(&cmit->object.oid);
	if (n && (tags || all || n->prio == 2)) {
		/*
		 * Exact match to an existing ref.
		 */
		append_name(n, dst);
		if (longformat)
			append_suffix(0, n->tag ? get_tagged_oid(n->tag) : oid, dst);
		if (suffix)
			strbuf_addstr(dst, suffix);
		return;
	}

	if (!max_candidates)
		die(_("no tag exactly matches '%s'"), oid_to_hex(&cmit->object.oid));
	if (debug)
		fprintf(stderr, _("No exact match on refs or tags, searching to describe\n"));

	if (!have_util) {
		struct hashmap_iter iter;
		struct commit *c;
		struct commit_name *n;

		init_commit_names(&commit_names);
		hashmap_for_each_entry(&names, &iter, n,
					entry /* member name */) {
			c = lookup_commit_reference_gently(the_repository,
							   &n->peeled, 1);
			if (c)
				*commit_names_at(&commit_names, c) = n;
		}
		have_util = 1;
	}

	list = NULL;
	cmit->object.flags = SEEN;
	commit_list_insert(cmit, &list);
	while (list) {
		struct commit *c = pop_commit(&list);
		struct commit_list *parents = c->parents;
		struct commit_name **slot;

		seen_commits++;
		slot = commit_names_peek(&commit_names, c);
		n = slot ? *slot : NULL;
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
		/* Stop if last remaining path already covered by best candidate(s) */
		if (annotated_cnt && !list) {
			int best_depth = INT_MAX;
			unsigned best_within = 0;
			for (cur_match = 0; cur_match < match_cnt; cur_match++) {
				struct possible_tag *t = &all_matches[cur_match];
				if (t->depth < best_depth) {
					best_depth = t->depth;
					best_within = t->flag_within;
				} else if (t->depth == best_depth) {
					best_within |= t->flag_within;
				}
			}
			if ((c->object.flags & best_within) == best_within) {
				if (debug)
					fprintf(stderr, _("finished search at %s\n"),
						oid_to_hex(&c->object.oid));
				break;
			}
		}
		while (parents) {
			struct commit *p = parents->item;
			parse_commit(p);
			if (!(p->object.flags & SEEN))
				commit_list_insert_by_date(p, &list);
			p->object.flags |= c->object.flags;
			parents = parents->next;

			if (first_parent)
				break;
		}
	}

	if (!match_cnt) {
		struct object_id *cmit_oid = &cmit->object.oid;
		if (always) {
			strbuf_add_unique_abbrev(dst, cmit_oid, abbrev);
			if (suffix)
				strbuf_addstr(dst, suffix);
			return;
		}
		if (unannotated_cnt)
			die(_("No annotated tags can describe '%s'.\n"
			    "However, there were unannotated tags: try --tags."),
			    oid_to_hex(cmit_oid));
		else
			die(_("No tags can describe '%s'.\n"
			    "Try --always, or create some tags."),
			    oid_to_hex(cmit_oid));
	}

	QSORT(all_matches, match_cnt, compare_pt);

	if (gave_up_on) {
		commit_list_insert_by_date(gave_up_on, &list);
		seen_commits--;
	}
	seen_commits += finish_depth_computation(&list, &all_matches[0]);
	free_commit_list(list);

	if (debug) {
		static int label_width = -1;
		if (label_width < 0) {
			int i, w;
			for (i = 0; i < ARRAY_SIZE(prio_names); i++) {
				w = strlen(_(prio_names[i]));
				if (label_width < w)
					label_width = w;
			}
		}
		for (cur_match = 0; cur_match < match_cnt; cur_match++) {
			struct possible_tag *t = &all_matches[cur_match];
			fprintf(stderr, " %-*s %8d %s\n",
				label_width, _(prio_names[t->name->prio]),
				t->depth, t->name->path);
		}
		fprintf(stderr, _("traversed %lu commits\n"), seen_commits);
		if (gave_up_on) {
			fprintf(stderr,
				_("more than %i tags found; listed %i most recent\n"
				"gave up search at %s\n"),
				max_candidates, max_candidates,
				oid_to_hex(&gave_up_on->object.oid));
		}
	}

	append_name(all_matches[0].name, dst);
	if (abbrev)
		append_suffix(all_matches[0].depth, &cmit->object.oid, dst);
	if (suffix)
		strbuf_addstr(dst, suffix);
}

struct process_commit_data {
	struct object_id current_commit;
	struct object_id looking_for;
	struct strbuf *dst;
	struct rev_info *revs;
};

static void process_commit(struct commit *commit, void *data)
{
	struct process_commit_data *pcd = data;
	pcd->current_commit = commit->object.oid;
}

static void process_object(struct object *obj, const char *path, void *data)
{
	struct process_commit_data *pcd = data;

	if (oideq(&pcd->looking_for, &obj->oid) && !pcd->dst->len) {
		reset_revision_walk();
		describe_commit(&pcd->current_commit, pcd->dst);
		strbuf_addf(pcd->dst, ":%s", path);
		free_commit_list(pcd->revs->commits);
		pcd->revs->commits = NULL;
	}
}

static void describe_blob(struct object_id oid, struct strbuf *dst)
{
	struct rev_info revs;
	struct argv_array args = ARGV_ARRAY_INIT;
	struct process_commit_data pcd = { null_oid, oid, dst, &revs};

	argv_array_pushl(&args, "internal: The first arg is not parsed",
		"--objects", "--in-commit-order", "--reverse", "HEAD",
		NULL);

	repo_init_revisions(the_repository, &revs, NULL);
	if (setup_revisions(args.argc, args.argv, &revs, NULL) > 1)
		BUG("setup_revisions could not handle all args?");

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");

	traverse_commit_list(&revs, process_commit, process_object, &pcd);
	reset_revision_walk();
}

static void describe(const char *arg, int last_one)
{
	struct object_id oid;
	struct commit *cmit;
	struct strbuf sb = STRBUF_INIT;

	if (debug)
		fprintf(stderr, _("describe %s\n"), arg);

	if (get_oid(arg, &oid))
		die(_("Not a valid object name %s"), arg);
	cmit = lookup_commit_reference_gently(the_repository, &oid, 1);

	if (cmit)
		describe_commit(&oid, &sb);
	else if (oid_object_info(the_repository, &oid, NULL) == OBJ_BLOB)
		describe_blob(oid, &sb);
	else
		die(_("%s is neither a commit nor blob"), arg);

	puts(sb.buf);

	if (!last_one)
		clear_commit_marks(cmit, -1);

	strbuf_release(&sb);
}

int cmd_describe(int argc, const char **argv, const char *prefix)
{
	int contains = 0;
	struct option options[] = {
		OPT_BOOL(0, "contains",   &contains, N_("find the tag that comes after the commit")),
		OPT_BOOL(0, "debug",      &debug, N_("debug search strategy on stderr")),
		OPT_BOOL(0, "all",        &all, N_("use any ref")),
		OPT_BOOL(0, "tags",       &tags, N_("use any tag, even unannotated")),
		OPT_BOOL(0, "long",       &longformat, N_("always use long format")),
		OPT_BOOL(0, "first-parent", &first_parent, N_("only follow first parent")),
		OPT__ABBREV(&abbrev),
		OPT_SET_INT(0, "exact-match", &max_candidates,
			    N_("only output exact matches"), 0),
		OPT_INTEGER(0, "candidates", &max_candidates,
			    N_("consider <n> most recent tags (default: 10)")),
		OPT_STRING_LIST(0, "match", &patterns, N_("pattern"),
			   N_("only consider tags matching <pattern>")),
		OPT_STRING_LIST(0, "exclude", &exclude_patterns, N_("pattern"),
			   N_("do not consider tags matching <pattern>")),
		OPT_BOOL(0, "always",        &always,
			N_("show abbreviated commit object as fallback")),
		{OPTION_STRING, 0, "dirty",  &dirty, N_("mark"),
			N_("append <mark> on dirty working tree (default: \"-dirty\")"),
			PARSE_OPT_OPTARG, NULL, (intptr_t) "-dirty"},
		{OPTION_STRING, 0, "broken",  &broken, N_("mark"),
			N_("append <mark> on broken working tree (default: \"-broken\")"),
			PARSE_OPT_OPTARG, NULL, (intptr_t) "-broken"},
		OPT_END(),
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options, describe_usage, 0);
	if (abbrev < 0)
		abbrev = DEFAULT_ABBREV;

	if (max_candidates < 0)
		max_candidates = 0;
	else if (max_candidates > MAX_TAGS)
		max_candidates = MAX_TAGS;

	save_commit_buffer = 0;

	if (longformat && abbrev == 0)
		die(_("--long is incompatible with --abbrev=0"));

	if (contains) {
		struct string_list_item *item;
		struct argv_array args;

		argv_array_init(&args);
		argv_array_pushl(&args, "name-rev",
				 "--peel-tag", "--name-only", "--no-undefined",
				 NULL);
		if (always)
			argv_array_push(&args, "--always");
		if (!all) {
			argv_array_push(&args, "--tags");
			for_each_string_list_item(item, &patterns)
				argv_array_pushf(&args, "--refs=refs/tags/%s", item->string);
			for_each_string_list_item(item, &exclude_patterns)
				argv_array_pushf(&args, "--exclude=refs/tags/%s", item->string);
		}
		if (argc)
			argv_array_pushv(&args, argv);
		else
			argv_array_push(&args, "HEAD");
		return cmd_name_rev(args.argc, args.argv, prefix);
	}

	hashmap_init(&names, commit_name_neq, NULL, 0);
	for_each_rawref(get_name, NULL);
	if (!hashmap_get_size(&names) && !always)
		die(_("No names found, cannot describe anything."));

	if (argc == 0) {
		if (broken) {
			struct child_process cp = CHILD_PROCESS_INIT;
			argv_array_pushv(&cp.args, diff_index_args);
			cp.git_cmd = 1;
			cp.no_stdin = 1;
			cp.no_stdout = 1;

			if (!dirty)
				dirty = "-dirty";

			switch (run_command(&cp)) {
			case 0:
				suffix = NULL;
				break;
			case 1:
				suffix = dirty;
				break;
			default:
				/* diff-index aborted abnormally */
				suffix = broken;
			}
		} else if (dirty) {
			struct lock_file index_lock = LOCK_INIT;
			struct rev_info revs;
			struct argv_array args = ARGV_ARRAY_INIT;
			int fd, result;

			setup_work_tree();
			read_cache();
			refresh_index(&the_index, REFRESH_QUIET|REFRESH_UNMERGED,
				      NULL, NULL, NULL);
			fd = hold_locked_index(&index_lock, 0);
			if (0 <= fd)
				repo_update_index_if_able(the_repository, &index_lock);

			repo_init_revisions(the_repository, &revs, prefix);
			argv_array_pushv(&args, diff_index_args);
			if (setup_revisions(args.argc, args.argv, &revs, NULL) != 1)
				BUG("malformed internal diff-index command line");
			result = run_diff_index(&revs, 0);

			if (!diff_result_code(&revs.diffopt, result))
				suffix = NULL;
			else
				suffix = dirty;
		}
		describe("HEAD", 1);
	} else if (dirty) {
		die(_("--dirty is incompatible with commit-ishes"));
	} else if (broken) {
		die(_("--broken is incompatible with commit-ishes"));
	} else {
		while (argc-- > 0)
			describe(*argv++, argc == 0);
	}
	return 0;
}
