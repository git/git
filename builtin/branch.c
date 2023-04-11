/*
 * Builtin "git branch"
 *
 * Copyright (c) 2006 Kristian Høgsberg <krh@redhat.com>
 * Based on git-branch.sh by Junio C Hamano.
 */

#include "cache.h"
#include "config.h"
#include "color.h"
#include "editor.h"
#include "environment.h"
#include "refs.h"
#include "commit.h"
#include "builtin.h"
#include "gettext.h"
#include "object-name.h"
#include "remote.h"
#include "parse-options.h"
#include "branch.h"
#include "diff.h"
#include "revision.h"
#include "string-list.h"
#include "column.h"
#include "utf8.h"
#include "wt-status.h"
#include "ref-filter.h"
#include "worktree.h"
#include "help.h"
#include "commit-reach.h"
#include "wrapper.h"

static const char * const builtin_branch_usage[] = {
	N_("git branch [<options>] [-r | -a] [--merged] [--no-merged]"),
	N_("git branch [<options>] [-f] [--recurse-submodules] <branch-name> [<start-point>]"),
	N_("git branch [<options>] [-l] [<pattern>...]"),
	N_("git branch [<options>] [-r] (-d | -D) <branch-name>..."),
	N_("git branch [<options>] (-m | -M) [<old-branch>] <new-branch>"),
	N_("git branch [<options>] (-c | -C) [<old-branch>] <new-branch>"),
	N_("git branch [<options>] [-r | -a] [--points-at]"),
	N_("git branch [<options>] [-r | -a] [--format]"),
	NULL
};

static const char *head;
static struct object_id head_oid;
static int recurse_submodules = 0;
static int submodule_propagate_branches = 0;

static int branch_use_color = -1;
static char branch_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_NORMAL,       /* PLAIN */
	GIT_COLOR_RED,          /* REMOTE */
	GIT_COLOR_NORMAL,       /* LOCAL */
	GIT_COLOR_GREEN,        /* CURRENT */
	GIT_COLOR_BLUE,         /* UPSTREAM */
	GIT_COLOR_CYAN,         /* WORKTREE */
};
enum color_branch {
	BRANCH_COLOR_RESET = 0,
	BRANCH_COLOR_PLAIN = 1,
	BRANCH_COLOR_REMOTE = 2,
	BRANCH_COLOR_LOCAL = 3,
	BRANCH_COLOR_CURRENT = 4,
	BRANCH_COLOR_UPSTREAM = 5,
	BRANCH_COLOR_WORKTREE = 6
};

static const char *color_branch_slots[] = {
	[BRANCH_COLOR_RESET]	= "reset",
	[BRANCH_COLOR_PLAIN]	= "plain",
	[BRANCH_COLOR_REMOTE]	= "remote",
	[BRANCH_COLOR_LOCAL]	= "local",
	[BRANCH_COLOR_CURRENT]	= "current",
	[BRANCH_COLOR_UPSTREAM] = "upstream",
	[BRANCH_COLOR_WORKTREE] = "worktree",
};

static struct string_list output = STRING_LIST_INIT_DUP;
static unsigned int colopts;

define_list_config_array(color_branch_slots);

static int git_branch_config(const char *var, const char *value, void *cb)
{
	const char *slot_name;

	if (!strcmp(var, "branch.sort")) {
		if (!value)
			return config_error_nonbool(var);
		string_list_append(cb, value);
		return 0;
	}

	if (starts_with(var, "column."))
		return git_column_config(var, value, "branch", &colopts);
	if (!strcmp(var, "color.branch")) {
		branch_use_color = git_config_colorbool(var, value);
		return 0;
	}
	if (skip_prefix(var, "color.branch.", &slot_name)) {
		int slot = LOOKUP_CONFIG(color_branch_slots, slot_name);
		if (slot < 0)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		return color_parse(value, branch_colors[slot]);
	}
	if (!strcmp(var, "submodule.recurse")) {
		recurse_submodules = git_config_bool(var, value);
		return 0;
	}
	if (!strcasecmp(var, "submodule.propagateBranches")) {
		submodule_propagate_branches = git_config_bool(var, value);
		return 0;
	}

	return git_color_default_config(var, value, cb);
}

static const char *branch_get_color(enum color_branch ix)
{
	if (want_color(branch_use_color))
		return branch_colors[ix];
	return "";
}

static int branch_merged(int kind, const char *name,
			 struct commit *rev, struct commit *head_rev)
{
	/*
	 * This checks whether the merge bases of branch and HEAD (or
	 * the other branch this branch builds upon) contains the
	 * branch, which means that the branch has already been merged
	 * safely to HEAD (or the other branch).
	 */
	struct commit *reference_rev = NULL;
	const char *reference_name = NULL;
	void *reference_name_to_free = NULL;
	int merged;

	if (kind == FILTER_REFS_BRANCHES) {
		struct branch *branch = branch_get(name);
		const char *upstream = branch_get_upstream(branch, NULL);
		struct object_id oid;

		if (upstream &&
		    (reference_name = reference_name_to_free =
		     resolve_refdup(upstream, RESOLVE_REF_READING,
				    &oid, NULL)) != NULL)
			reference_rev = lookup_commit_reference(the_repository,
								&oid);
	}
	if (!reference_rev)
		reference_rev = head_rev;

	merged = reference_rev ? repo_in_merge_bases(the_repository, rev,
						     reference_rev) : 0;

	/*
	 * After the safety valve is fully redefined to "check with
	 * upstream, if any, otherwise with HEAD", we should just
	 * return the result of the repo_in_merge_bases() above without
	 * any of the following code, but during the transition period,
	 * a gentle reminder is in order.
	 */
	if ((head_rev != reference_rev) &&
	    (head_rev ? repo_in_merge_bases(the_repository, rev, head_rev) : 0) != merged) {
		if (merged)
			warning(_("deleting branch '%s' that has been merged to\n"
				"         '%s', but not yet merged to HEAD."),
				name, reference_name);
		else
			warning(_("not deleting branch '%s' that is not yet merged to\n"
				"         '%s', even though it is merged to HEAD."),
				name, reference_name);
	}
	free(reference_name_to_free);
	return merged;
}

static int check_branch_commit(const char *branchname, const char *refname,
			       const struct object_id *oid, struct commit *head_rev,
			       int kinds, int force)
{
	struct commit *rev = lookup_commit_reference(the_repository, oid);
	if (!force && !rev) {
		error(_("Couldn't look up commit object for '%s'"), refname);
		return -1;
	}
	if (!force && !branch_merged(kinds, branchname, rev, head_rev)) {
		error(_("The branch '%s' is not fully merged.\n"
		      "If you are sure you want to delete it, "
		      "run 'git branch -D %s'."), branchname, branchname);
		return -1;
	}
	return 0;
}

static void delete_branch_config(const char *branchname)
{
	struct strbuf buf = STRBUF_INIT;
	strbuf_addf(&buf, "branch.%s", branchname);
	if (git_config_rename_section(buf.buf, NULL) < 0)
		warning(_("Update of config-file failed"));
	strbuf_release(&buf);
}

static int delete_branches(int argc, const char **argv, int force, int kinds,
			   int quiet)
{
	struct commit *head_rev = NULL;
	struct object_id oid;
	char *name = NULL;
	const char *fmt;
	int i;
	int ret = 0;
	int remote_branch = 0;
	struct strbuf bname = STRBUF_INIT;
	unsigned allowed_interpret;
	struct string_list refs_to_delete = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	int branch_name_pos;

	switch (kinds) {
	case FILTER_REFS_REMOTES:
		fmt = "refs/remotes/%s";
		/* For subsequent UI messages */
		remote_branch = 1;
		allowed_interpret = INTERPRET_BRANCH_REMOTE;

		force = 1;
		break;
	case FILTER_REFS_BRANCHES:
		fmt = "refs/heads/%s";
		allowed_interpret = INTERPRET_BRANCH_LOCAL;
		break;
	default:
		die(_("cannot use -a with -d"));
	}
	branch_name_pos = strcspn(fmt, "%");

	if (!force)
		head_rev = lookup_commit_reference(the_repository, &head_oid);

	for (i = 0; i < argc; i++, strbuf_reset(&bname)) {
		char *target = NULL;
		int flags = 0;

		strbuf_branchname(&bname, argv[i], allowed_interpret);
		free(name);
		name = mkpathdup(fmt, bname.buf);

		if (kinds == FILTER_REFS_BRANCHES) {
			const char *path;
			if ((path = branch_checked_out(name))) {
				error(_("Cannot delete branch '%s' "
					"checked out at '%s'"),
				      bname.buf, path);
				ret = 1;
				continue;
			}
		}

		target = resolve_refdup(name,
					RESOLVE_REF_READING
					| RESOLVE_REF_NO_RECURSE
					| RESOLVE_REF_ALLOW_BAD_NAME,
					&oid, &flags);
		if (!target) {
			error(remote_branch
			      ? _("remote-tracking branch '%s' not found.")
			      : _("branch '%s' not found."), bname.buf);
			ret = 1;
			continue;
		}

		if (!(flags & (REF_ISSYMREF|REF_ISBROKEN)) &&
		    check_branch_commit(bname.buf, name, &oid, head_rev, kinds,
					force)) {
			ret = 1;
			goto next;
		}

		item = string_list_append(&refs_to_delete, name);
		item->util = xstrdup((flags & REF_ISBROKEN) ? "broken"
				    : (flags & REF_ISSYMREF) ? target
				    : repo_find_unique_abbrev(the_repository, &oid, DEFAULT_ABBREV));

	next:
		free(target);
	}

	if (delete_refs(NULL, &refs_to_delete, REF_NO_DEREF))
		ret = 1;

	for_each_string_list_item(item, &refs_to_delete) {
		char *describe_ref = item->util;
		char *name = item->string;
		if (!ref_exists(name)) {
			char *refname = name + branch_name_pos;
			if (!quiet)
				printf(remote_branch
					? _("Deleted remote-tracking branch %s (was %s).\n")
					: _("Deleted branch %s (was %s).\n"),
					name + branch_name_pos, describe_ref);

			delete_branch_config(refname);
		}
		free(describe_ref);
	}
	string_list_clear(&refs_to_delete, 0);

	free(name);
	strbuf_release(&bname);

	return ret;
}

static int calc_maxwidth(struct ref_array *refs, int remote_bonus)
{
	int i, max = 0;
	for (i = 0; i < refs->nr; i++) {
		struct ref_array_item *it = refs->items[i];
		const char *desc = it->refname;
		int w;

		skip_prefix(it->refname, "refs/heads/", &desc);
		skip_prefix(it->refname, "refs/remotes/", &desc);
		if (it->kind == FILTER_REFS_DETACHED_HEAD) {
			char *head_desc = get_head_description();
			w = utf8_strwidth(head_desc);
			free(head_desc);
		} else
			w = utf8_strwidth(desc);

		if (it->kind == FILTER_REFS_REMOTES)
			w += remote_bonus;
		if (w > max)
			max = w;
	}
	return max;
}

static const char *quote_literal_for_format(const char *s)
{
	static struct strbuf buf = STRBUF_INIT;

	strbuf_reset(&buf);
	while (*s) {
		const char *ep = strchrnul(s, '%');
		if (s < ep)
			strbuf_add(&buf, s, ep - s);
		if (*ep == '%') {
			strbuf_addstr(&buf, "%%");
			s = ep + 1;
		} else {
			s = ep;
		}
	}
	return buf.buf;
}

static char *build_format(struct ref_filter *filter, int maxwidth, const char *remote_prefix)
{
	struct strbuf fmt = STRBUF_INIT;
	struct strbuf local = STRBUF_INIT;
	struct strbuf remote = STRBUF_INIT;

	strbuf_addf(&local, "%%(if)%%(HEAD)%%(then)* %s%%(else)%%(if)%%(worktreepath)%%(then)+ %s%%(else)  %s%%(end)%%(end)",
			branch_get_color(BRANCH_COLOR_CURRENT),
			branch_get_color(BRANCH_COLOR_WORKTREE),
			branch_get_color(BRANCH_COLOR_LOCAL));
	strbuf_addf(&remote, "  %s",
		    branch_get_color(BRANCH_COLOR_REMOTE));

	if (filter->verbose) {
		struct strbuf obname = STRBUF_INIT;

		if (filter->abbrev < 0)
			strbuf_addf(&obname, "%%(objectname:short)");
		else if (!filter->abbrev)
			strbuf_addf(&obname, "%%(objectname)");
		else
			strbuf_addf(&obname, "%%(objectname:short=%d)", filter->abbrev);

		strbuf_addf(&local, "%%(align:%d,left)%%(refname:lstrip=2)%%(end)", maxwidth);
		strbuf_addstr(&local, branch_get_color(BRANCH_COLOR_RESET));
		strbuf_addf(&local, " %s ", obname.buf);

		if (filter->verbose > 1)
		{
			strbuf_addf(&local, "%%(if:notequals=*)%%(HEAD)%%(then)%%(if)%%(worktreepath)%%(then)(%s%%(worktreepath)%s) %%(end)%%(end)",
				    branch_get_color(BRANCH_COLOR_WORKTREE), branch_get_color(BRANCH_COLOR_RESET));
			strbuf_addf(&local, "%%(if)%%(upstream)%%(then)[%s%%(upstream:short)%s%%(if)%%(upstream:track)"
				    "%%(then): %%(upstream:track,nobracket)%%(end)] %%(end)%%(contents:subject)",
				    branch_get_color(BRANCH_COLOR_UPSTREAM), branch_get_color(BRANCH_COLOR_RESET));
		}
		else
			strbuf_addf(&local, "%%(if)%%(upstream:track)%%(then)%%(upstream:track) %%(end)%%(contents:subject)");

		strbuf_addf(&remote, "%%(align:%d,left)%s%%(refname:lstrip=2)%%(end)%s"
			    "%%(if)%%(symref)%%(then) -> %%(symref:short)"
			    "%%(else) %s %%(contents:subject)%%(end)",
			    maxwidth, quote_literal_for_format(remote_prefix),
			    branch_get_color(BRANCH_COLOR_RESET), obname.buf);
		strbuf_release(&obname);
	} else {
		strbuf_addf(&local, "%%(refname:lstrip=2)%s%%(if)%%(symref)%%(then) -> %%(symref:short)%%(end)",
			    branch_get_color(BRANCH_COLOR_RESET));
		strbuf_addf(&remote, "%s%%(refname:lstrip=2)%s%%(if)%%(symref)%%(then) -> %%(symref:short)%%(end)",
			    quote_literal_for_format(remote_prefix),
			    branch_get_color(BRANCH_COLOR_RESET));
	}

	strbuf_addf(&fmt, "%%(if:notequals=refs/remotes)%%(refname:rstrip=-2)%%(then)%s%%(else)%s%%(end)", local.buf, remote.buf);

	strbuf_release(&local);
	strbuf_release(&remote);
	return strbuf_detach(&fmt, NULL);
}

static void print_ref_list(struct ref_filter *filter, struct ref_sorting *sorting,
			   struct ref_format *format, struct string_list *output)
{
	int i;
	struct ref_array array;
	struct strbuf out = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;
	int maxwidth = 0;
	const char *remote_prefix = "";
	char *to_free = NULL;

	/*
	 * If we are listing more than just remote branches,
	 * then remote branches will have a "remotes/" prefix.
	 * We need to account for this in the width.
	 */
	if (filter->kind != FILTER_REFS_REMOTES)
		remote_prefix = "remotes/";

	memset(&array, 0, sizeof(array));

	filter_refs(&array, filter, filter->kind);

	if (filter->verbose)
		maxwidth = calc_maxwidth(&array, strlen(remote_prefix));

	if (!format->format)
		format->format = to_free = build_format(filter, maxwidth, remote_prefix);
	format->use_color = branch_use_color;

	if (verify_ref_format(format))
		die(_("unable to parse format string"));

	ref_array_sort(sorting, &array);

	for (i = 0; i < array.nr; i++) {
		strbuf_reset(&err);
		strbuf_reset(&out);
		if (format_ref_array_item(array.items[i], format, &out, &err))
			die("%s", err.buf);
		if (column_active(colopts)) {
			assert(!filter->verbose && "--column and --verbose are incompatible");
			 /* format to a string_list to let print_columns() do its job */
			string_list_append(output, out.buf);
		} else {
			fwrite(out.buf, 1, out.len, stdout);
			putchar('\n');
		}
	}

	strbuf_release(&err);
	strbuf_release(&out);
	ref_array_clear(&array);
	free(to_free);
}

static void print_current_branch_name(void)
{
	int flags;
	const char *refname = resolve_ref_unsafe("HEAD", 0, NULL, &flags);
	const char *shortname;
	if (!refname)
		die(_("could not resolve HEAD"));
	else if (!(flags & REF_ISSYMREF))
		return;
	else if (skip_prefix(refname, "refs/heads/", &shortname))
		puts(shortname);
	else
		die(_("HEAD (%s) points outside of refs/heads/"), refname);
}

static void reject_rebase_or_bisect_branch(const char *target)
{
	struct worktree **worktrees = get_worktrees();
	int i;

	for (i = 0; worktrees[i]; i++) {
		struct worktree *wt = worktrees[i];

		if (!wt->is_detached)
			continue;

		if (is_worktree_being_rebased(wt, target))
			die(_("Branch %s is being rebased at %s"),
			    target, wt->path);

		if (is_worktree_being_bisected(wt, target))
			die(_("Branch %s is being bisected at %s"),
			    target, wt->path);
	}

	free_worktrees(worktrees);
}

static void copy_or_rename_branch(const char *oldname, const char *newname, int copy, int force)
{
	struct strbuf oldref = STRBUF_INIT, newref = STRBUF_INIT, logmsg = STRBUF_INIT;
	struct strbuf oldsection = STRBUF_INIT, newsection = STRBUF_INIT;
	const char *interpreted_oldname = NULL;
	const char *interpreted_newname = NULL;
	int recovery = 0;

	if (strbuf_check_branch_ref(&oldref, oldname)) {
		/*
		 * Bad name --- this could be an attempt to rename a
		 * ref that we used to allow to be created by accident.
		 */
		if (ref_exists(oldref.buf))
			recovery = 1;
		else
			die(_("Invalid branch name: '%s'"), oldname);
	}

	if ((copy || strcmp(head, oldname)) && !ref_exists(oldref.buf)) {
		if (copy && !strcmp(head, oldname))
			die(_("No commit on branch '%s' yet."), oldname);
		else
			die(_("No branch named '%s'."), oldname);
	}

	/*
	 * A command like "git branch -M currentbranch currentbranch" cannot
	 * cause the worktree to become inconsistent with HEAD, so allow it.
	 */
	if (!strcmp(oldname, newname))
		validate_branchname(newname, &newref);
	else
		validate_new_branchname(newname, &newref, force);

	reject_rebase_or_bisect_branch(oldref.buf);

	if (!skip_prefix(oldref.buf, "refs/heads/", &interpreted_oldname) ||
	    !skip_prefix(newref.buf, "refs/heads/", &interpreted_newname)) {
		BUG("expected prefix missing for refs");
	}

	if (copy)
		strbuf_addf(&logmsg, "Branch: copied %s to %s",
			    oldref.buf, newref.buf);
	else
		strbuf_addf(&logmsg, "Branch: renamed %s to %s",
			    oldref.buf, newref.buf);

	if (!copy &&
	    (!head || strcmp(oldname, head) || !is_null_oid(&head_oid)) &&
	    rename_ref(oldref.buf, newref.buf, logmsg.buf))
		die(_("Branch rename failed"));
	if (copy && copy_existing_ref(oldref.buf, newref.buf, logmsg.buf))
		die(_("Branch copy failed"));

	if (recovery) {
		if (copy)
			warning(_("Created a copy of a misnamed branch '%s'"),
				interpreted_oldname);
		else
			warning(_("Renamed a misnamed branch '%s' away"),
				interpreted_oldname);
	}

	if (!copy &&
	    replace_each_worktree_head_symref(oldref.buf, newref.buf, logmsg.buf))
		die(_("Branch renamed to %s, but HEAD is not updated!"), newname);

	strbuf_release(&logmsg);

	strbuf_addf(&oldsection, "branch.%s", interpreted_oldname);
	strbuf_addf(&newsection, "branch.%s", interpreted_newname);
	if (!copy && git_config_rename_section(oldsection.buf, newsection.buf) < 0)
		die(_("Branch is renamed, but update of config-file failed"));
	if (copy && strcmp(interpreted_oldname, interpreted_newname) && git_config_copy_section(oldsection.buf, newsection.buf) < 0)
		die(_("Branch is copied, but update of config-file failed"));
	strbuf_release(&oldref);
	strbuf_release(&newref);
	strbuf_release(&oldsection);
	strbuf_release(&newsection);
}

static GIT_PATH_FUNC(edit_description, "EDIT_DESCRIPTION")

static int edit_branch_description(const char *branch_name)
{
	int exists;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf name = STRBUF_INIT;

	exists = !read_branch_desc(&buf, branch_name);
	if (!buf.len || buf.buf[buf.len-1] != '\n')
		strbuf_addch(&buf, '\n');
	strbuf_commented_addf(&buf,
		    _("Please edit the description for the branch\n"
		      "  %s\n"
		      "Lines starting with '%c' will be stripped.\n"),
		    branch_name, comment_line_char);
	write_file_buf(edit_description(), buf.buf, buf.len);
	strbuf_reset(&buf);
	if (launch_editor(edit_description(), &buf, NULL)) {
		strbuf_release(&buf);
		return -1;
	}
	strbuf_stripspace(&buf, 1);

	strbuf_addf(&name, "branch.%s.description", branch_name);
	if (buf.len || exists)
		git_config_set(name.buf, buf.len ? buf.buf : NULL);
	strbuf_release(&name);
	strbuf_release(&buf);

	return 0;
}

int cmd_branch(int argc, const char **argv, const char *prefix)
{
	/* possible actions */
	int delete = 0, rename = 0, copy = 0, list = 0,
	    unset_upstream = 0, show_current = 0, edit_description = 0;
	const char *new_upstream = NULL;
	int noncreate_actions = 0;
	/* possible options */
	int reflog = 0, quiet = 0, icase = 0, force = 0,
	    recurse_submodules_explicit = 0;
	enum branch_track track;
	struct ref_filter filter;
	static struct ref_sorting *sorting;
	struct string_list sorting_options = STRING_LIST_INIT_DUP;
	struct ref_format format = REF_FORMAT_INIT;

	struct option options[] = {
		OPT_GROUP(N_("Generic options")),
		OPT__VERBOSE(&filter.verbose,
			N_("show hash and subject, give twice for upstream branch")),
		OPT__QUIET(&quiet, N_("suppress informational messages")),
		OPT_CALLBACK_F('t', "track",  &track, "(direct|inherit)",
			N_("set branch tracking configuration"),
			PARSE_OPT_OPTARG,
			parse_opt_tracking_mode),
		OPT_SET_INT_F(0, "set-upstream", &track, N_("do not use"),
			BRANCH_TRACK_OVERRIDE, PARSE_OPT_HIDDEN),
		OPT_STRING('u', "set-upstream-to", &new_upstream, N_("upstream"), N_("change the upstream info")),
		OPT_BOOL(0, "unset-upstream", &unset_upstream, N_("unset the upstream info")),
		OPT__COLOR(&branch_use_color, N_("use colored output")),
		OPT_SET_INT('r', "remotes",     &filter.kind, N_("act on remote-tracking branches"),
			FILTER_REFS_REMOTES),
		OPT_CONTAINS(&filter.with_commit, N_("print only branches that contain the commit")),
		OPT_NO_CONTAINS(&filter.no_commit, N_("print only branches that don't contain the commit")),
		OPT_WITH(&filter.with_commit, N_("print only branches that contain the commit")),
		OPT_WITHOUT(&filter.no_commit, N_("print only branches that don't contain the commit")),
		OPT__ABBREV(&filter.abbrev),

		OPT_GROUP(N_("Specific git-branch actions:")),
		OPT_SET_INT('a', "all", &filter.kind, N_("list both remote-tracking and local branches"),
			FILTER_REFS_REMOTES | FILTER_REFS_BRANCHES),
		OPT_BIT('d', "delete", &delete, N_("delete fully merged branch"), 1),
		OPT_BIT('D', NULL, &delete, N_("delete branch (even if not merged)"), 2),
		OPT_BIT('m', "move", &rename, N_("move/rename a branch and its reflog"), 1),
		OPT_BIT('M', NULL, &rename, N_("move/rename a branch, even if target exists"), 2),
		OPT_BIT('c', "copy", &copy, N_("copy a branch and its reflog"), 1),
		OPT_BIT('C', NULL, &copy, N_("copy a branch, even if target exists"), 2),
		OPT_BOOL('l', "list", &list, N_("list branch names")),
		OPT_BOOL(0, "show-current", &show_current, N_("show current branch name")),
		OPT_BOOL(0, "create-reflog", &reflog, N_("create the branch's reflog")),
		OPT_BOOL(0, "edit-description", &edit_description,
			 N_("edit the description for the branch")),
		OPT__FORCE(&force, N_("force creation, move/rename, deletion"), PARSE_OPT_NOCOMPLETE),
		OPT_MERGED(&filter, N_("print only branches that are merged")),
		OPT_NO_MERGED(&filter, N_("print only branches that are not merged")),
		OPT_COLUMN(0, "column", &colopts, N_("list branches in columns")),
		OPT_REF_SORT(&sorting_options),
		OPT_CALLBACK(0, "points-at", &filter.points_at, N_("object"),
			N_("print only branches of the object"), parse_opt_object_name),
		OPT_BOOL('i', "ignore-case", &icase, N_("sorting and filtering are case insensitive")),
		OPT_BOOL(0, "recurse-submodules", &recurse_submodules_explicit, N_("recurse through submodules")),
		OPT_STRING(  0 , "format", &format.format, N_("format"), N_("format to use for the output")),
		OPT_END(),
	};

	setup_ref_filter_porcelain_msg();

	memset(&filter, 0, sizeof(filter));
	filter.kind = FILTER_REFS_BRANCHES;
	filter.abbrev = -1;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_branch_usage, options);

	git_config(git_branch_config, &sorting_options);

	track = git_branch_track;

	head = resolve_refdup("HEAD", 0, &head_oid, NULL);
	if (!head)
		die(_("Failed to resolve HEAD as a valid ref."));
	if (!strcmp(head, "HEAD"))
		filter.detached = 1;
	else if (!skip_prefix(head, "refs/heads/", &head))
		die(_("HEAD not found below refs/heads!"));

	argc = parse_options(argc, argv, prefix, options, builtin_branch_usage,
			     0);

	if (!delete && !rename && !copy && !edit_description && !new_upstream &&
	    !show_current && !unset_upstream && argc == 0)
		list = 1;

	if (filter.with_commit || filter.no_commit ||
	    filter.reachable_from || filter.unreachable_from || filter.points_at.nr)
		list = 1;

	noncreate_actions = !!delete + !!rename + !!copy + !!new_upstream +
			    !!show_current + !!list + !!edit_description +
			    !!unset_upstream;
	if (noncreate_actions > 1)
		usage_with_options(builtin_branch_usage, options);

	if (recurse_submodules_explicit) {
		if (!submodule_propagate_branches)
			die(_("branch with --recurse-submodules can only be used if submodule.propagateBranches is enabled"));
		if (noncreate_actions)
			die(_("--recurse-submodules can only be used to create branches"));
	}

	recurse_submodules =
		(recurse_submodules || recurse_submodules_explicit) &&
		submodule_propagate_branches;

	if (filter.abbrev == -1)
		filter.abbrev = DEFAULT_ABBREV;
	filter.ignore_case = icase;

	finalize_colopts(&colopts, -1);
	if (filter.verbose) {
		if (explicitly_enable_column(colopts))
			die(_("options '%s' and '%s' cannot be used together"), "--column", "--verbose");
		colopts = 0;
	}

	if (force) {
		delete *= 2;
		rename *= 2;
		copy *= 2;
	}

	if (list)
		setup_auto_pager("branch", 1);

	if (delete) {
		if (!argc)
			die(_("branch name required"));
		return delete_branches(argc, argv, delete > 1, filter.kind, quiet);
	} else if (show_current) {
		print_current_branch_name();
		return 0;
	} else if (list) {
		/*  git branch --list also shows HEAD when it is detached */
		if ((filter.kind & FILTER_REFS_BRANCHES) && filter.detached)
			filter.kind |= FILTER_REFS_DETACHED_HEAD;
		filter.name_patterns = argv;
		/*
		 * If no sorting parameter is given then we default to sorting
		 * by 'refname'. This would give us an alphabetically sorted
		 * array with the 'HEAD' ref at the beginning followed by
		 * local branches 'refs/heads/...' and finally remote-tracking
		 * branches 'refs/remotes/...'.
		 */
		sorting = ref_sorting_options(&sorting_options);
		ref_sorting_set_sort_flags_all(sorting, REF_SORTING_ICASE, icase);
		ref_sorting_set_sort_flags_all(
			sorting, REF_SORTING_DETACHED_HEAD_FIRST, 1);
		print_ref_list(&filter, sorting, &format, &output);
		print_columns(&output, colopts, NULL);
		string_list_clear(&output, 0);
		ref_sorting_release(sorting);
		return 0;
	} else if (edit_description) {
		const char *branch_name;
		struct strbuf branch_ref = STRBUF_INIT;
		struct strbuf buf = STRBUF_INIT;
		int ret = 1; /* assume failure */

		if (!argc) {
			if (filter.detached)
				die(_("Cannot give description to detached HEAD"));
			branch_name = head;
		} else if (argc == 1) {
			strbuf_branchname(&buf, argv[0], INTERPRET_BRANCH_LOCAL);
			branch_name = buf.buf;
		} else {
			die(_("cannot edit description of more than one branch"));
		}

		strbuf_addf(&branch_ref, "refs/heads/%s", branch_name);
		if (!ref_exists(branch_ref.buf))
			error((!argc || !strcmp(head, branch_name))
			      ? _("No commit on branch '%s' yet.")
			      : _("No branch named '%s'."),
			      branch_name);
		else if (!edit_branch_description(branch_name))
			ret = 0; /* happy */

		strbuf_release(&branch_ref);
		strbuf_release(&buf);

		return ret;
	} else if (copy || rename) {
		if (!argc)
			die(_("branch name required"));
		else if ((argc == 1) && filter.detached)
			die(copy? _("cannot copy the current branch while not on any.")
				: _("cannot rename the current branch while not on any."));
		else if (argc == 1)
			copy_or_rename_branch(head, argv[0], copy, copy + rename > 1);
		else if (argc == 2)
			copy_or_rename_branch(argv[0], argv[1], copy, copy + rename > 1);
		else
			die(copy? _("too many branches for a copy operation")
				: _("too many arguments for a rename operation"));
	} else if (new_upstream) {
		struct branch *branch;
		struct strbuf buf = STRBUF_INIT;

		if (!argc)
			branch = branch_get(NULL);
		else if (argc == 1) {
			strbuf_branchname(&buf, argv[0], INTERPRET_BRANCH_LOCAL);
			branch = branch_get(buf.buf);
		} else
			die(_("too many arguments to set new upstream"));

		if (!branch) {
			if (!argc || !strcmp(argv[0], "HEAD"))
				die(_("could not set upstream of HEAD to %s when "
				      "it does not point to any branch."),
				    new_upstream);
			die(_("no such branch '%s'"), argv[0]);
		}

		if (!ref_exists(branch->refname)) {
			if (!argc || !strcmp(head, branch->name))
				die(_("No commit on branch '%s' yet."), branch->name);
			die(_("branch '%s' does not exist"), branch->name);
		}

		dwim_and_setup_tracking(the_repository, branch->name,
					new_upstream, BRANCH_TRACK_OVERRIDE,
					quiet);
		strbuf_release(&buf);
	} else if (unset_upstream) {
		struct branch *branch;
		struct strbuf buf = STRBUF_INIT;

		if (!argc)
			branch = branch_get(NULL);
		else if (argc == 1) {
			strbuf_branchname(&buf, argv[0], INTERPRET_BRANCH_LOCAL);
			branch = branch_get(buf.buf);
		} else
			die(_("too many arguments to unset upstream"));

		if (!branch) {
			if (!argc || !strcmp(argv[0], "HEAD"))
				die(_("could not unset upstream of HEAD when "
				      "it does not point to any branch."));
			die(_("no such branch '%s'"), argv[0]);
		}

		if (!branch_has_merge_config(branch))
			die(_("Branch '%s' has no upstream information"), branch->name);

		strbuf_reset(&buf);
		strbuf_addf(&buf, "branch.%s.remote", branch->name);
		git_config_set_multivar(buf.buf, NULL, NULL, CONFIG_FLAGS_MULTI_REPLACE);
		strbuf_reset(&buf);
		strbuf_addf(&buf, "branch.%s.merge", branch->name);
		git_config_set_multivar(buf.buf, NULL, NULL, CONFIG_FLAGS_MULTI_REPLACE);
		strbuf_release(&buf);
	} else if (!noncreate_actions && argc > 0 && argc <= 2) {
		const char *branch_name = argv[0];
		const char *start_name = argc == 2 ? argv[1] : head;

		if (filter.kind != FILTER_REFS_BRANCHES)
			die(_("The -a, and -r, options to 'git branch' do not take a branch name.\n"
				  "Did you mean to use: -a|-r --list <pattern>?"));

		if (track == BRANCH_TRACK_OVERRIDE)
			die(_("the '--set-upstream' option is no longer supported. Please use '--track' or '--set-upstream-to' instead."));

		if (recurse_submodules) {
			create_branches_recursively(the_repository, branch_name,
						    start_name, NULL, force,
						    reflog, quiet, track, 0);
			return 0;
		}
		create_branch(the_repository, branch_name, start_name, force, 0,
			      reflog, quiet, track, 0);
	} else
		usage_with_options(builtin_branch_usage, options);

	return 0;
}
