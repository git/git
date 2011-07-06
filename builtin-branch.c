/*
 * Builtin "git branch"
 *
 * Copyright (c) 2006 Kristian Høgsberg <krh@redhat.com>
 * Based on git-branch.sh by Junio C Hamano.
 */

#include "cache.h"
#include "color.h"
#include "refs.h"
#include "commit.h"
#include "builtin.h"
#include "remote.h"
#include "parse-options.h"
#include "branch.h"
#include "diff.h"
#include "revision.h"

static const char * const builtin_branch_usage[] = {
	"git branch [options] [-r | -a] [--merged | --no-merged]",
	"git branch [options] [-l] [-f] <branchname> [<start-point>]",
	"git branch [options] [-r] (-d | -D) <branchname>",
	"git branch [options] (-m | -M) [<oldbranch>] <newbranch>",
	NULL
};

#define REF_LOCAL_BRANCH    0x01
#define REF_REMOTE_BRANCH   0x02

static const char *head;
static unsigned char head_sha1[20];

static int branch_use_color = -1;
static char branch_colors[][COLOR_MAXLEN] = {
	"\033[m",	/* reset */
	"",		/* PLAIN (normal) */
	"\033[31m",	/* REMOTE (red) */
	"",		/* LOCAL (normal) */
	"\033[32m",	/* CURRENT (green) */
};
enum color_branch {
	COLOR_BRANCH_RESET = 0,
	COLOR_BRANCH_PLAIN = 1,
	COLOR_BRANCH_REMOTE = 2,
	COLOR_BRANCH_LOCAL = 3,
	COLOR_BRANCH_CURRENT = 4,
};

static enum merge_filter {
	NO_FILTER = 0,
	SHOW_NOT_MERGED,
	SHOW_MERGED,
} merge_filter;
static unsigned char merge_filter_ref[20];

static int parse_branch_color_slot(const char *var, int ofs)
{
	if (!strcasecmp(var+ofs, "plain"))
		return COLOR_BRANCH_PLAIN;
	if (!strcasecmp(var+ofs, "reset"))
		return COLOR_BRANCH_RESET;
	if (!strcasecmp(var+ofs, "remote"))
		return COLOR_BRANCH_REMOTE;
	if (!strcasecmp(var+ofs, "local"))
		return COLOR_BRANCH_LOCAL;
	if (!strcasecmp(var+ofs, "current"))
		return COLOR_BRANCH_CURRENT;
	die("bad config variable '%s'", var);
}

static int git_branch_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "color.branch")) {
		branch_use_color = git_config_colorbool(var, value, -1);
		return 0;
	}
	if (!prefixcmp(var, "color.branch.")) {
		int slot = parse_branch_color_slot(var, 13);
		if (!value)
			return config_error_nonbool(var);
		color_parse(value, var, branch_colors[slot]);
		return 0;
	}
	return git_color_default_config(var, value, cb);
}

static const char *branch_get_color(enum color_branch ix)
{
	if (branch_use_color > 0)
		return branch_colors[ix];
	return "";
}

static int delete_branches(int argc, const char **argv, int force, int kinds)
{
	struct commit *rev, *head_rev = head_rev;
	unsigned char sha1[20];
	char *name = NULL;
	const char *fmt, *remote;
	char section[PATH_MAX];
	int i;
	int ret = 0;

	switch (kinds) {
	case REF_REMOTE_BRANCH:
		fmt = "refs/remotes/%s";
		remote = "remote ";
		force = 1;
		break;
	case REF_LOCAL_BRANCH:
		fmt = "refs/heads/%s";
		remote = "";
		break;
	default:
		die("cannot use -a with -d");
	}

	if (!force) {
		head_rev = lookup_commit_reference(head_sha1);
		if (!head_rev)
			die("Couldn't look up commit object for HEAD");
	}
	for (i = 0; i < argc; i++) {
		if (kinds == REF_LOCAL_BRANCH && !strcmp(head, argv[i])) {
			error("Cannot delete the branch '%s' "
				"which you are currently on.", argv[i]);
			ret = 1;
			continue;
		}

		free(name);

		name = xstrdup(mkpath(fmt, argv[i]));
		if (!resolve_ref(name, sha1, 1, NULL)) {
			error("%sbranch '%s' not found.",
					remote, argv[i]);
			ret = 1;
			continue;
		}

		rev = lookup_commit_reference(sha1);
		if (!rev) {
			error("Couldn't look up commit object for '%s'", name);
			ret = 1;
			continue;
		}

		/* This checks whether the merge bases of branch and
		 * HEAD contains branch -- which means that the HEAD
		 * contains everything in both.
		 */

		if (!force &&
		    !in_merge_bases(rev, &head_rev, 1)) {
			error("The branch '%s' is not an ancestor of "
				"your current HEAD.\n"
				"If you are sure you want to delete it, "
				"run 'git branch -D %s'.", argv[i], argv[i]);
			ret = 1;
			continue;
		}

		if (delete_ref(name, sha1)) {
			error("Error deleting %sbranch '%s'", remote,
			       argv[i]);
			ret = 1;
		} else {
			printf("Deleted %sbranch %s.\n", remote, argv[i]);
			snprintf(section, sizeof(section), "branch.%s",
				 argv[i]);
			if (git_config_rename_section(section, NULL) < 0)
				warning("Update of config-file failed");
		}
	}

	free(name);

	return(ret);
}

struct ref_item {
	char *name;
	unsigned int kind;
	struct commit *commit;
};

struct ref_list {
	struct rev_info revs;
	int index, alloc, maxwidth;
	struct ref_item *list;
	struct commit_list *with_commit;
	int kinds;
};

static int has_commit(struct commit *commit, struct commit_list *with_commit)
{
	if (!with_commit)
		return 1;
	while (with_commit) {
		struct commit *other;

		other = with_commit->item;
		with_commit = with_commit->next;
		if (in_merge_bases(other, &commit, 1))
			return 1;
	}
	return 0;
}

static int append_ref(const char *refname, const unsigned char *sha1, int flags, void *cb_data)
{
	struct ref_list *ref_list = (struct ref_list*)(cb_data);
	struct ref_item *newitem;
	struct commit *commit;
	int kind;
	int len;

	/* Detect kind */
	if (!prefixcmp(refname, "refs/heads/")) {
		kind = REF_LOCAL_BRANCH;
		refname += 11;
	} else if (!prefixcmp(refname, "refs/remotes/")) {
		kind = REF_REMOTE_BRANCH;
		refname += 13;
	} else
		return 0;

	commit = lookup_commit_reference_gently(sha1, 1);
	if (!commit)
		return error("branch '%s' does not point at a commit", refname);

	/* Filter with with_commit if specified */
	if (!has_commit(commit, ref_list->with_commit))
		return 0;

	/* Don't add types the caller doesn't want */
	if ((kind & ref_list->kinds) == 0)
		return 0;

	if (merge_filter != NO_FILTER)
		add_pending_object(&ref_list->revs,
				   (struct object *)commit, refname);

	/* Resize buffer */
	if (ref_list->index >= ref_list->alloc) {
		ref_list->alloc = alloc_nr(ref_list->alloc);
		ref_list->list = xrealloc(ref_list->list,
				ref_list->alloc * sizeof(struct ref_item));
	}

	/* Record the new item */
	newitem = &(ref_list->list[ref_list->index++]);
	newitem->name = xstrdup(refname);
	newitem->kind = kind;
	newitem->commit = commit;
	len = strlen(newitem->name);
	if (len > ref_list->maxwidth)
		ref_list->maxwidth = len;

	return 0;
}

static void free_ref_list(struct ref_list *ref_list)
{
	int i;

	for (i = 0; i < ref_list->index; i++)
		free(ref_list->list[i].name);
	free(ref_list->list);
}

static int ref_cmp(const void *r1, const void *r2)
{
	struct ref_item *c1 = (struct ref_item *)(r1);
	struct ref_item *c2 = (struct ref_item *)(r2);

	if (c1->kind != c2->kind)
		return c1->kind - c2->kind;
	return strcmp(c1->name, c2->name);
}

static void fill_tracking_info(char *stat, const char *branch_name)
{
	int ours, theirs;
	struct branch *branch = branch_get(branch_name);

	if (!stat_tracking_info(branch, &ours, &theirs) || (!ours && !theirs))
		return;
	if (!ours)
		sprintf(stat, "[behind %d] ", theirs);
	else if (!theirs)
		sprintf(stat, "[ahead %d] ", ours);
	else
		sprintf(stat, "[ahead %d, behind %d] ", ours, theirs);
}

static int matches_merge_filter(struct commit *commit)
{
	int is_merged;

	if (merge_filter == NO_FILTER)
		return 1;

	is_merged = !!(commit->object.flags & UNINTERESTING);
	return (is_merged == (merge_filter == SHOW_MERGED));
}

static void print_ref_item(struct ref_item *item, int maxwidth, int verbose,
			   int abbrev, int current)
{
	char c;
	int color;
	struct commit *commit = item->commit;

	if (!matches_merge_filter(commit))
		return;

	switch (item->kind) {
	case REF_LOCAL_BRANCH:
		color = COLOR_BRANCH_LOCAL;
		break;
	case REF_REMOTE_BRANCH:
		color = COLOR_BRANCH_REMOTE;
		break;
	default:
		color = COLOR_BRANCH_PLAIN;
		break;
	}

	c = ' ';
	if (current) {
		c = '*';
		color = COLOR_BRANCH_CURRENT;
	}

	if (verbose) {
		struct strbuf subject;
		const char *sub = " **** invalid ref ****";
		char stat[128];

		strbuf_init(&subject, 0);
		stat[0] = '\0';

		commit = item->commit;
		if (commit && !parse_commit(commit)) {
			pretty_print_commit(CMIT_FMT_ONELINE, commit,
					    &subject, 0, NULL, NULL, 0, 0);
			sub = subject.buf;
		}

		if (item->kind == REF_LOCAL_BRANCH)
			fill_tracking_info(stat, item->name);

		printf("%c %s%-*s%s %s %s%s\n", c, branch_get_color(color),
		       maxwidth, item->name,
		       branch_get_color(COLOR_BRANCH_RESET),
		       find_unique_abbrev(item->commit->object.sha1, abbrev),
		       stat, sub);
		strbuf_release(&subject);
	} else {
		printf("%c %s%s%s\n", c, branch_get_color(color), item->name,
		       branch_get_color(COLOR_BRANCH_RESET));
	}
}

static int calc_maxwidth(struct ref_list *refs)
{
	int i, l, w = 0;
	for (i = 0; i < refs->index; i++) {
		if (!matches_merge_filter(refs->list[i].commit))
			continue;
		l = strlen(refs->list[i].name);
		if (l > w)
			w = l;
	}
	return w;
}

static void print_ref_list(int kinds, int detached, int verbose, int abbrev, struct commit_list *with_commit)
{
	int i;
	struct ref_list ref_list;
	struct commit *head_commit = lookup_commit_reference_gently(head_sha1, 1);

	memset(&ref_list, 0, sizeof(ref_list));
	ref_list.kinds = kinds;
	ref_list.with_commit = with_commit;
	if (merge_filter != NO_FILTER)
		init_revisions(&ref_list.revs, NULL);
	for_each_ref(append_ref, &ref_list);
	if (merge_filter != NO_FILTER) {
		struct commit *filter;
		filter = lookup_commit_reference_gently(merge_filter_ref, 0);
		filter->object.flags |= UNINTERESTING;
		add_pending_object(&ref_list.revs,
				   (struct object *) filter, "");
		ref_list.revs.limited = 1;
		prepare_revision_walk(&ref_list.revs);
		if (verbose)
			ref_list.maxwidth = calc_maxwidth(&ref_list);
	}

	qsort(ref_list.list, ref_list.index, sizeof(struct ref_item), ref_cmp);

	detached = (detached && (kinds & REF_LOCAL_BRANCH));
	if (detached && head_commit && has_commit(head_commit, with_commit)) {
		struct ref_item item;
		item.name = xstrdup("(no branch)");
		item.kind = REF_LOCAL_BRANCH;
		item.commit = head_commit;
		if (strlen(item.name) > ref_list.maxwidth)
			ref_list.maxwidth = strlen(item.name);
		print_ref_item(&item, ref_list.maxwidth, verbose, abbrev, 1);
		free(item.name);
	}

	for (i = 0; i < ref_list.index; i++) {
		int current = !detached &&
			(ref_list.list[i].kind == REF_LOCAL_BRANCH) &&
			!strcmp(ref_list.list[i].name, head);
		print_ref_item(&ref_list.list[i], ref_list.maxwidth, verbose,
			       abbrev, current);
	}

	free_ref_list(&ref_list);
}

static void rename_branch(const char *oldname, const char *newname, int force)
{
	char oldref[PATH_MAX], newref[PATH_MAX], logmsg[PATH_MAX*2 + 100];
	unsigned char sha1[20];
	char oldsection[PATH_MAX], newsection[PATH_MAX];

	if (!oldname)
		die("cannot rename the current branch while not on any.");

	if (snprintf(oldref, sizeof(oldref), "refs/heads/%s", oldname) > sizeof(oldref))
		die("Old branchname too long");

	if (check_ref_format(oldref))
		die("Invalid branch name: %s", oldref);

	if (snprintf(newref, sizeof(newref), "refs/heads/%s", newname) > sizeof(newref))
		die("New branchname too long");

	if (check_ref_format(newref))
		die("Invalid branch name: %s", newref);

	if (resolve_ref(newref, sha1, 1, NULL) && !force)
		die("A branch named '%s' already exists.", newname);

	snprintf(logmsg, sizeof(logmsg), "Branch: renamed %s to %s",
		 oldref, newref);

	if (rename_ref(oldref, newref, logmsg))
		die("Branch rename failed");

	/* no need to pass logmsg here as HEAD didn't really move */
	if (!strcmp(oldname, head) && create_symref("HEAD", newref, NULL))
		die("Branch renamed to %s, but HEAD is not updated!", newname);

	snprintf(oldsection, sizeof(oldsection), "branch.%s", oldref + 11);
	snprintf(newsection, sizeof(newsection), "branch.%s", newref + 11);
	if (git_config_rename_section(oldsection, newsection) < 0)
		die("Branch is renamed, but update of config-file failed");
}

static int opt_parse_with_commit(const struct option *opt, const char *arg, int unset)
{
	unsigned char sha1[20];
	struct commit *commit;

	if (!arg)
		return -1;
	if (get_sha1(arg, sha1))
		die("malformed object name %s", arg);
	commit = lookup_commit_reference(sha1);
	if (!commit)
		die("no such commit %s", arg);
	commit_list_insert(commit, opt->value);
	return 0;
}

static int opt_parse_merge_filter(const struct option *opt, const char *arg, int unset)
{
	merge_filter = ((opt->long_name[0] == 'n')
			? SHOW_NOT_MERGED
			: SHOW_MERGED);
	if (unset)
		merge_filter = SHOW_NOT_MERGED; /* b/c for --no-merged */
	if (!arg)
		arg = "HEAD";
	if (get_sha1(arg, merge_filter_ref))
		die("malformed object name %s", arg);
	return 0;
}

int cmd_branch(int argc, const char **argv, const char *prefix)
{
	int delete = 0, rename = 0, force_create = 0;
	int verbose = 0, abbrev = DEFAULT_ABBREV, detached = 0;
	int reflog = 0;
	enum branch_track track;
	int kinds = REF_LOCAL_BRANCH;
	struct commit_list *with_commit = NULL;

	struct option options[] = {
		OPT_GROUP("Generic options"),
		OPT__VERBOSE(&verbose),
		OPT_SET_INT( 0 , "track",  &track, "set up tracking mode (see git-pull(1))",
			BRANCH_TRACK_EXPLICIT),
		OPT_BOOLEAN( 0 , "color",  &branch_use_color, "use colored output"),
		OPT_SET_INT('r', NULL,     &kinds, "act on remote-tracking branches",
			REF_REMOTE_BRANCH),
		{
			OPTION_CALLBACK, 0, "contains", &with_commit, "commit",
			"print only branches that contain the commit",
			PARSE_OPT_LASTARG_DEFAULT,
			opt_parse_with_commit, (intptr_t)"HEAD",
		},
		{
			OPTION_CALLBACK, 0, "with", &with_commit, "commit",
			"print only branches that contain the commit",
			PARSE_OPT_HIDDEN | PARSE_OPT_LASTARG_DEFAULT,
			opt_parse_with_commit, (intptr_t) "HEAD",
		},
		OPT__ABBREV(&abbrev),

		OPT_GROUP("Specific git-branch actions:"),
		OPT_SET_INT('a', NULL, &kinds, "list both remote-tracking and local branches",
			REF_REMOTE_BRANCH | REF_LOCAL_BRANCH),
		OPT_BIT('d', NULL, &delete, "delete fully merged branch", 1),
		OPT_BIT('D', NULL, &delete, "delete branch (even if not merged)", 2),
		OPT_BIT('m', NULL, &rename, "move/rename a branch and its reflog", 1),
		OPT_BIT('M', NULL, &rename, "move/rename a branch, even if target exists", 2),
		OPT_BOOLEAN('l', NULL, &reflog, "create the branch's reflog"),
		OPT_BOOLEAN('f', NULL, &force_create, "force creation (when already exists)"),
		{
			OPTION_CALLBACK, 0, "no-merged", &merge_filter_ref,
			"commit", "print only not merged branches",
			PARSE_OPT_LASTARG_DEFAULT | PARSE_OPT_NONEG,
			opt_parse_merge_filter, (intptr_t) "HEAD",
		},
		{
			OPTION_CALLBACK, 0, "merged", &merge_filter_ref,
			"commit", "print only merged branches",
			PARSE_OPT_LASTARG_DEFAULT | PARSE_OPT_NONEG,
			opt_parse_merge_filter, (intptr_t) "HEAD",
		},
		OPT_END(),
	};

	git_config(git_branch_config, NULL);

	if (branch_use_color == -1)
		branch_use_color = git_use_color_default;

	track = git_branch_track;

	head = resolve_ref("HEAD", head_sha1, 0, NULL);
	if (!head)
		die("Failed to resolve HEAD as a valid ref.");
	head = xstrdup(head);
	if (!strcmp(head, "HEAD")) {
		detached = 1;
	} else {
		if (prefixcmp(head, "refs/heads/"))
			die("HEAD not found below refs/heads!");
		head += 11;
	}
	hashcpy(merge_filter_ref, head_sha1);

	argc = parse_options(argc, argv, options, builtin_branch_usage, 0);
	if (!!delete + !!rename + !!force_create > 1)
		usage_with_options(builtin_branch_usage, options);

	if (delete)
		return delete_branches(argc, argv, delete > 1, kinds);
	else if (argc == 0)
		print_ref_list(kinds, detached, verbose, abbrev, with_commit);
	else if (rename && (argc == 1))
		rename_branch(head, argv[0], rename > 1);
	else if (rename && (argc == 2))
		rename_branch(argv[0], argv[1], rename > 1);
	else if (argc <= 2)
		create_branch(head, argv[0], (argc == 2) ? argv[1] : head,
			      force_create, reflog, track);
	else
		usage_with_options(builtin_branch_usage, options);

	return 0;
}
