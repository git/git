#include <stdlib.h>
#include <fnmatch.h>
#include "cache.h"
#include "commit.h"
#include "refs.h"

static const char show_branch_usage[] =
"git-show-branch [--dense] [--current] [--all] [--heads] [--tags] [--topo-order] [--more=count | --list | --independent | --merge-base ] [--topics] [<refs>...]";

static int default_num = 0;
static int default_alloc = 0;
static char **default_arg = NULL;

#define UNINTERESTING	01

#define REV_SHIFT	 2
#define MAX_REVS	29 /* should not exceed bits_per_int - REV_SHIFT */

static struct commit *interesting(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		list = list->next;
		if (commit->object.flags & UNINTERESTING)
			continue;
		return commit;
	}
	return NULL;
}

static struct commit *pop_one_commit(struct commit_list **list_p)
{
	struct commit *commit;
	struct commit_list *list;
	list = *list_p;
	commit = list->item;
	*list_p = list->next;
	free(list);
	return commit;
}

struct commit_name {
	const char *head_name; /* which head's ancestor? */
	int generation; /* how many parents away from head_name */
};

/* Name the commit as nth generation ancestor of head_name;
 * we count only the first-parent relationship for naming purposes.
 */
static void name_commit(struct commit *commit, const char *head_name, int nth)
{
	struct commit_name *name;
	if (!commit->object.util)
		commit->object.util = xmalloc(sizeof(struct commit_name));
	name = commit->object.util;
	name->head_name = head_name;
	name->generation = nth;
}

/* Parent is the first parent of the commit.  We may name it
 * as (n+1)th generation ancestor of the same head_name as
 * commit is nth generation ancestor of, if that generation
 * number is better than the name it already has.
 */
static void name_parent(struct commit *commit, struct commit *parent)
{
	struct commit_name *commit_name = commit->object.util;
	struct commit_name *parent_name = parent->object.util;
	if (!commit_name)
		return;
	if (!parent_name ||
	    commit_name->generation + 1 < parent_name->generation)
		name_commit(parent, commit_name->head_name,
			    commit_name->generation + 1);
}

static int name_first_parent_chain(struct commit *c)
{
	int i = 0;
	while (c) {
		struct commit *p;
		if (!c->object.util)
			break;
		if (!c->parents)
			break;
		p = c->parents->item;
		if (!p->object.util) {
			name_parent(c, p);
			i++;
		}
		c = p;
	}
	return i;
}

static void name_commits(struct commit_list *list,
			 struct commit **rev,
			 char **ref_name,
			 int num_rev)
{
	struct commit_list *cl;
	struct commit *c;
	int i;

	/* First give names to the given heads */
	for (cl = list; cl; cl = cl->next) {
		c = cl->item;
		if (c->object.util)
			continue;
		for (i = 0; i < num_rev; i++) {
			if (rev[i] == c) {
				name_commit(c, ref_name[i], 0);
				break;
			}
		}
	}

	/* Then commits on the first parent ancestry chain */
	do {
		i = 0;
		for (cl = list; cl; cl = cl->next) {
			i += name_first_parent_chain(cl->item);
		}
	} while (i);

	/* Finally, any unnamed commits */
	do {
		i = 0;
		for (cl = list; cl; cl = cl->next) {
			struct commit_list *parents;
			struct commit_name *n;
			int nth;
			c = cl->item;
			if (!c->object.util)
				continue;
			n = c->object.util;
			parents = c->parents;
			nth = 0;
			while (parents) {
				struct commit *p = parents->item;
				char newname[1000], *en;
				parents = parents->next;
				nth++;
				if (p->object.util)
					continue;
				en = newname;
				switch (n->generation) {
				case 0:
					en += sprintf(en, "%s", n->head_name);
					break;
				case 1:
					en += sprintf(en, "%s^", n->head_name);
					break;
				default:
					en += sprintf(en, "%s~%d",
						n->head_name, n->generation);
					break;
				}
				if (nth == 1)
					en += sprintf(en, "^");
				else
					en += sprintf(en, "^%d", nth);
				name_commit(p, strdup(newname), 0);
				i++;
				name_first_parent_chain(p);
			}
		}
	} while (i);
}

static int mark_seen(struct commit *commit, struct commit_list **seen_p)
{
	if (!commit->object.flags) {
		insert_by_date(commit, seen_p);
		return 1;
	}
	return 0;
}

static void join_revs(struct commit_list **list_p,
		      struct commit_list **seen_p,
		      int num_rev, int extra)
{
	int all_mask = ((1u << (REV_SHIFT + num_rev)) - 1);
	int all_revs = all_mask & ~((1u << REV_SHIFT) - 1);

	while (*list_p) {
		struct commit_list *parents;
		int still_interesting = !!interesting(*list_p);
		struct commit *commit = pop_one_commit(list_p);
		int flags = commit->object.flags & all_mask;

		if (!still_interesting && extra <= 0)
			break;

		mark_seen(commit, seen_p);
		if ((flags & all_revs) == all_revs)
			flags |= UNINTERESTING;
		parents = commit->parents;

		while (parents) {
			struct commit *p = parents->item;
			int this_flag = p->object.flags;
			parents = parents->next;
			if ((this_flag & flags) == flags)
				continue;
			if (!p->object.parsed)
				parse_commit(p);
			if (mark_seen(p, seen_p) && !still_interesting)
				extra--;
			p->object.flags |= flags;
			insert_by_date(p, list_p);
		}
	}

	/*
	 * Postprocess to complete well-poisoning.
	 *
	 * At this point we have all the commits we have seen in
	 * seen_p list (which happens to be sorted chronologically but
	 * it does not really matter).  Mark anything that can be
	 * reached from uninteresting commits not interesting.
	 */
	for (;;) {
		int changed = 0;
		struct commit_list *s;
		for (s = *seen_p; s; s = s->next) {
			struct commit *c = s->item;
			struct commit_list *parents;

			if (((c->object.flags & all_revs) != all_revs) &&
			    !(c->object.flags & UNINTERESTING))
				continue;

			/* The current commit is either a merge base or
			 * already uninteresting one.  Mark its parents
			 * as uninteresting commits _only_ if they are
			 * already parsed.  No reason to find new ones
			 * here.
			 */
			parents = c->parents;
			while (parents) {
				struct commit *p = parents->item;
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

static void show_one_commit(struct commit *commit, int no_name)
{
	char pretty[256], *cp;
	struct commit_name *name = commit->object.util;
	if (commit->object.parsed)
		pretty_print_commit(CMIT_FMT_ONELINE, commit, ~0,
				    pretty, sizeof(pretty), 0, NULL, NULL);
	else
		strcpy(pretty, "(unavailable)");
	if (!strncmp(pretty, "[PATCH] ", 8))
		cp = pretty + 8;
	else
		cp = pretty;

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
			       find_unique_abbrev(commit->object.sha1, 7));
	}
	puts(cp);
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
	qsort(ref_name + bottom, top - bottom, sizeof(ref_name[0]),
	      compare_ref_name);
}

static int append_ref(const char *refname, const unsigned char *sha1)
{
	struct commit *commit = lookup_commit_reference_gently(sha1, 1);
	int i;

	if (!commit)
		return 0;
	/* Avoid adding the same thing twice */
	for (i = 0; i < ref_name_cnt; i++)
		if (!strcmp(refname, ref_name[i]))
			return 0;

	if (MAX_REVS <= ref_name_cnt) {
		fprintf(stderr, "warning: ignoring %s; "
			"cannot handle more than %d refs\n",
			refname, MAX_REVS);
		return 0;
	}
	ref_name[ref_name_cnt++] = strdup(refname);
	ref_name[ref_name_cnt] = NULL;
	return 0;
}

static int append_head_ref(const char *refname, const unsigned char *sha1)
{
	unsigned char tmp[20];
	int ofs = 11;
	if (strncmp(refname, "refs/heads/", ofs))
		return 0;
	/* If both heads/foo and tags/foo exists, get_sha1 would
	 * get confused.
	 */
	if (get_sha1(refname + ofs, tmp) || memcmp(tmp, sha1, 20))
		ofs = 5;
	return append_ref(refname + ofs, sha1);
}

static int append_tag_ref(const char *refname, const unsigned char *sha1)
{
	if (strncmp(refname, "refs/tags/", 10))
		return 0;
	return append_ref(refname + 5, sha1);
}

static const char *match_ref_pattern = NULL;
static int match_ref_slash = 0;
static int count_slash(const char *s)
{
	int cnt = 0;
	while (*s)
		if (*s++ == '/')
			cnt++;
	return cnt;
}

static int append_matching_ref(const char *refname, const unsigned char *sha1)
{
	/* we want to allow pattern hold/<asterisk> to show all
	 * branches under refs/heads/hold/, and v0.99.9? to show
	 * refs/tags/v0.99.9a and friends.
	 */
	const char *tail;
	int slash = count_slash(refname);
	for (tail = refname; *tail && match_ref_slash < slash; )
		if (*tail++ == '/')
			slash--;
	if (!*tail)
		return 0;
	if (fnmatch(match_ref_pattern, tail, 0))
		return 0;
	if (!strncmp("refs/heads/", refname, 11))
		return append_head_ref(refname, sha1);
	if (!strncmp("refs/tags/", refname, 10))
		return append_tag_ref(refname, sha1);
	return append_ref(refname, sha1);
}

static void snarf_refs(int head, int tag)
{
	if (head) {
		int orig_cnt = ref_name_cnt;
		for_each_ref(append_head_ref);
		sort_ref_range(orig_cnt, ref_name_cnt);
	}
	if (tag) {
		int orig_cnt = ref_name_cnt;
		for_each_ref(append_tag_ref);
		sort_ref_range(orig_cnt, ref_name_cnt);
	}
}

static int rev_is_head(char *head_path, int headlen, char *name,
		       unsigned char *head_sha1, unsigned char *sha1)
{
	int namelen;
	if ((!head_path[0]) ||
	    (head_sha1 && sha1 && memcmp(head_sha1, sha1, 20)))
		return 0;
	namelen = strlen(name);
	if ((headlen < namelen) ||
	    memcmp(head_path + headlen - namelen, name, namelen))
		return 0;
	if (headlen == namelen ||
	    head_path[headlen - namelen - 1] == '/')
		return 1;
	return 0;
}

static int show_merge_base(struct commit_list *seen, int num_rev)
{
	int all_mask = ((1u << (REV_SHIFT + num_rev)) - 1);
	int all_revs = all_mask & ~((1u << REV_SHIFT) - 1);
	int exit_status = 1;

	while (seen) {
		struct commit *commit = pop_one_commit(&seen);
		int flags = commit->object.flags & all_mask;
		if (!(flags & UNINTERESTING) &&
		    ((flags & all_revs) == all_revs)) {
			puts(sha1_to_hex(commit->object.sha1));
			exit_status = 0;
			commit->object.flags |= UNINTERESTING;
		}
	}
	return exit_status;
}

static int show_independent(struct commit **rev,
			    int num_rev,
			    char **ref_name,
			    unsigned int *rev_mask)
{
	int i;

	for (i = 0; i < num_rev; i++) {
		struct commit *commit = rev[i];
		unsigned int flag = rev_mask[i];

		if (commit->object.flags == flag)
			puts(sha1_to_hex(commit->object.sha1));
		commit->object.flags |= UNINTERESTING;
	}
	return 0;
}

static void append_one_rev(const char *av)
{
	unsigned char revkey[20];
	if (!get_sha1(av, revkey)) {
		append_ref(av, revkey);
		return;
	}
	if (strchr(av, '*') || strchr(av, '?') || strchr(av, '[')) {
		/* glob style match */
		int saved_matches = ref_name_cnt;
		match_ref_pattern = av;
		match_ref_slash = count_slash(av);
		for_each_ref(append_matching_ref);
		if (saved_matches == ref_name_cnt &&
		    ref_name_cnt < MAX_REVS)
			error("no matching refs with %s", av);
		if (saved_matches + 1 < ref_name_cnt)
			sort_ref_range(saved_matches, ref_name_cnt);
		return;
	}
	die("bad sha1 reference %s", av);
}

static int git_show_branch_config(const char *var, const char *value)
{
	if (!strcmp(var, "showbranch.default")) {
		if (default_alloc <= default_num + 1) {
			default_alloc = default_alloc * 3 / 2 + 20;
			default_arg = xrealloc(default_arg, sizeof *default_arg * default_alloc);
		}
		default_arg[default_num++] = strdup(value);
		default_arg[default_num] = NULL;
		return 0;
	}

	return git_default_config(var, value);
}

static int omit_in_dense(struct commit *commit, struct commit **rev, int n)
{
	/* If the commit is tip of the named branches, do not
	 * omit it.
	 * Otherwise, if it is a merge that is reachable from only one
	 * tip, it is not that interesting.
	 */
	int i, flag, count;
	for (i = 0; i < n; i++)
		if (rev[i] == commit)
			return 0;
	flag = commit->object.flags;
	for (i = count = 0; i < n; i++) {
		if (flag & (1u << (i + REV_SHIFT)))
			count++;
	}
	if (count == 1)
		return 1;
	return 0;
}

int main(int ac, char **av)
{
	struct commit *rev[MAX_REVS], *commit;
	struct commit_list *list = NULL, *seen = NULL;
	unsigned int rev_mask[MAX_REVS];
	int num_rev, i, extra = 0;
	int all_heads = 0, all_tags = 0;
	int all_mask, all_revs;
	int lifo = 1;
	char head_path[128];
	const char *head_path_p;
	int head_path_len;
	unsigned char head_sha1[20];
	int merge_base = 0;
	int independent = 0;
	int no_name = 0;
	int sha1_name = 0;
	int shown_merge_point = 0;
	int with_current_branch = 0;
	int head_at = -1;
	int topics = 0;
	int dense = 1;

	setup_git_directory();
	git_config(git_show_branch_config);

	/* If nothing is specified, try the default first */
	if (ac == 1 && default_num) {
		ac = default_num + 1;
		av = default_arg - 1; /* ick; we would not address av[0] */
	}

	while (1 < ac && av[1][0] == '-') {
		char *arg = av[1];
		if (!strcmp(arg, "--")) {
			ac--; av++;
			break;
		}
		else if (!strcmp(arg, "--all"))
			all_heads = all_tags = 1;
		else if (!strcmp(arg, "--heads"))
			all_heads = 1;
		else if (!strcmp(arg, "--tags"))
			all_tags = 1;
		else if (!strcmp(arg, "--more"))
			extra = 1;
		else if (!strcmp(arg, "--list"))
			extra = -1;
		else if (!strcmp(arg, "--no-name"))
			no_name = 1;
		else if (!strcmp(arg, "--current"))
			with_current_branch = 1;
		else if (!strcmp(arg, "--sha1-name"))
			sha1_name = 1;
		else if (!strncmp(arg, "--more=", 7))
			extra = atoi(arg + 7);
		else if (!strcmp(arg, "--merge-base"))
			merge_base = 1;
		else if (!strcmp(arg, "--independent"))
			independent = 1;
		else if (!strcmp(arg, "--topo-order"))
			lifo = 1;
		else if (!strcmp(arg, "--topics"))
			topics = 1;
		else if (!strcmp(arg, "--sparse"))
			dense = 0;
		else if (!strcmp(arg, "--date-order"))
			lifo = 0;
		else
			usage(show_branch_usage);
		ac--; av++;
	}
	ac--; av++;

	/* Only one of these is allowed */
	if (1 < independent + merge_base + (extra != 0))
		usage(show_branch_usage);

	/* If nothing is specified, show all branches by default */
	if (ac + all_heads + all_tags == 0)
		all_heads = 1;

	if (all_heads + all_tags)
		snarf_refs(all_heads, all_tags);
	while (0 < ac) {
		append_one_rev(*av);
		ac--; av++;
	}

	head_path_p = resolve_ref(git_path("HEAD"), head_sha1, 1);
	if (head_path_p) {
		head_path_len = strlen(head_path_p);
		memcpy(head_path, head_path_p, head_path_len + 1);
	}
	else {
		head_path_len = 0;
		head_path[0] = 0;
	}

	if (with_current_branch && head_path_p) {
		int has_head = 0;
		for (i = 0; !has_head && i < ref_name_cnt; i++) {
			/* We are only interested in adding the branch
			 * HEAD points at.
			 */
			if (rev_is_head(head_path,
					head_path_len,
					ref_name[i],
					head_sha1, NULL))
				has_head++;
		}
		if (!has_head) {
			int pfxlen = strlen(git_path("refs/heads/"));
			append_one_rev(head_path + pfxlen);
		}
	}

	if (!ref_name_cnt) {
		fprintf(stderr, "No revs to be shown.\n");
		exit(0);
	}

	for (num_rev = 0; ref_name[num_rev]; num_rev++) {
		unsigned char revkey[20];
		unsigned int flag = 1u << (num_rev + REV_SHIFT);

		if (MAX_REVS <= num_rev)
			die("cannot handle more than %d revs.", MAX_REVS);
		if (get_sha1(ref_name[num_rev], revkey))
			die("'%s' is not a valid ref.", ref_name[num_rev]);
		commit = lookup_commit_reference(revkey);
		if (!commit)
			die("cannot find commit %s (%s)",
			    ref_name[num_rev], revkey);
		parse_commit(commit);
		mark_seen(commit, &seen);

		/* rev#0 uses bit REV_SHIFT, rev#1 uses bit REV_SHIFT+1,
		 * and so on.  REV_SHIFT bits from bit 0 are used for
		 * internal bookkeeping.
		 */
		commit->object.flags |= flag;
		if (commit->object.flags == flag)
			insert_by_date(commit, &list);
		rev[num_rev] = commit;
	}
	for (i = 0; i < num_rev; i++)
		rev_mask[i] = rev[i]->object.flags;

	if (0 <= extra)
		join_revs(&list, &seen, num_rev, extra);

	if (merge_base)
		return show_merge_base(seen, num_rev);

	if (independent)
		return show_independent(rev, num_rev, ref_name, rev_mask);

	/* Show list; --more=-1 means list-only */
	if (1 < num_rev || extra < 0) {
		for (i = 0; i < num_rev; i++) {
			int j;
			int is_head = rev_is_head(head_path,
						  head_path_len,
						  ref_name[i],
						  head_sha1,
						  rev[i]->object.sha1);
			if (extra < 0)
				printf("%c [%s] ",
				       is_head ? '*' : ' ', ref_name[i]);
			else {
				for (j = 0; j < i; j++)
					putchar(' ');
				printf("%c [%s] ",
				       is_head ? '*' : '!', ref_name[i]);
			}
			/* header lines never need name */
			show_one_commit(rev[i], 1);
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
	sort_in_topological_order(&seen, lifo);

	/* Give names to commits */
	if (!sha1_name && !no_name)
		name_commits(seen, rev, ref_name, num_rev);

	all_mask = ((1u << (REV_SHIFT + num_rev)) - 1);
	all_revs = all_mask & ~((1u << REV_SHIFT) - 1);

	while (seen) {
		struct commit *commit = pop_one_commit(&seen);
		int this_flag = commit->object.flags;
		int is_merge_point = ((this_flag & all_revs) == all_revs);

		shown_merge_point |= is_merge_point;

		if (1 < num_rev) {
			int is_merge = !!(commit->parents &&
					  commit->parents->next);
			if (topics &&
			    !is_merge_point &&
			    (this_flag & (1u << REV_SHIFT)))
				continue;
			if (dense && is_merge &&
			    omit_in_dense(commit, rev, num_rev))
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
				putchar(mark);
			}
			putchar(' ');
		}
		show_one_commit(commit, no_name);

		if (shown_merge_point && --extra < 0)
			break;
	}
	return 0;
}
