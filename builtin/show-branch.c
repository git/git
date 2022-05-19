#include "cache.h"
#include "config.h"
#include "pretty.h"
#include "refs.h"
#include "builtin.h"
#include "color.h"
#include "strvec.h"
#include "parse-options.h"
#include "dir.h"
#include "cummit-slab.h"
#include "date.h"

static const char* show_branch_usage[] = {
    N_("git show-branch [-a | --all] [-r | --remotes] [--topo-order | --date-order]\n"
       "                [--current] [--color[=<when>] | --no-color] [--sparse]\n"
       "                [--more=<n> | --list | --independent | --merge-base]\n"
       "                [--no-name | --sha1-name] [--topics] [(<rev> | <glob>)...]"),
    N_("git show-branch (-g | --reflog)[=<n>[,<base>]] [--list] [<ref>]"),
    NULL
};

static int showbranch_use_color = -1;

static struct strvec default_args = STRVEC_INIT;

/*
 * TODO: convert this use of cummit->object.flags to cummit-slab
 * instead to store a pointer to ref name directly. Then use the same
 * UNINTERESTING definition from revision.h here.
 */
#define UNINTERESTING	01

#define REV_SHIFT	 2
#define MAX_REVS	(FLAG_BITS - REV_SHIFT) /* should not exceed bits_per_int - REV_SHIFT */

#define DEFAULT_REFLOG	4

static const char *get_color_code(int idx)
{
	if (want_color(showbranch_use_color))
		return column_colors_ansi[idx % column_colors_ansi_max];
	return "";
}

static const char *get_color_reset_code(void)
{
	if (want_color(showbranch_use_color))
		return GIT_COLOR_RESET;
	return "";
}

static struct cummit *interesting(struct cummit_list *list)
{
	while (list) {
		struct cummit *cummit = list->item;
		list = list->next;
		if (cummit->object.flags & UNINTERESTING)
			continue;
		return cummit;
	}
	return NULL;
}

struct cummit_name {
	const char *head_name; /* which head's ancestor? */
	int generation; /* how many parents away from head_name */
};

define_cummit_slab(cummit_name_slab, struct cummit_name *);
static struct cummit_name_slab name_slab;

static struct cummit_name *cummit_to_name(struct cummit *cummit)
{
	return *cummit_name_slab_at(&name_slab, cummit);
}


/* Name the cummit as nth generation ancestor of head_name;
 * we count only the first-parent relationship for naming purposes.
 */
static void name_cummit(struct cummit *cummit, const char *head_name, int nth)
{
	struct cummit_name *name;

	name = *cummit_name_slab_at(&name_slab, cummit);
	if (!name) {
		name = xmalloc(sizeof(*name));
		*cummit_name_slab_at(&name_slab, cummit) = name;
	}
	name->head_name = head_name;
	name->generation = nth;
}

/* Parent is the first parent of the cummit.  We may name it
 * as (n+1)th generation ancestor of the same head_name as
 * cummit is nth generation ancestor of, if that generation
 * number is better than the name it already has.
 */
static void name_parent(struct cummit *cummit, struct cummit *parent)
{
	struct cummit_name *cummit_name = cummit_to_name(cummit);
	struct cummit_name *parent_name = cummit_to_name(parent);
	if (!cummit_name)
		return;
	if (!parent_name ||
	    cummit_name->generation + 1 < parent_name->generation)
		name_cummit(parent, cummit_name->head_name,
			    cummit_name->generation + 1);
}

static int name_first_parent_chain(struct cummit *c)
{
	int i = 0;
	while (c) {
		struct cummit *p;
		if (!cummit_to_name(c))
			break;
		if (!c->parents)
			break;
		p = c->parents->item;
		if (!cummit_to_name(p)) {
			name_parent(c, p);
			i++;
		}
		else
			break;
		c = p;
	}
	return i;
}

static void name_cummits(struct cummit_list *list,
			 struct cummit **rev,
			 char **ref_name,
			 int num_rev)
{
	struct cummit_list *cl;
	struct cummit *c;
	int i;

	/* First give names to the given heads */
	for (cl = list; cl; cl = cl->next) {
		c = cl->item;
		if (cummit_to_name(c))
			continue;
		for (i = 0; i < num_rev; i++) {
			if (rev[i] == c) {
				name_cummit(c, ref_name[i], 0);
				break;
			}
		}
	}

	/* Then cummits on the first parent ancestry chain */
	do {
		i = 0;
		for (cl = list; cl; cl = cl->next) {
			i += name_first_parent_chain(cl->item);
		}
	} while (i);

	/* Finally, any unnamed cummits */
	do {
		i = 0;
		for (cl = list; cl; cl = cl->next) {
			struct cummit_list *parents;
			struct cummit_name *n;
			int nth;
			c = cl->item;
			if (!cummit_to_name(c))
				continue;
			n = cummit_to_name(c);
			parents = c->parents;
			nth = 0;
			while (parents) {
				struct cummit *p = parents->item;
				struct strbuf newname = STRBUF_INIT;
				parents = parents->next;
				nth++;
				if (cummit_to_name(p))
					continue;
				switch (n->generation) {
				case 0:
					strbuf_addstr(&newname, n->head_name);
					break;
				case 1:
					strbuf_addf(&newname, "%s^", n->head_name);
					break;
				default:
					strbuf_addf(&newname, "%s~%d",
						    n->head_name, n->generation);
					break;
				}
				if (nth == 1)
					strbuf_addch(&newname, '^');
				else
					strbuf_addf(&newname, "^%d", nth);
				name_cummit(p, strbuf_detach(&newname, NULL), 0);
				i++;
				name_first_parent_chain(p);
			}
		}
	} while (i);
}

static int mark_seen(struct cummit *cummit, struct cummit_list **seen_p)
{
	if (!cummit->object.flags) {
		cummit_list_insert(cummit, seen_p);
		return 1;
	}
	return 0;
}

static void join_revs(struct cummit_list **list_p,
		      struct cummit_list **seen_p,
		      int num_rev, int extra)
{
	int all_mask = ((1u << (REV_SHIFT + num_rev)) - 1);
	int all_revs = all_mask & ~((1u << REV_SHIFT) - 1);

	while (*list_p) {
		struct cummit_list *parents;
		int still_interesting = !!interesting(*list_p);
		struct cummit *cummit = pop_cummit(list_p);
		int flags = cummit->object.flags & all_mask;

		if (!still_interesting && extra <= 0)
			break;

		mark_seen(cummit, seen_p);
		if ((flags & all_revs) == all_revs)
			flags |= UNINTERESTING;
		parents = cummit->parents;

		while (parents) {
			struct cummit *p = parents->item;
			int this_flag = p->object.flags;
			parents = parents->next;
			if ((this_flag & flags) == flags)
				continue;
			parse_cummit(p);
			if (mark_seen(p, seen_p) && !still_interesting)
				extra--;
			p->object.flags |= flags;
			cummit_list_insert_by_date(p, list_p);
		}
	}

	/*
	 * Postprocess to complete well-poisoning.
	 *
	 * At this point we have all the cummits we have seen in
	 * seen_p list.  Mark anything that can be reached from
	 * uninteresting cummits not interesting.
	 */
	for (;;) {
		int changed = 0;
		struct cummit_list *s;
		for (s = *seen_p; s; s = s->next) {
			struct cummit *c = s->item;
			struct cummit_list *parents;

			if (((c->object.flags & all_revs) != all_revs) &&
			    !(c->object.flags & UNINTERESTING))
				continue;

			/* The current cummit is either a merge base or
			 * already uninteresting one.  Mark its parents
			 * as uninteresting cummits _only_ if they are
			 * already parsed.  No reason to find new ones
			 * here.
			 */
			parents = c->parents;
			while (parents) {
				struct cummit *p = parents->item;
				parents = parents->next;
				if (!(p->object.flags & UNINTERESTING)) {
					p->object.flags |= UNINTERESTING;
					changed = 1;
				}
			}
		}
		if (!changed)
			break;
	}
}

static void show_one_cummit(struct cummit *cummit, int no_name)
{
	struct strbuf pretty = STRBUF_INIT;
	const char *pretty_str = "(unavailable)";
	struct cummit_name *name = cummit_to_name(cummit);

	if (cummit->object.parsed) {
		pp_cummit_easy(CMIT_FMT_ONELINE, cummit, &pretty);
		pretty_str = pretty.buf;
	}
	skip_prefix(pretty_str, "[PATCH] ", &pretty_str);

	if (!no_name) {
		if (name && name->head_name) {
			printf("[%s", name->head_name);
			if (name->generation) {
				if (name->generation == 1)
					printf("^");
				else
					printf("~%d", name->generation);
			}
			printf("] ");
		}
		else
			printf("[%s] ",
			       find_unique_abbrev(&cummit->object.oid,
						  DEFAULT_ABBREV));
	}
	puts(pretty_str);
	strbuf_release(&pretty);
}

static char *ref_name[MAX_REVS + 1];
static int ref_name_cnt;

static const char *find_digit_prefix(const char *s, int *v)
{
	const char *p;
	int ver;
	char ch;

	for (p = s, ver = 0;
	     '0' <= (ch = *p) && ch <= '9';
	     p++)
		ver = ver * 10 + ch - '0';
	*v = ver;
	return p;
}


static int version_cmp(const char *a, const char *b)
{
	while (1) {
		int va, vb;

		a = find_digit_prefix(a, &va);
		b = find_digit_prefix(b, &vb);
		if (va != vb)
			return va - vb;

		while (1) {
			int ca = *a;
			int cb = *b;
			if ('0' <= ca && ca <= '9')
				ca = 0;
			if ('0' <= cb && cb <= '9')
				cb = 0;
			if (ca != cb)
				return ca - cb;
			if (!ca)
				break;
			a++;
			b++;
		}
		if (!*a && !*b)
			return 0;
	}
}

static int compare_ref_name(const void *a_, const void *b_)
{
	const char * const*a = a_, * const*b = b_;
	return version_cmp(*a, *b);
}

static void sort_ref_range(int bottom, int top)
{
	QSORT(ref_name + bottom, top - bottom, compare_ref_name);
}

static int append_ref(const char *refname, const struct object_id *oid,
		      int allow_dups)
{
	struct cummit *cummit = lookup_cummit_reference_gently(the_repository,
							       oid, 1);
	int i;

	if (!cummit)
		return 0;

	if (!allow_dups) {
		/* Avoid adding the same thing twice */
		for (i = 0; i < ref_name_cnt; i++)
			if (!strcmp(refname, ref_name[i]))
				return 0;
	}
	if (MAX_REVS <= ref_name_cnt) {
		warning(Q_("ignoring %s; cannot handle more than %d ref",
			   "ignoring %s; cannot handle more than %d refs",
			   MAX_REVS), refname, MAX_REVS);
		return 0;
	}
	ref_name[ref_name_cnt++] = xstrdup(refname);
	ref_name[ref_name_cnt] = NULL;
	return 0;
}

static int append_head_ref(const char *refname, const struct object_id *oid,
			   int flag, void *cb_data)
{
	struct object_id tmp;
	int ofs = 11;
	if (!starts_with(refname, "refs/heads/"))
		return 0;
	/* If both heads/foo and tags/foo exists, get_sha1 would
	 * get confused.
	 */
	if (get_oid(refname + ofs, &tmp) || !oideq(&tmp, oid))
		ofs = 5;
	return append_ref(refname + ofs, oid, 0);
}

static int append_remote_ref(const char *refname, const struct object_id *oid,
			     int flag, void *cb_data)
{
	struct object_id tmp;
	int ofs = 13;
	if (!starts_with(refname, "refs/remotes/"))
		return 0;
	/* If both heads/foo and tags/foo exists, get_sha1 would
	 * get confused.
	 */
	if (get_oid(refname + ofs, &tmp) || !oideq(&tmp, oid))
		ofs = 5;
	return append_ref(refname + ofs, oid, 0);
}

static int append_tag_ref(const char *refname, const struct object_id *oid,
			  int flag, void *cb_data)
{
	if (!starts_with(refname, "refs/tags/"))
		return 0;
	return append_ref(refname + 5, oid, 0);
}

static const char *match_ref_pattern = NULL;
static int match_ref_slash = 0;

static int append_matching_ref(const char *refname, const struct object_id *oid,
			       int flag, void *cb_data)
{
	/* we want to allow pattern hold/<asterisk> to show all
	 * branches under refs/heads/hold/, and v0.99.9? to show
	 * refs/tags/v0.99.9a and friends.
	 */
	const char *tail;
	int slash = count_slashes(refname);
	for (tail = refname; *tail && match_ref_slash < slash; )
		if (*tail++ == '/')
			slash--;
	if (!*tail)
		return 0;
	if (wildmatch(match_ref_pattern, tail, 0))
		return 0;
	if (starts_with(refname, "refs/heads/"))
		return append_head_ref(refname, oid, flag, cb_data);
	if (starts_with(refname, "refs/tags/"))
		return append_tag_ref(refname, oid, flag, cb_data);
	return append_ref(refname, oid, 0);
}

static void snarf_refs(int head, int remotes)
{
	if (head) {
		int orig_cnt = ref_name_cnt;

		for_each_ref(append_head_ref, NULL);
		sort_ref_range(orig_cnt, ref_name_cnt);
	}
	if (remotes) {
		int orig_cnt = ref_name_cnt;

		for_each_ref(append_remote_ref, NULL);
		sort_ref_range(orig_cnt, ref_name_cnt);
	}
}

static int rev_is_head(const char *head, const char *name)
{
	if (!head)
		return 0;
	skip_prefix(head, "refs/heads/", &head);
	if (!skip_prefix(name, "refs/heads/", &name))
		skip_prefix(name, "heads/", &name);
	return !strcmp(head, name);
}

static int show_merge_base(struct cummit_list *seen, int num_rev)
{
	int all_mask = ((1u << (REV_SHIFT + num_rev)) - 1);
	int all_revs = all_mask & ~((1u << REV_SHIFT) - 1);
	int exit_status = 1;

	while (seen) {
		struct cummit *cummit = pop_cummit(&seen);
		int flags = cummit->object.flags & all_mask;
		if (!(flags & UNINTERESTING) &&
		    ((flags & all_revs) == all_revs)) {
			puts(oid_to_hex(&cummit->object.oid));
			exit_status = 0;
			cummit->object.flags |= UNINTERESTING;
		}
	}
	return exit_status;
}

static int show_independent(struct cummit **rev,
			    int num_rev,
			    unsigned int *rev_mask)
{
	int i;

	for (i = 0; i < num_rev; i++) {
		struct cummit *cummit = rev[i];
		unsigned int flag = rev_mask[i];

		if (cummit->object.flags == flag)
			puts(oid_to_hex(&cummit->object.oid));
		cummit->object.flags |= UNINTERESTING;
	}
	return 0;
}

static void append_one_rev(const char *av)
{
	struct object_id revkey;
	if (!get_oid(av, &revkey)) {
		append_ref(av, &revkey, 0);
		return;
	}
	if (strpbrk(av, "*?[")) {
		/* glob style match */
		int saved_matches = ref_name_cnt;

		match_ref_pattern = av;
		match_ref_slash = count_slashes(av);
		for_each_ref(append_matching_ref, NULL);
		if (saved_matches == ref_name_cnt &&
		    ref_name_cnt < MAX_REVS)
			error(_("no matching refs with %s"), av);
		sort_ref_range(saved_matches, ref_name_cnt);
		return;
	}
	die("bad sha1 reference %s", av);
}

static int git_show_branch_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "showbranch.default")) {
		if (!value)
			return config_error_nonbool(var);
		/*
		 * default_arg is now passed to parse_options(), so we need to
		 * mimic the real argv a bit better.
		 */
		if (!default_args.nr)
			strvec_push(&default_args, "show-branch");
		strvec_push(&default_args, value);
		return 0;
	}

	if (!strcmp(var, "color.showbranch")) {
		showbranch_use_color = git_config_colorbool(var, value);
		return 0;
	}

	return git_color_default_config(var, value, cb);
}

static int omit_in_dense(struct cummit *cummit, struct cummit **rev, int n)
{
	/* If the cummit is tip of the named branches, do not
	 * omit it.
	 * Otherwise, if it is a merge that is reachable from only one
	 * tip, it is not that interesting.
	 */
	int i, flag, count;
	for (i = 0; i < n; i++)
		if (rev[i] == cummit)
			return 0;
	flag = cummit->object.flags;
	for (i = count = 0; i < n; i++) {
		if (flag & (1u << (i + REV_SHIFT)))
			count++;
	}
	if (count == 1)
		return 1;
	return 0;
}

static int reflog = 0;

static int parse_reflog_param(const struct option *opt, const char *arg,
			      int unset)
{
	char *ep;
	const char **base = (const char **)opt->value;
	BUG_ON_OPT_NEG(unset);
	if (!arg)
		arg = "";
	reflog = strtoul(arg, &ep, 10);
	if (*ep == ',')
		*base = ep + 1;
	else if (*ep)
		return error("unrecognized reflog param '%s'", arg);
	else
		*base = NULL;
	if (reflog <= 0)
		reflog = DEFAULT_REFLOG;
	return 0;
}

int cmd_show_branch(int ac, const char **av, const char *prefix)
{
	struct cummit *rev[MAX_REVS], *cummit;
	char *reflog_msg[MAX_REVS];
	struct cummit_list *list = NULL, *seen = NULL;
	unsigned int rev_mask[MAX_REVS];
	int num_rev, i, extra = 0;
	int all_heads = 0, all_remotes = 0;
	int all_mask, all_revs;
	enum rev_sort_order sort_order = REV_SORT_IN_GRAPH_ORDER;
	char *head;
	struct object_id head_oid;
	int merge_base = 0;
	int independent = 0;
	int no_name = 0;
	int sha1_name = 0;
	int shown_merge_point = 0;
	int with_current_branch = 0;
	int head_at = -1;
	int topics = 0;
	int dense = 1;
	const char *reflog_base = NULL;
	struct option builtin_show_branch_options[] = {
		OPT_BOOL('a', "all", &all_heads,
			 N_("show remote-tracking and local branches")),
		OPT_BOOL('r', "remotes", &all_remotes,
			 N_("show remote-tracking branches")),
		OPT__COLOR(&showbranch_use_color,
			    N_("color '*!+-' corresponding to the branch")),
		{ OPTION_INTEGER, 0, "more", &extra, N_("n"),
			    N_("show <n> more cummits after the common ancestor"),
			    PARSE_OPT_OPTARG, NULL, (intptr_t)1 },
		OPT_SET_INT(0, "list", &extra, N_("synonym to more=-1"), -1),
		OPT_BOOL(0, "no-name", &no_name, N_("suppress naming strings")),
		OPT_BOOL(0, "current", &with_current_branch,
			 N_("include the current branch")),
		OPT_BOOL(0, "sha1-name", &sha1_name,
			 N_("name cummits with their object names")),
		OPT_BOOL(0, "merge-base", &merge_base,
			 N_("show possible merge bases")),
		OPT_BOOL(0, "independent", &independent,
			    N_("show refs unreachable from any other ref")),
		OPT_SET_INT(0, "topo-order", &sort_order,
			    N_("show cummits in topological order"),
			    REV_SORT_IN_GRAPH_ORDER),
		OPT_BOOL(0, "topics", &topics,
			 N_("show only cummits not on the first branch")),
		OPT_SET_INT(0, "sparse", &dense,
			    N_("show merges reachable from only one tip"), 0),
		OPT_SET_INT(0, "date-order", &sort_order,
			    N_("topologically sort, maintaining date order "
			       "where possible"),
			    REV_SORT_BY_cummit_DATE),
		OPT_CALLBACK_F('g', "reflog", &reflog_base, N_("<n>[,<base>]"),
			    N_("show <n> most recent ref-log entries starting at "
			       "base"),
			    PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			    parse_reflog_param),
		OPT_END()
	};

	init_cummit_name_slab(&name_slab);

	git_config(git_show_branch_config, NULL);

	/* If nothing is specified, try the default first */
	if (ac == 1 && default_args.nr) {
		ac = default_args.nr;
		av = default_args.v;
	}

	ac = parse_options(ac, av, prefix, builtin_show_branch_options,
			   show_branch_usage, PARSE_OPT_STOP_AT_NON_OPTION);
	if (all_heads)
		all_remotes = 1;

	if (extra || reflog) {
		/* "listing" mode is incompatible with
		 * independent nor merge-base modes.
		 */
		if (independent || merge_base)
			usage_with_options(show_branch_usage,
					   builtin_show_branch_options);
		if (reflog && ((0 < extra) || all_heads || all_remotes))
			/*
			 * Asking for --more in reflog mode does not
			 * make sense.  --list is Ok.
			 *
			 * Also --all and --remotes do not make sense either.
			 */
			die(_("options '%s' and '%s' cannot be used together"), "--reflog",
				"--all/--remotes/--independent/--merge-base");
	}

	/* If nothing is specified, show all branches by default */
	if (ac <= topics && all_heads + all_remotes == 0)
		all_heads = 1;

	if (reflog) {
		struct object_id oid;
		char *ref;
		int base = 0;
		unsigned int flags = 0;

		if (ac == 0) {
			static const char *fake_av[2];

			fake_av[0] = resolve_refdup("HEAD",
						    RESOLVE_REF_READING, &oid,
						    NULL);
			fake_av[1] = NULL;
			av = fake_av;
			ac = 1;
			if (!*av)
				die(_("no branches given, and HEAD is not valid"));
		}
		if (ac != 1)
			die(_("--reflog option needs one branch name"));

		if (MAX_REVS < reflog)
			die(Q_("only %d entry can be shown at one time.",
			       "only %d entries can be shown at one time.",
			       MAX_REVS), MAX_REVS);
		if (!dwim_ref(*av, strlen(*av), &oid, &ref, 0))
			die(_("no such ref %s"), *av);

		/* Has the base been specified? */
		if (reflog_base) {
			char *ep;
			base = strtoul(reflog_base, &ep, 10);
			if (*ep) {
				/* Ah, that is a date spec... */
				timestamp_t at;
				at = approxidate(reflog_base);
				read_ref_at(get_main_ref_store(the_repository),
					    ref, flags, at, -1, &oid, NULL,
					    NULL, NULL, &base);
			}
		}

		for (i = 0; i < reflog; i++) {
			char *logmsg;
			char *nth_desc;
			const char *msg;
			char *end;
			timestamp_t timestamp;
			int tz;

			if (read_ref_at(get_main_ref_store(the_repository),
					ref, flags, 0, base + i, &oid, &logmsg,
					&timestamp, &tz, NULL)) {
				reflog = i;
				break;
			}

			end = strchr(logmsg, '\n');
			if (end)
				*end = '\0';

			msg = (*logmsg == '\0') ? "(none)" : logmsg;
			reflog_msg[i] = xstrfmt("(%s) %s",
						show_date(timestamp, tz,
							  DATE_MODE(RELATIVE)),
						msg);
			free(logmsg);

			nth_desc = xstrfmt("%s@{%d}", *av, base+i);
			append_ref(nth_desc, &oid, 1);
			free(nth_desc);
		}
		free(ref);
	}
	else {
		while (0 < ac) {
			append_one_rev(*av);
			ac--; av++;
		}
		if (all_heads + all_remotes)
			snarf_refs(all_heads, all_remotes);
	}

	head = resolve_refdup("HEAD", RESOLVE_REF_READING,
			      &head_oid, NULL);

	if (with_current_branch && head) {
		int has_head = 0;
		for (i = 0; !has_head && i < ref_name_cnt; i++) {
			/* We are only interested in adding the branch
			 * HEAD points at.
			 */
			if (rev_is_head(head, ref_name[i]))
				has_head++;
		}
		if (!has_head) {
			const char *name = head;
			skip_prefix(name, "refs/heads/", &name);
			append_one_rev(name);
		}
	}

	if (!ref_name_cnt) {
		fprintf(stderr, "No revs to be shown.\n");
		exit(0);
	}

	for (num_rev = 0; ref_name[num_rev]; num_rev++) {
		struct object_id revkey;
		unsigned int flag = 1u << (num_rev + REV_SHIFT);

		if (MAX_REVS <= num_rev)
			die(Q_("cannot handle more than %d rev.",
			       "cannot handle more than %d revs.",
			       MAX_REVS), MAX_REVS);
		if (get_oid(ref_name[num_rev], &revkey))
			die(_("'%s' is not a valid ref."), ref_name[num_rev]);
		cummit = lookup_cummit_reference(the_repository, &revkey);
		if (!cummit)
			die(_("cannot find cummit %s (%s)"),
			    ref_name[num_rev], oid_to_hex(&revkey));
		parse_cummit(cummit);
		mark_seen(cummit, &seen);

		/* rev#0 uses bit REV_SHIFT, rev#1 uses bit REV_SHIFT+1,
		 * and so on.  REV_SHIFT bits from bit 0 are used for
		 * internal bookkeeping.
		 */
		cummit->object.flags |= flag;
		if (cummit->object.flags == flag)
			cummit_list_insert_by_date(cummit, &list);
		rev[num_rev] = cummit;
	}
	for (i = 0; i < num_rev; i++)
		rev_mask[i] = rev[i]->object.flags;

	if (0 <= extra)
		join_revs(&list, &seen, num_rev, extra);

	cummit_list_sort_by_date(&seen);

	if (merge_base)
		return show_merge_base(seen, num_rev);

	if (independent)
		return show_independent(rev, num_rev, rev_mask);

	/* Show list; --more=-1 means list-only */
	if (1 < num_rev || extra < 0) {
		for (i = 0; i < num_rev; i++) {
			int j;
			int is_head = rev_is_head(head, ref_name[i]) &&
				      oideq(&head_oid, &rev[i]->object.oid);
			if (extra < 0)
				printf("%c [%s] ",
				       is_head ? '*' : ' ', ref_name[i]);
			else {
				for (j = 0; j < i; j++)
					putchar(' ');
				printf("%s%c%s [%s] ",
				       get_color_code(i),
				       is_head ? '*' : '!',
				       get_color_reset_code(), ref_name[i]);
			}

			if (!reflog) {
				/* header lines never need name */
				show_one_cummit(rev[i], 1);
			}
			else
				puts(reflog_msg[i]);

			if (is_head)
				head_at = i;
		}
		if (0 <= extra) {
			for (i = 0; i < num_rev; i++)
				putchar('-');
			putchar('\n');
		}
	}
	if (extra < 0)
		exit(0);

	/* Sort topologically */
	sort_in_topological_order(&seen, sort_order);

	/* Give names to cummits */
	if (!sha1_name && !no_name)
		name_cummits(seen, rev, ref_name, num_rev);

	all_mask = ((1u << (REV_SHIFT + num_rev)) - 1);
	all_revs = all_mask & ~((1u << REV_SHIFT) - 1);

	while (seen) {
		struct cummit *cummit = pop_cummit(&seen);
		int this_flag = cummit->object.flags;
		int is_merge_point = ((this_flag & all_revs) == all_revs);

		shown_merge_point |= is_merge_point;

		if (1 < num_rev) {
			int is_merge = !!(cummit->parents &&
					  cummit->parents->next);
			if (topics &&
			    !is_merge_point &&
			    (this_flag & (1u << REV_SHIFT)))
				continue;
			if (dense && is_merge &&
			    omit_in_dense(cummit, rev, num_rev))
				continue;
			for (i = 0; i < num_rev; i++) {
				int mark;
				if (!(this_flag & (1u << (i + REV_SHIFT))))
					mark = ' ';
				else if (is_merge)
					mark = '-';
				else if (i == head_at)
					mark = '*';
				else
					mark = '+';
				if (mark == ' ')
					putchar(mark);
				else
					printf("%s%c%s",
					       get_color_code(i),
					       mark, get_color_reset_code());
			}
			putchar(' ');
		}
		show_one_cummit(cummit, no_name);

		if (shown_merge_point && --extra < 0)
			break;
	}
	return 0;
}
