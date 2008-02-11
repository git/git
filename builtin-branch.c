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

static const char * const builtin_branch_usage[] = {
	"git-branch [options] [-r | -a]",
	"git-branch [options] [-l] [-f] <branchname> [<start-point>]",
	"git-branch [options] [-r] (-d | -D) <branchname>",
	"git-branch [options] (-m | -M) [<oldbranch>] <newbranch>",
	NULL
};

#define REF_UNKNOWN_TYPE    0x00
#define REF_LOCAL_BRANCH    0x01
#define REF_REMOTE_BRANCH   0x02
#define REF_TAG             0x04

static const char *head;
static unsigned char head_sha1[20];

static int branch_track = 1;

static int branch_use_color;
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

static int git_branch_config(const char *var, const char *value)
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
	if (!strcmp(var, "branch.autosetupmerge")) {
		branch_track = git_config_bool(var, value);
		return 0;
	}
	return git_default_config(var, value);
}

static const char *branch_get_color(enum color_branch ix)
{
	if (branch_use_color)
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

		if (name)
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

	if (name)
		free(name);

	return(ret);
}

struct ref_item {
	char *name;
	unsigned int kind;
	unsigned char sha1[20];
};

struct ref_list {
	int index, alloc, maxwidth;
	struct ref_item *list;
	struct commit_list *with_commit;
	int kinds;
};

static int has_commit(const unsigned char *sha1, struct commit_list *with_commit)
{
	struct commit *commit;

	if (!with_commit)
		return 1;
	commit = lookup_commit_reference_gently(sha1, 1);
	if (!commit)
		return 0;
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
	int kind = REF_UNKNOWN_TYPE;
	int len;

	/* Detect kind */
	if (!prefixcmp(refname, "refs/heads/")) {
		kind = REF_LOCAL_BRANCH;
		refname += 11;
	} else if (!prefixcmp(refname, "refs/remotes/")) {
		kind = REF_REMOTE_BRANCH;
		refname += 13;
	} else if (!prefixcmp(refname, "refs/tags/")) {
		kind = REF_TAG;
		refname += 10;
	}

	/* Filter with with_commit if specified */
	if (!has_commit(sha1, ref_list->with_commit))
		return 0;

	/* Don't add types the caller doesn't want */
	if ((kind & ref_list->kinds) == 0)
		return 0;

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
	hashcpy(newitem->sha1, sha1);
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

static void print_ref_item(struct ref_item *item, int maxwidth, int verbose,
			   int abbrev, int current)
{
	char c;
	int color;
	struct commit *commit;

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

		strbuf_init(&subject, 0);

		commit = lookup_commit(item->sha1);
		if (commit && !parse_commit(commit)) {
			pretty_print_commit(CMIT_FMT_ONELINE, commit,
					    &subject, 0, NULL, NULL, 0, 0);
			sub = subject.buf;
		}
		printf("%c %s%-*s%s %s %s\n", c, branch_get_color(color),
		       maxwidth, item->name,
		       branch_get_color(COLOR_BRANCH_RESET),
		       find_unique_abbrev(item->sha1, abbrev), sub);
		strbuf_release(&subject);
	} else {
		printf("%c %s%s%s\n", c, branch_get_color(color), item->name,
		       branch_get_color(COLOR_BRANCH_RESET));
	}
}

static void print_ref_list(int kinds, int detached, int verbose, int abbrev, struct commit_list *with_commit)
{
	int i;
	struct ref_list ref_list;

	memset(&ref_list, 0, sizeof(ref_list));
	ref_list.kinds = kinds;
	ref_list.with_commit = with_commit;
	for_each_ref(append_ref, &ref_list);

	qsort(ref_list.list, ref_list.index, sizeof(struct ref_item), ref_cmp);

	detached = (detached && (kinds & REF_LOCAL_BRANCH));
	if (detached && has_commit(head_sha1, with_commit)) {
		struct ref_item item;
		item.name = xstrdup("(no branch)");
		item.kind = REF_LOCAL_BRANCH;
		hashcpy(item.sha1, head_sha1);
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

struct tracking {
	struct refspec spec;
	char *src;
	const char *remote;
	int matches;
};

static int find_tracked_branch(struct remote *remote, void *priv)
{
	struct tracking *tracking = priv;

	if (!remote_find_tracking(remote, &tracking->spec)) {
		if (++tracking->matches == 1) {
			tracking->src = tracking->spec.src;
			tracking->remote = remote->name;
		} else {
			free(tracking->spec.src);
			if (tracking->src) {
				free(tracking->src);
				tracking->src = NULL;
			}
		}
		tracking->spec.src = NULL;
	}

	return 0;
}


/*
 * This is called when new_ref is branched off of orig_ref, and tries
 * to infer the settings for branch.<new_ref>.{remote,merge} from the
 * config.
 */
static int setup_tracking(const char *new_ref, const char *orig_ref)
{
	char key[1024];
	struct tracking tracking;

	if (strlen(new_ref) > 1024 - 7 - 7 - 1)
		return error("Tracking not set up: name too long: %s",
				new_ref);

	memset(&tracking, 0, sizeof(tracking));
	tracking.spec.dst = (char *)orig_ref;
	if (for_each_remote(find_tracked_branch, &tracking) ||
			!tracking.matches)
		return 1;

	if (tracking.matches > 1)
		return error("Not tracking: ambiguous information for ref %s",
				orig_ref);

	if (tracking.matches == 1) {
		sprintf(key, "branch.%s.remote", new_ref);
		git_config_set(key, tracking.remote ?  tracking.remote : ".");
		sprintf(key, "branch.%s.merge", new_ref);
		git_config_set(key, tracking.src);
		free(tracking.src);
		printf("Branch %s set up to track remote branch %s.\n",
			       new_ref, orig_ref);
	}

	return 0;
}

static void create_branch(const char *name, const char *start_name,
			  int force, int reflog, int track)
{
	struct ref_lock *lock;
	struct commit *commit;
	unsigned char sha1[20];
	char *real_ref, ref[PATH_MAX], msg[PATH_MAX + 20];
	int forcing = 0;

	snprintf(ref, sizeof ref, "refs/heads/%s", name);
	if (check_ref_format(ref))
		die("'%s' is not a valid branch name.", name);

	if (resolve_ref(ref, sha1, 1, NULL)) {
		if (!force)
			die("A branch named '%s' already exists.", name);
		else if (!is_bare_repository() && !strcmp(head, name))
			die("Cannot force update the current branch.");
		forcing = 1;
	}

	real_ref = NULL;
	if (get_sha1(start_name, sha1))
		die("Not a valid object name: '%s'.", start_name);

	switch (dwim_ref(start_name, strlen(start_name), sha1, &real_ref)) {
	case 0:
		/* Not branching from any existing branch */
		real_ref = NULL;
		break;
	case 1:
		/* Unique completion -- good */
		break;
	default:
		die("Ambiguous object name: '%s'.", start_name);
		break;
	}

	if ((commit = lookup_commit_reference(sha1)) == NULL)
		die("Not a valid branch point: '%s'.", start_name);
	hashcpy(sha1, commit->object.sha1);

	lock = lock_any_ref_for_update(ref, NULL, 0);
	if (!lock)
		die("Failed to lock ref for update: %s.", strerror(errno));

	if (reflog)
		log_all_ref_updates = 1;

	if (forcing)
		snprintf(msg, sizeof msg, "branch: Reset from %s",
			 start_name);
	else
		snprintf(msg, sizeof msg, "branch: Created from %s",
			 start_name);

	/* When branching off a remote branch, set up so that git-pull
	   automatically merges from there.  So far, this is only done for
	   remotes registered via .git/config.  */
	if (real_ref && track)
		setup_tracking(name, real_ref);

	if (write_ref_sha1(lock, sha1, msg) < 0)
		die("Failed to write ref: %s.", strerror(errno));

	if (real_ref)
		free(real_ref);
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

int cmd_branch(int argc, const char **argv, const char *prefix)
{
	int delete = 0, rename = 0, force_create = 0;
	int verbose = 0, abbrev = DEFAULT_ABBREV, detached = 0;
	int reflog = 0, track;
	int kinds = REF_LOCAL_BRANCH;
	struct commit_list *with_commit = NULL;

	struct option options[] = {
		OPT_GROUP("Generic options"),
		OPT__VERBOSE(&verbose),
		OPT_BOOLEAN( 0 , "track",  &track, "set up tracking mode (see git-pull(1))"),
		OPT_BOOLEAN( 0 , "color",  &branch_use_color, "use colored output"),
		OPT_SET_INT('r', NULL,     &kinds, "act on remote-tracking branches",
			REF_REMOTE_BRANCH),
		OPT_CALLBACK(0, "contains", &with_commit, "commit",
			     "print only branches that contain the commit",
			     opt_parse_with_commit),
		{
			OPTION_CALLBACK, 0, "with", &with_commit, "commit",
			"print only branches that contain the commit",
			PARSE_OPT_HIDDEN, opt_parse_with_commit,
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
		OPT_END(),
	};

	git_config(git_branch_config);
	track = branch_track;
	argc = parse_options(argc, argv, options, builtin_branch_usage, 0);
	if (!!delete + !!rename + !!force_create > 1)
		usage_with_options(builtin_branch_usage, options);

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

	if (delete)
		return delete_branches(argc, argv, delete > 1, kinds);
	else if (argc == 0)
		print_ref_list(kinds, detached, verbose, abbrev, with_commit);
	else if (rename && (argc == 1))
		rename_branch(head, argv[0], rename > 1);
	else if (rename && (argc == 2))
		rename_branch(argv[0], argv[1], rename > 1);
	else if (argc <= 2)
		create_branch(argv[0], (argc == 2) ? argv[1] : head,
			      force_create, reflog, track);
	else
		usage_with_options(builtin_branch_usage, options);

	return 0;
}
