#include <stdlib.h>
#include "cache.h"
#include "commit.h"
#include "refs.h"

static const char show_branch_usage[] =
"git-show-branch [--all] [--heads] [--tags] [--more=count] [<refs>...]";

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
	int head_rev;	/* which head's ancestor? */
	int generation;	/* how many parents away from head_rev */
};

/* Name the commit as nth generation ancestor of head_rev;
 * we count only the first-parent relationship for naming purposes.
 */
static void name_commit(struct commit *commit, int head_rev, int nth)
{
	struct commit_name *name;
	if (!commit->object.util)
		commit->object.util = xmalloc(sizeof(struct commit_name));
	name = commit->object.util;
	name->head_rev = head_rev;
	name->generation = nth;
}

/* Parent is the first parent of the commit.  We may name it
 * as (n+1)th generation ancestor of the same head_rev as
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
		name_commit(parent, commit_name->head_rev,
			    commit_name->generation + 1);
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
		int nth_parent = 0;
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
			nth_parent++;
			if (nth_parent == 1)
				name_parent(commit, p);

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

static void show_one_commit(struct commit *commit, char **head_name)
{
	char pretty[128], *cp;
	struct commit_name *name = commit->object.util;
	pretty_print_commit(CMIT_FMT_ONELINE, commit->buffer, ~0,
			    pretty, sizeof(pretty));
	if (!strncmp(pretty, "[PATCH] ", 8))
		cp = pretty + 8;
	else
		cp = pretty;
	if (name && head_name) {
		printf("[%s", head_name[name->head_rev]);
		if (name->generation)
			printf("~%d", name->generation);
		printf("] ");
	}
	puts(cp);
}

static char *ref_name[MAX_REVS + 1];
static int ref_name_cnt;

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
	return append_ref(refname + 5, sha1);
}

static int append_tag_ref(const char *refname, const unsigned char *sha1)
{
	if (strncmp(refname, "refs/tags/", 10))
		return 0;
	return append_ref(refname + 5, sha1);
}

static void snarf_refs(int head, int tag)
{
	if (head)
		for_each_ref(append_head_ref);
	if (tag)
		for_each_ref(append_tag_ref);
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

	while (seen) {
		struct commit *commit = pop_one_commit(&seen);
		int flags = commit->object.flags & all_mask;
		if (!(flags & UNINTERESTING) &&
		    ((flags & all_revs) == all_revs)) {
			puts(sha1_to_hex(commit->object.sha1));
			return 0;
		}
	}
	return 1;
}

int main(int ac, char **av)
{
	struct commit *rev[MAX_REVS], *commit;
	struct commit_list *list = NULL, *seen = NULL;
	int num_rev, i, extra = 0;
	int all_heads = 0, all_tags = 0;
	char head_path[128];
	int head_path_len;
	unsigned char head_sha1[20];
	int merge_base = 0;
	char **label;

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
		else if (!strncmp(arg, "--more=", 7)) {
			extra = atoi(arg + 7);
			if (extra < 0)
				usage(show_branch_usage);
		}
		else if (!strcmp(arg, "--merge-base"))
			merge_base = 1;
		else
			usage(show_branch_usage);
		ac--; av++;
	}
	ac--; av++;

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

		if (MAX_REVS <= num_rev)
			die("cannot handle more than %d revs.", MAX_REVS);
		if (get_sha1(ref_name[num_rev], revkey))
			usage(show_branch_usage);
		commit = lookup_commit_reference(revkey);
		if (!commit)
			die("cannot find commit %s (%s)",
			    ref_name[num_rev], revkey);
		parse_commit(commit);
		if (!commit->object.util)
			name_commit(commit, num_rev, 0);
		mark_seen(commit, &seen);

		/* rev#0 uses bit REV_SHIFT, rev#1 uses bit REV_SHIFT+1,
		 * and so on.  REV_SHIFT bits from bit 0 are used for
		 * internal bookkeeping.
		 */
		commit->object.flags |= 1u << (num_rev + REV_SHIFT);
		insert_by_date(commit, &list);
		rev[num_rev] = commit;
	}
	join_revs(&list, &seen, num_rev, extra);

	head_path_len = readlink(".git/HEAD", head_path, sizeof(head_path)-1);
	if ((head_path_len < 0) || get_sha1("HEAD", head_sha1))
		head_path[0] = 0;
	else
		head_path[head_path_len] = 0;

	if (merge_base)
		return show_merge_base(seen, num_rev);

	/* Show list */
	if (1 < num_rev) {
		for (i = 0; i < num_rev; i++) {
			int j;
			int is_head = rev_is_head(head_path,
						  head_path_len,
						  ref_name[i],
						  head_sha1,
						  rev[i]->object.sha1);
			for (j = 0; j < i; j++)
				putchar(' ');
			printf("%c [%s] ", is_head ? '*' : '!', ref_name[i]);
			show_one_commit(rev[i], NULL);
		}
		for (i = 0; i < num_rev; i++)
			putchar('-');
		putchar('\n');
	}

	label = ref_name;
	while (seen) {
		struct commit *commit = pop_one_commit(&seen);
		int this_flag = commit->object.flags;
		static char *obvious[] = { "" };

		if ((this_flag & UNINTERESTING) && (--extra < 0))
			break;
		if (1 < num_rev) {
			for (i = 0; i < num_rev; i++)
				putchar((this_flag & (1u << (i + REV_SHIFT)))
					? '+' : ' ');
			putchar(' ');
		}
		show_one_commit(commit, label);
		if (num_rev == 1)
			label = obvious;
	}
	return 0;
}
