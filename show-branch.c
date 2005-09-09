#include <stdlib.h>
#include "cache.h"
#include "commit.h"
#include "refs.h"

static const char show_branch_usage[] =
"git-show-branch [--all] [--heads] [--tags] [--more=count | --list | --independent | --merge-base ] [<refs>...]";

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
 * commit is nth generation ancestore of, if that generation
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
				char newname[1000];
				parents = parents->next;
				nth++;
				if (p->object.util)
					continue;
				sprintf(newname, "%s^%d", n->head_name, nth);
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
		struct commit *commit = pop_one_commit(list_p);
		int flags = commit->object.flags & all_mask;
		int still_interesting = !!interesting(*list_p);

		if (!still_interesting && extra < 0)
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
			parse_commit(p);
			if (mark_seen(p, seen_p) && !still_interesting)
				extra--;
			p->object.flags |= flags;
			insert_by_date(p, list_p);
		}
	}
}

static void show_one_commit(struct commit *commit)
{
	char pretty[128], *cp;
	struct commit_name *name = commit->object.util;
	pretty_print_commit(CMIT_FMT_ONELINE, commit->buffer, ~0,
			    pretty, sizeof(pretty));
	if (!strncmp(pretty, "[PATCH] ", 8))
		cp = pretty + 8;
	else
		cp = pretty;
	if (name && name->head_name) {
		printf("[%s", name->head_name);
		if (name->generation)
			printf("~%d", name->generation);
		printf("] ");
	}
	puts(cp);
}

static char *ref_name[MAX_REVS + 1];
static int ref_name_cnt;

static int compare_ref_name(const void *a_, const void *b_)
{
	const char * const*a = a_, * const*b = b_;
	return strcmp(*a, *b);
}

static void sort_ref_range(int bottom, int top)
{
	qsort(ref_name + bottom, top - bottom, sizeof(ref_name[0]),
	      compare_ref_name);
}

static int append_ref(const char *refname, const unsigned char *sha1)
{
	struct commit *commit = lookup_commit_reference_gently(sha1, 1);
	if (!commit)
		return 0;
	if (MAX_REVS < ref_name_cnt) {
		fprintf(stderr, "warning: ignoring %s; "
			"cannot handle more than %d refs",
			refname, MAX_REVS);
		return 0;
	}
	ref_name[ref_name_cnt++] = strdup(refname);
	ref_name[ref_name_cnt] = NULL;
	return 0;
}

static int append_head_ref(const char *refname, const unsigned char *sha1)
{
	if (strncmp(refname, "refs/heads/", 11))
		return 0;
	return append_ref(refname + 11, sha1);
}

static int append_tag_ref(const char *refname, const unsigned char *sha1)
{
	if (strncmp(refname, "refs/tags/", 10))
		return 0;
	return append_ref(refname + 5, sha1);
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

static int rev_is_head(char *head_path, int headlen,
		       char *name,
		       unsigned char *head_sha1, unsigned char *sha1)
{
	int namelen;
	if ((!head_path[0]) || memcmp(head_sha1, sha1, 20))
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

int main(int ac, char **av)
{
	struct commit *rev[MAX_REVS], *commit;
	struct commit_list *list = NULL, *seen = NULL;
	unsigned int rev_mask[MAX_REVS];
	int num_rev, i, extra = 0;
	int all_heads = 0, all_tags = 0;
	int all_mask, all_revs, shown_merge_point;
	char head_path[128];
	int head_path_len;
	unsigned char head_sha1[20];
	int merge_base = 0;
	int independent = 0;
	char **label;

	setup_git_directory();

	while (1 < ac && av[1][0] == '-') {
		char *arg = av[1];
		if (!strcmp(arg, "--all"))
			all_heads = all_tags = 1;
		else if (!strcmp(arg, "--heads"))
			all_heads = 1;
		else if (!strcmp(arg, "--tags"))
			all_tags = 1;
		else if (!strcmp(arg, "--more"))
			extra = 1;
		else if (!strcmp(arg, "--list"))
			extra = -1;
		else if (!strncmp(arg, "--more=", 7))
			extra = atoi(arg + 7);
		else if (!strcmp(arg, "--merge-base"))
			merge_base = 1;
		else if (!strcmp(arg, "--independent"))
			independent = 1;
		else
			usage(show_branch_usage);
		ac--; av++;
	}
	ac--; av++;

	/* Only one of these is allowed */
	if (1 < independent + merge_base + (extra != 0))
		usage(show_branch_usage);

	if (all_heads + all_tags)
		snarf_refs(all_heads, all_tags);

	while (0 < ac) {
		unsigned char revkey[20];
		if (get_sha1(*av, revkey))
			die("bad sha1 reference %s", *av);
		append_ref(*av, revkey);
		ac--; av++;
	}

	/* If still no revs, then add heads */
	if (!ref_name_cnt)
		snarf_refs(1, 0);

	for (num_rev = 0; ref_name[num_rev]; num_rev++) {
		unsigned char revkey[20];
		unsigned int flag = 1u << (num_rev + REV_SHIFT);

		if (MAX_REVS <= num_rev)
			die("cannot handle more than %d revs.", MAX_REVS);
		if (get_sha1(ref_name[num_rev], revkey))
			usage(show_branch_usage);
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

	head_path_len = readlink(".git/HEAD", head_path, sizeof(head_path)-1);
	if ((head_path_len < 0) || get_sha1("HEAD", head_sha1))
		head_path[0] = 0;
	else
		head_path[head_path_len] = 0;

	if (merge_base)
		return show_merge_base(seen, num_rev);

	if (independent)
		return show_independent(rev, num_rev, ref_name, rev_mask);

	/* Show list; --more=-1 means list-only */
	if (1 < num_rev) {
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
			show_one_commit(rev[i]);
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
	sort_in_topological_order(&seen);

	/* Give names to commits */
	name_commits(seen, rev, ref_name, num_rev);

	all_mask = ((1u << (REV_SHIFT + num_rev)) - 1);
	all_revs = all_mask & ~((1u << REV_SHIFT) - 1);
	shown_merge_point = 0;

	while (seen) {
		struct commit *commit = pop_one_commit(&seen);
		int this_flag = commit->object.flags;
		int is_merge_point = (this_flag & all_revs) == all_revs;
		static char *obvious[] = { "" };

		if (is_merge_point)
			shown_merge_point = 1;

		if (1 < num_rev) {
			for (i = 0; i < num_rev; i++)
				putchar((this_flag & (1u << (i + REV_SHIFT)))
					? '+' : ' ');
			putchar(' ');
		}
		show_one_commit(commit);
		if (num_rev == 1)
			label = obvious;
		if (shown_merge_point && is_merge_point)
			if (--extra < 0)
				break;
	}
	return 0;
}
