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
	"git branch [options] [-r] (-d | -D) <branchname>...",
	"git branch [options] (-m | -M) [<oldbranch>] <newbranch>",
	NULL
};

#define REF_LOCAL_BRANCH    0x01
#define REF_REMOTE_BRANCH   0x02

static const char *head;
static unsigned char head_sha1[20];

static int branch_use_color = -1;
static char branch_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_NORMAL,	/* PLAIN */
	GIT_COLOR_RED,		/* REMOTE */
	GIT_COLOR_NORMAL,	/* LOCAL */
	GIT_COLOR_GREEN,	/* CURRENT */
};
enum color_branch {
	BRANCH_COLOR_RESET = 0,
	BRANCH_COLOR_PLAIN = 1,
	BRANCH_COLOR_REMOTE = 2,
	BRANCH_COLOR_LOCAL = 3,
	BRANCH_COLOR_CURRENT = 4
};

static enum merge_filter {
	NO_FILTER = 0,
	SHOW_NOT_MERGED,
	SHOW_MERGED
} merge_filter;
static unsigned char merge_filter_ref[20];

static int parse_branch_color_slot(const char *var, int ofs)
{
	if (!strcasecmp(var+ofs, "plain"))
		return BRANCH_COLOR_PLAIN;
	if (!strcasecmp(var+ofs, "reset"))
		return BRANCH_COLOR_RESET;
	if (!strcasecmp(var+ofs, "remote"))
		return BRANCH_COLOR_REMOTE;
	if (!strcasecmp(var+ofs, "local"))
		return BRANCH_COLOR_LOCAL;
	if (!strcasecmp(var+ofs, "current"))
		return BRANCH_COLOR_CURRENT;
	return -1;
}

static int git_branch_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "color.branch")) {
		branch_use_color = git_config_colorbool(var, value);
		return 0;
	}
	if (!prefixcmp(var, "color.branch.")) {
		int slot = parse_branch_color_slot(var, 13);
		if (slot < 0)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		color_parse(value, var, branch_colors[slot]);
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
	int merged;

	if (kind == REF_LOCAL_BRANCH) {
		struct branch *branch = branch_get(name);
		unsigned char sha1[20];

		if (branch &&
		    branch->merge &&
		    branch->merge[0] &&
		    branch->merge[0]->dst &&
		    (reference_name =
		     resolve_ref(branch->merge[0]->dst, sha1, 1, NULL)) != NULL)
			reference_rev = lookup_commit_reference(sha1);
	}
	if (!reference_rev)
		reference_rev = head_rev;

	merged = in_merge_bases(rev, &reference_rev, 1);

	/*
	 * After the safety valve is fully redefined to "check with
	 * upstream, if any, otherwise with HEAD", we should just
	 * return the result of the in_merge_bases() above without
	 * any of the following code, but during the transition period,
	 * a gentle reminder is in order.
	 */
	if ((head_rev != reference_rev) &&
	    in_merge_bases(rev, &head_rev, 1) != merged) {
		if (merged)
			warning(_("deleting branch '%s' that has been merged to\n"
				"         '%s', but not yet merged to HEAD."),
				name, reference_name);
		else
			warning(_("not deleting branch '%s' that is not yet merged to\n"
				"         '%s', even though it is merged to HEAD."),
				name, reference_name);
	}
	return merged;
}

static int delete_branches(int argc, const char **argv, int force, int kinds)
{
	struct commit *rev, *head_rev = NULL;
	unsigned char sha1[20];
	char *name = NULL;
	const char *fmt, *remote;
	int i;
	int ret = 0;
	struct strbuf bname = STRBUF_INIT;

	switch (kinds) {
	case REF_REMOTE_BRANCH:
		fmt = "refs/remotes/%s";
		/* TRANSLATORS: This is "remote " in "remote branch '%s' not found" */
		remote = _("remote ");
		force = 1;
		break;
	case REF_LOCAL_BRANCH:
		fmt = "refs/heads/%s";
		remote = "";
		break;
	default:
		die(_("cannot use -a with -d"));
	}

	if (!force) {
		head_rev = lookup_commit_reference(head_sha1);
		if (!head_rev)
			die(_("Couldn't look up commit object for HEAD"));
	}
	for (i = 0; i < argc; i++, strbuf_release(&bname)) {
		strbuf_branchname(&bname, argv[i]);
		if (kinds == REF_LOCAL_BRANCH && !strcmp(head, bname.buf)) {
			error(_("Cannot delete the branch '%s' "
			      "which you are currently on."), bname.buf);
			ret = 1;
			continue;
		}

		free(name);

		name = xstrdup(mkpath(fmt, bname.buf));
		if (!resolve_ref(name, sha1, 1, NULL)) {
			error(_("%sbranch '%s' not found."),
					remote, bname.buf);
			ret = 1;
			continue;
		}

		rev = lookup_commit_reference(sha1);
		if (!rev) {
			error(_("Couldn't look up commit object for '%s'"), name);
			ret = 1;
			continue;
		}

		if (!force && !branch_merged(kinds, bname.buf, rev, head_rev)) {
			error(_("The branch '%s' is not fully merged.\n"
			      "If you are sure you want to delete it, "
			      "run 'git branch -D %s'."), bname.buf, bname.buf);
			ret = 1;
			continue;
		}

		if (delete_ref(name, sha1, 0)) {
			error(_("Error deleting %sbranch '%s'"), remote,
			      bname.buf);
			ret = 1;
		} else {
			struct strbuf buf = STRBUF_INIT;
			printf(_("Deleted %sbranch %s (was %s).\n"), remote,
			       bname.buf,
			       find_unique_abbrev(sha1, DEFAULT_ABBREV));
			strbuf_addf(&buf, "branch.%s", bname.buf);
			if (git_config_rename_section(buf.buf, NULL) < 0)
				warning(_("Update of config-file failed"));
			strbuf_release(&buf);
		}
	}

	free(name);

	return(ret);
}

struct ref_item {
	char *name;
	char *dest;
	unsigned int kind, len;
	struct commit *commit;
};

struct ref_list {
	struct rev_info revs;
	int index, alloc, maxwidth, verbose, abbrev;
	struct ref_item *list;
	struct commit_list *with_commit;
	int kinds;
};

static char *resolve_symref(const char *src, const char *prefix)
{
	unsigned char sha1[20];
	int flag;
	const char *dst, *cp;

	dst = resolve_ref(src, sha1, 0, &flag);
	if (!(dst && (flag & REF_ISSYMREF)))
		return NULL;
	if (prefix && (cp = skip_prefix(dst, prefix)))
		dst = cp;
	return xstrdup(dst);
}

struct append_ref_cb {
	struct ref_list *ref_list;
	const char **pattern;
	int ret;
};

static int match_patterns(const char **pattern, const char *refname)
{
	if (!*pattern)
		return 1; /* no pattern always matches */
	while (*pattern) {
		if (!fnmatch(*pattern, refname, 0))
			return 1;
		pattern++;
	}
	return 0;
}

static int append_ref(const char *refname, const unsigned char *sha1, int flags, void *cb_data)
{
	struct append_ref_cb *cb = (struct append_ref_cb *)(cb_data);
	struct ref_list *ref_list = cb->ref_list;
	struct ref_item *newitem;
	struct commit *commit;
	int kind, i;
	const char *prefix, *orig_refname = refname;

	static struct {
		int kind;
		const char *prefix;
		int pfxlen;
	} ref_kind[] = {
		{ REF_LOCAL_BRANCH, "refs/heads/", 11 },
		{ REF_REMOTE_BRANCH, "refs/remotes/", 13 },
	};

	/* Detect kind */
	for (i = 0; i < ARRAY_SIZE(ref_kind); i++) {
		prefix = ref_kind[i].prefix;
		if (strncmp(refname, prefix, ref_kind[i].pfxlen))
			continue;
		kind = ref_kind[i].kind;
		refname += ref_kind[i].pfxlen;
		break;
	}
	if (ARRAY_SIZE(ref_kind) <= i)
		return 0;

	/* Don't add types the caller doesn't want */
	if ((kind & ref_list->kinds) == 0)
		return 0;

	if (!match_patterns(cb->pattern, refname))
		return 0;

	commit = NULL;
	if (ref_list->verbose || ref_list->with_commit || merge_filter != NO_FILTER) {
		commit = lookup_commit_reference_gently(sha1, 1);
		if (!commit) {
			cb->ret = error(_("branch '%s' does not point at a commit"), refname);
			return 0;
		}

		/* Filter with with_commit if specified */
		if (!is_descendant_of(commit, ref_list->with_commit))
			return 0;

		if (merge_filter != NO_FILTER)
			add_pending_object(&ref_list->revs,
					   (struct object *)commit, refname);
	}

	ALLOC_GROW(ref_list->list, ref_list->index + 1, ref_list->alloc);

	/* Record the new item */
	newitem = &(ref_list->list[ref_list->index++]);
	newitem->name = xstrdup(refname);
	newitem->kind = kind;
	newitem->commit = commit;
	newitem->len = strlen(refname);
	newitem->dest = resolve_symref(orig_refname, prefix);
	/* adjust for "remotes/" */
	if (newitem->kind == REF_REMOTE_BRANCH &&
	    ref_list->kinds != REF_REMOTE_BRANCH)
		newitem->len += 8;
	if (newitem->len > ref_list->maxwidth)
		ref_list->maxwidth = newitem->len;

	return 0;
}

static void free_ref_list(struct ref_list *ref_list)
{
	int i;

	for (i = 0; i < ref_list->index; i++) {
		free(ref_list->list[i].name);
		free(ref_list->list[i].dest);
	}
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

static void fill_tracking_info(struct strbuf *stat, const char *branch_name,
		int show_upstream_ref)
{
	int ours, theirs;
	struct branch *branch = branch_get(branch_name);

	if (!stat_tracking_info(branch, &ours, &theirs)) {
		if (branch && branch->merge && branch->merge[0]->dst &&
		    show_upstream_ref)
			strbuf_addf(stat, "[%s] ",
			    shorten_unambiguous_ref(branch->merge[0]->dst, 0));
		return;
	}

	strbuf_addch(stat, '[');
	if (show_upstream_ref)
		strbuf_addf(stat, "%s: ",
			shorten_unambiguous_ref(branch->merge[0]->dst, 0));
	if (!ours)
		strbuf_addf(stat, _("behind %d] "), theirs);
	else if (!theirs)
		strbuf_addf(stat, _("ahead %d] "), ours);
	else
		strbuf_addf(stat, _("ahead %d, behind %d] "), ours, theirs);
}

static int matches_merge_filter(struct commit *commit)
{
	int is_merged;

	if (merge_filter == NO_FILTER)
		return 1;

	is_merged = !!(commit->object.flags & UNINTERESTING);
	return (is_merged == (merge_filter == SHOW_MERGED));
}

static void add_verbose_info(struct strbuf *out, struct ref_item *item,
			     int verbose, int abbrev)
{
	struct strbuf subject = STRBUF_INIT, stat = STRBUF_INIT;
	const char *sub = " **** invalid ref ****";
	struct commit *commit = item->commit;

	if (commit && !parse_commit(commit)) {
		pp_commit_easy(CMIT_FMT_ONELINE, commit, &subject);
		sub = subject.buf;
	}

	if (item->kind == REF_LOCAL_BRANCH)
		fill_tracking_info(&stat, item->name, verbose > 1);

	strbuf_addf(out, " %s %s%s",
		find_unique_abbrev(item->commit->object.sha1, abbrev),
		stat.buf, sub);
	strbuf_release(&stat);
	strbuf_release(&subject);
}

static void print_ref_item(struct ref_item *item, int maxwidth, int verbose,
			   int abbrev, int current, char *prefix)
{
	char c;
	int color;
	struct commit *commit = item->commit;
	struct strbuf out = STRBUF_INIT, name = STRBUF_INIT;

	if (!matches_merge_filter(commit))
		return;

	switch (item->kind) {
	case REF_LOCAL_BRANCH:
		color = BRANCH_COLOR_LOCAL;
		break;
	case REF_REMOTE_BRANCH:
		color = BRANCH_COLOR_REMOTE;
		break;
	default:
		color = BRANCH_COLOR_PLAIN;
		break;
	}

	c = ' ';
	if (current) {
		c = '*';
		color = BRANCH_COLOR_CURRENT;
	}

	strbuf_addf(&name, "%s%s", prefix, item->name);
	if (verbose)
		strbuf_addf(&out, "%c %s%-*s%s", c, branch_get_color(color),
			    maxwidth, name.buf,
			    branch_get_color(BRANCH_COLOR_RESET));
	else
		strbuf_addf(&out, "%c %s%s%s", c, branch_get_color(color),
			    name.buf, branch_get_color(BRANCH_COLOR_RESET));

	if (item->dest)
		strbuf_addf(&out, " -> %s", item->dest);
	else if (verbose)
		/* " f7c0c00 [ahead 58, behind 197] vcs-svn: drop obj_pool.h" */
		add_verbose_info(&out, item, verbose, abbrev);
	printf("%s\n", out.buf);
	strbuf_release(&name);
	strbuf_release(&out);
}

static int calc_maxwidth(struct ref_list *refs)
{
	int i, w = 0;
	for (i = 0; i < refs->index; i++) {
		if (!matches_merge_filter(refs->list[i].commit))
			continue;
		if (refs->list[i].len > w)
			w = refs->list[i].len;
	}
	return w;
}


static void show_detached(struct ref_list *ref_list)
{
	struct commit *head_commit = lookup_commit_reference_gently(head_sha1, 1);

	if (head_commit && is_descendant_of(head_commit, ref_list->with_commit)) {
		struct ref_item item;
		item.name = xstrdup(_("(no branch)"));
		item.len = strlen(item.name);
		item.kind = REF_LOCAL_BRANCH;
		item.dest = NULL;
		item.commit = head_commit;
		if (item.len > ref_list->maxwidth)
			ref_list->maxwidth = item.len;
		print_ref_item(&item, ref_list->maxwidth, ref_list->verbose, ref_list->abbrev, 1, "");
		free(item.name);
	}
}

static int print_ref_list(int kinds, int detached, int verbose, int abbrev, struct commit_list *with_commit, const char **pattern)
{
	int i;
	struct append_ref_cb cb;
	struct ref_list ref_list;

	memset(&ref_list, 0, sizeof(ref_list));
	ref_list.kinds = kinds;
	ref_list.verbose = verbose;
	ref_list.abbrev = abbrev;
	ref_list.with_commit = with_commit;
	if (merge_filter != NO_FILTER)
		init_revisions(&ref_list.revs, NULL);
	cb.ref_list = &ref_list;
	cb.pattern = pattern;
	cb.ret = 0;
	for_each_rawref(append_ref, &cb);
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
	if (detached && match_patterns(pattern, "HEAD"))
		show_detached(&ref_list);

	for (i = 0; i < ref_list.index; i++) {
		int current = !detached &&
			(ref_list.list[i].kind == REF_LOCAL_BRANCH) &&
			!strcmp(ref_list.list[i].name, head);
		char *prefix = (kinds != REF_REMOTE_BRANCH &&
				ref_list.list[i].kind == REF_REMOTE_BRANCH)
				? "remotes/" : "";
		print_ref_item(&ref_list.list[i], ref_list.maxwidth, verbose,
			       abbrev, current, prefix);
	}

	free_ref_list(&ref_list);

	if (cb.ret)
		error(_("some refs could not be read"));

	return cb.ret;
}

static void rename_branch(const char *oldname, const char *newname, int force)
{
	struct strbuf oldref = STRBUF_INIT, newref = STRBUF_INIT, logmsg = STRBUF_INIT;
	unsigned char sha1[20];
	struct strbuf oldsection = STRBUF_INIT, newsection = STRBUF_INIT;
	int recovery = 0;

	if (!oldname)
		die(_("cannot rename the current branch while not on any."));

	if (strbuf_check_branch_ref(&oldref, oldname)) {
		/*
		 * Bad name --- this could be an attempt to rename a
		 * ref that we used to allow to be created by accident.
		 */
		if (resolve_ref(oldref.buf, sha1, 1, NULL))
			recovery = 1;
		else
			die(_("Invalid branch name: '%s'"), oldname);
	}

	validate_new_branchname(newname, &newref, force, 0);

	strbuf_addf(&logmsg, "Branch: renamed %s to %s",
		 oldref.buf, newref.buf);

	if (rename_ref(oldref.buf, newref.buf, logmsg.buf))
		die(_("Branch rename failed"));
	strbuf_release(&logmsg);

	if (recovery)
		warning(_("Renamed a misnamed branch '%s' away"), oldref.buf + 11);

	/* no need to pass logmsg here as HEAD didn't really move */
	if (!strcmp(oldname, head) && create_symref("HEAD", newref.buf, NULL))
		die(_("Branch renamed to %s, but HEAD is not updated!"), newname);

	strbuf_addf(&oldsection, "branch.%s", oldref.buf + 11);
	strbuf_release(&oldref);
	strbuf_addf(&newsection, "branch.%s", newref.buf + 11);
	strbuf_release(&newref);
	if (git_config_rename_section(oldsection.buf, newsection.buf) < 0)
		die(_("Branch is renamed, but update of config-file failed"));
	strbuf_release(&oldsection);
	strbuf_release(&newsection);
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
		die(_("malformed object name %s"), arg);
	return 0;
}

static const char edit_description[] = "BRANCH_DESCRIPTION";

static int edit_branch_description(const char *branch_name)
{
	FILE *fp;
	int status;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf name = STRBUF_INIT;

	read_branch_desc(&buf, branch_name);
	if (!buf.len || buf.buf[buf.len-1] != '\n')
		strbuf_addch(&buf, '\n');
	strbuf_addf(&buf,
		    "# Please edit the description for the branch\n"
		    "#   %s\n"
		    "# Lines starting with '#' will be stripped.\n",
		    branch_name);
	fp = fopen(git_path(edit_description), "w");
	if ((fwrite(buf.buf, 1, buf.len, fp) < buf.len) || fclose(fp)) {
		strbuf_release(&buf);
		return error(_("could not write branch description template: %s\n"),
			     strerror(errno));
	}
	strbuf_reset(&buf);
	if (launch_editor(git_path(edit_description), &buf, NULL)) {
		strbuf_release(&buf);
		return -1;
	}
	stripspace(&buf, 1);

	strbuf_addf(&name, "branch.%s.description", branch_name);
	status = git_config_set(name.buf, buf.buf);
	strbuf_release(&name);
	strbuf_release(&buf);

	return status;
}

int cmd_branch(int argc, const char **argv, const char *prefix)
{
	int delete = 0, rename = 0, force_create = 0, list = 0;
	int verbose = 0, abbrev = -1, detached = 0;
	int reflog = 0, edit_description = 0;
	enum branch_track track;
	int kinds = REF_LOCAL_BRANCH;
	struct commit_list *with_commit = NULL;

	struct option options[] = {
		OPT_GROUP("Generic options"),
		OPT__VERBOSE(&verbose,
			"show hash and subject, give twice for upstream branch"),
		OPT_SET_INT('t', "track",  &track, "set up tracking mode (see git-pull(1))",
			BRANCH_TRACK_EXPLICIT),
		OPT_SET_INT( 0, "set-upstream",  &track, "change upstream info",
			BRANCH_TRACK_OVERRIDE),
		OPT__COLOR(&branch_use_color, "use colored output"),
		OPT_SET_INT('r', "remotes",     &kinds, "act on remote-tracking branches",
			REF_REMOTE_BRANCH),
		{
			OPTION_CALLBACK, 0, "contains", &with_commit, "commit",
			"print only branches that contain the commit",
			PARSE_OPT_LASTARG_DEFAULT,
			parse_opt_with_commit, (intptr_t)"HEAD",
		},
		{
			OPTION_CALLBACK, 0, "with", &with_commit, "commit",
			"print only branches that contain the commit",
			PARSE_OPT_HIDDEN | PARSE_OPT_LASTARG_DEFAULT,
			parse_opt_with_commit, (intptr_t) "HEAD",
		},
		OPT__ABBREV(&abbrev),

		OPT_GROUP("Specific git-branch actions:"),
		OPT_SET_INT('a', "all", &kinds, "list both remote-tracking and local branches",
			REF_REMOTE_BRANCH | REF_LOCAL_BRANCH),
		OPT_BIT('d', "delete", &delete, "delete fully merged branch", 1),
		OPT_BIT('D', NULL, &delete, "delete branch (even if not merged)", 2),
		OPT_BIT('m', "move", &rename, "move/rename a branch and its reflog", 1),
		OPT_BIT('M', NULL, &rename, "move/rename a branch, even if target exists", 2),
		OPT_BOOLEAN(0, "list", &list, "list branch names"),
		OPT_BOOLEAN('l', "create-reflog", &reflog, "create the branch's reflog"),
		OPT_BOOLEAN(0, "edit-description", &edit_description,
			    "edit the description for the branch"),
		OPT__FORCE(&force_create, "force creation (when already exists)"),
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

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_branch_usage, options);

	git_config(git_branch_config, NULL);

	track = git_branch_track;

	head = resolve_ref("HEAD", head_sha1, 0, NULL);
	if (!head)
		die(_("Failed to resolve HEAD as a valid ref."));
	head = xstrdup(head);
	if (!strcmp(head, "HEAD")) {
		detached = 1;
	} else {
		if (prefixcmp(head, "refs/heads/"))
			die(_("HEAD not found below refs/heads!"));
		head += 11;
	}
	hashcpy(merge_filter_ref, head_sha1);

	argc = parse_options(argc, argv, prefix, options, builtin_branch_usage,
			     0);

	if (!delete && !rename && !force_create && !edit_description && argc == 0)
		list = 1;

	if (!!delete + !!rename + !!force_create + !!list > 1)
		usage_with_options(builtin_branch_usage, options);

	if (abbrev == -1)
		abbrev = DEFAULT_ABBREV;

	if (delete)
		return delete_branches(argc, argv, delete > 1, kinds);
	else if (list)
		return print_ref_list(kinds, detached, verbose, abbrev,
				      with_commit, argv);
	else if (edit_description) {
		const char *branch_name;
		if (detached)
			die("Cannot give description to detached HEAD");
		if (!argc)
			branch_name = head;
		else if (argc == 1)
			branch_name = argv[0];
		else
			usage_with_options(builtin_branch_usage, options);
		if (edit_branch_description(branch_name))
			return 1;
	}
	else if (rename && (argc == 1))
		rename_branch(head, argv[0], rename > 1);
	else if (rename && (argc == 2))
		rename_branch(argv[0], argv[1], rename > 1);
	else if (argc <= 2) {
		if (kinds != REF_LOCAL_BRANCH)
			die(_("-a and -r options to 'git branch' do not make sense with a branch name"));
		create_branch(head, argv[0], (argc == 2) ? argv[1] : head,
			      force_create, reflog, track);
	} else
		usage_with_options(builtin_branch_usage, options);

	return 0;
}
