/*
 * Builtin "git commit"
 *
 * Copyright (c) 2007 Kristian Høgsberg <krh@redhat.com>
 * Based on git-commit.sh by Junio C Hamano and Linus Torvalds
 */

#include "cache.h"
#include "lockfile.h"
#include "cache-tree.h"
#include "color.h"
#include "dir.h"
#include "builtin.h"
#include "diff.h"
#include "diffcore.h"
#include "commit.h"
#include "revision.h"
#include "wt-status.h"
#include "run-command.h"
#include "refs.h"
#include "log-tree.h"
#include "strbuf.h"
#include "utf8.h"
#include "parse-options.h"
#include "string-list.h"
#include "rerere.h"
#include "unpack-trees.h"
#include "quote.h"
#include "submodule.h"
#include "gpg-interface.h"
#include "column.h"
#include "sequencer.h"
#include "notes-utils.h"
#include "mailmap.h"
#include "sigchain.h"

static const char * const builtin_commit_usage[] = {
	N_("git commit [<options>] [--] <pathspec>..."),
	NULL
};

static const char * const builtin_status_usage[] = {
	N_("git status [<options>] [--] <pathspec>..."),
	NULL
};

static const char implicit_ident_advice_noconfig[] =
N_("Your name and email address were configured automatically based\n"
"on your username and hostname. Please check that they are accurate.\n"
"You can suppress this message by setting them explicitly. Run the\n"
"following command and follow the instructions in your editor to edit\n"
"your configuration file:\n"
"\n"
"    git config --global --edit\n"
"\n"
"After doing this, you may fix the identity used for this commit with:\n"
"\n"
"    git commit --amend --reset-author\n");

static const char implicit_ident_advice_config[] =
N_("Your name and email address were configured automatically based\n"
"on your username and hostname. Please check that they are accurate.\n"
"You can suppress this message by setting them explicitly:\n"
"\n"
"    git config --global user.name \"Your Name\"\n"
"    git config --global user.email you@example.com\n"
"\n"
"After doing this, you may fix the identity used for this commit with:\n"
"\n"
"    git commit --amend --reset-author\n");

static const char empty_amend_advice[] =
N_("You asked to amend the most recent commit, but doing so would make\n"
"it empty. You can repeat your command with --allow-empty, or you can\n"
"remove the commit entirely with \"git reset HEAD^\".\n");

static const char empty_cherry_pick_advice[] =
N_("The previous cherry-pick is now empty, possibly due to conflict resolution.\n"
"If you wish to commit it anyway, use:\n"
"\n"
"    git commit --allow-empty\n"
"\n");

static const char empty_cherry_pick_advice_single[] =
N_("Otherwise, please use 'git reset'\n");

static const char empty_cherry_pick_advice_multi[] =
N_("If you wish to skip this commit, use:\n"
"\n"
"    git reset\n"
"\n"
"Then \"git cherry-pick --continue\" will resume cherry-picking\n"
"the remaining commits.\n");

static GIT_PATH_FUNC(git_path_commit_editmsg, "COMMIT_EDITMSG")

static const char *use_message_buffer;
static struct lock_file index_lock; /* real index */
static struct lock_file false_lock; /* used only for partial commits */
static enum {
	COMMIT_AS_IS = 1,
	COMMIT_NORMAL,
	COMMIT_PARTIAL
} commit_style;

static const char *logfile, *force_author;
static const char *template_file;
/*
 * The _message variables are commit names from which to take
 * the commit message and/or authorship.
 */
static const char *author_message, *author_message_buffer;
static char *edit_message, *use_message;
static char *fixup_message, *squash_message;
static int all, also, interactive, patch_interactive, only, amend, signoff;
static int edit_flag = -1; /* unspecified */
static int quiet, verbose, no_verify, allow_empty, dry_run, renew_authorship;
static int config_commit_verbose = -1; /* unspecified */
static int no_post_rewrite, allow_empty_message;
static char *untracked_files_arg, *force_date, *ignore_submodule_arg;
static char *sign_commit;

/*
 * The default commit message cleanup mode will remove the lines
 * beginning with # (shell comments) and leading and trailing
 * whitespaces (empty lines or containing only whitespaces)
 * if editor is used, and only the whitespaces if the message
 * is specified explicitly.
 */
static enum {
	CLEANUP_SPACE,
	CLEANUP_NONE,
	CLEANUP_SCISSORS,
	CLEANUP_ALL
} cleanup_mode;
static const char *cleanup_arg;

static enum commit_whence whence;
static int sequencer_in_use;
static int use_editor = 1, include_status = 1;
static int show_ignored_in_status, have_option_m;
static const char *only_include_assumed;
static struct strbuf message = STRBUF_INIT;

static enum wt_status_format status_format = STATUS_FORMAT_UNSPECIFIED;

static int opt_parse_porcelain(const struct option *opt, const char *arg, int unset)
{
	enum wt_status_format *value = (enum wt_status_format *)opt->value;
	if (unset)
		*value = STATUS_FORMAT_NONE;
	else if (!arg)
		*value = STATUS_FORMAT_PORCELAIN;
	else if (!strcmp(arg, "v1") || !strcmp(arg, "1"))
		*value = STATUS_FORMAT_PORCELAIN;
	else if (!strcmp(arg, "v2") || !strcmp(arg, "2"))
		*value = STATUS_FORMAT_PORCELAIN_V2;
	else
		die("unsupported porcelain version '%s'", arg);

	return 0;
}

static int opt_parse_m(const struct option *opt, const char *arg, int unset)
{
	struct strbuf *buf = opt->value;
	if (unset) {
		have_option_m = 0;
		strbuf_setlen(buf, 0);
	} else {
		have_option_m = 1;
		if (buf->len)
			strbuf_addch(buf, '\n');
		strbuf_addstr(buf, arg);
		strbuf_complete_line(buf);
	}
	return 0;
}

static void determine_whence(struct wt_status *s)
{
	if (file_exists(git_path_merge_head()))
		whence = FROM_MERGE;
	else if (file_exists(git_path_cherry_pick_head())) {
		whence = FROM_CHERRY_PICK;
		if (file_exists(git_path_seq_dir()))
			sequencer_in_use = 1;
	}
	else
		whence = FROM_COMMIT;
	if (s)
		s->whence = whence;
}

static void status_init_config(struct wt_status *s, config_fn_t fn)
{
	wt_status_prepare(s);
	gitmodules_config();
	git_config(fn, s);
	determine_whence(s);
	init_diff_ui_defaults();
	s->hints = advice_status_hints; /* must come after git_config() */
}

static void rollback_index_files(void)
{
	switch (commit_style) {
	case COMMIT_AS_IS:
		break; /* nothing to do */
	case COMMIT_NORMAL:
		rollback_lock_file(&index_lock);
		break;
	case COMMIT_PARTIAL:
		rollback_lock_file(&index_lock);
		rollback_lock_file(&false_lock);
		break;
	}
}

static int commit_index_files(void)
{
	int err = 0;

	switch (commit_style) {
	case COMMIT_AS_IS:
		break; /* nothing to do */
	case COMMIT_NORMAL:
		err = commit_lock_file(&index_lock);
		break;
	case COMMIT_PARTIAL:
		err = commit_lock_file(&index_lock);
		rollback_lock_file(&false_lock);
		break;
	}

	return err;
}

/*
 * Take a union of paths in the index and the named tree (typically, "HEAD"),
 * and return the paths that match the given pattern in list.
 */
static int list_paths(struct string_list *list, const char *with_tree,
		      const char *prefix, const struct pathspec *pattern)
{
	int i, ret;
	char *m;

	if (!pattern->nr)
		return 0;

	m = xcalloc(1, pattern->nr);

	if (with_tree) {
		char *max_prefix = common_prefix(pattern);
		overlay_tree_on_cache(with_tree, max_prefix ? max_prefix : prefix);
		free(max_prefix);
	}

	for (i = 0; i < active_nr; i++) {
		const struct cache_entry *ce = active_cache[i];
		struct string_list_item *item;

		if (ce->ce_flags & CE_UPDATE)
			continue;
		if (!ce_path_match(ce, pattern, m))
			continue;
		item = string_list_insert(list, ce->name);
		if (ce_skip_worktree(ce))
			item->util = item; /* better a valid pointer than a fake one */
	}

	ret = report_path_error(m, pattern, prefix);
	free(m);
	return ret;
}

static void add_remove_files(struct string_list *list)
{
	int i;
	for (i = 0; i < list->nr; i++) {
		struct stat st;
		struct string_list_item *p = &(list->items[i]);

		/* p->util is skip-worktree */
		if (p->util)
			continue;

		if (!lstat(p->string, &st)) {
			if (add_to_cache(p->string, &st, 0))
				die(_("updating files failed"));
		} else
			remove_file_from_cache(p->string);
	}
}

static void create_base_index(const struct commit *current_head)
{
	struct tree *tree;
	struct unpack_trees_options opts;
	struct tree_desc t;

	if (!current_head) {
		discard_cache();
		return;
	}

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = 1;
	opts.index_only = 1;
	opts.merge = 1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;

	opts.fn = oneway_merge;
	tree = parse_tree_indirect(current_head->object.oid.hash);
	if (!tree)
		die(_("failed to unpack HEAD tree object"));
	parse_tree(tree);
	init_tree_desc(&t, tree->buffer, tree->size);
	if (unpack_trees(1, &t, &opts))
		exit(128); /* We've already reported the error, finish dying */
}

static void refresh_cache_or_die(int refresh_flags)
{
	/*
	 * refresh_flags contains REFRESH_QUIET, so the only errors
	 * are for unmerged entries.
	 */
	if (refresh_cache(refresh_flags | REFRESH_IN_PORCELAIN))
		die_resolve_conflict("commit");
}

static const char *prepare_index(int argc, const char **argv, const char *prefix,
				 const struct commit *current_head, int is_status)
{
	struct string_list partial;
	struct pathspec pathspec;
	int refresh_flags = REFRESH_QUIET;
	const char *ret;

	if (is_status)
		refresh_flags |= REFRESH_UNMERGED;
	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL,
		       prefix, argv);

	if (read_cache_preload(&pathspec) < 0)
		die(_("index file corrupt"));

	if (interactive) {
		char *old_index_env = NULL;
		hold_locked_index(&index_lock, LOCK_DIE_ON_ERROR);

		refresh_cache_or_die(refresh_flags);

		if (write_locked_index(&the_index, &index_lock, CLOSE_LOCK))
			die(_("unable to create temporary index"));

		old_index_env = getenv(INDEX_ENVIRONMENT);
		setenv(INDEX_ENVIRONMENT, get_lock_file_path(&index_lock), 1);

		if (interactive_add(argc, argv, prefix, patch_interactive) != 0)
			die(_("interactive add failed"));

		if (old_index_env && *old_index_env)
			setenv(INDEX_ENVIRONMENT, old_index_env, 1);
		else
			unsetenv(INDEX_ENVIRONMENT);

		discard_cache();
		read_cache_from(get_lock_file_path(&index_lock));
		if (update_main_cache_tree(WRITE_TREE_SILENT) == 0) {
			if (reopen_lock_file(&index_lock) < 0)
				die(_("unable to write index file"));
			if (write_locked_index(&the_index, &index_lock, CLOSE_LOCK))
				die(_("unable to update temporary index"));
		} else
			warning(_("Failed to update main cache tree"));

		commit_style = COMMIT_NORMAL;
		return get_lock_file_path(&index_lock);
	}

	/*
	 * Non partial, non as-is commit.
	 *
	 * (1) get the real index;
	 * (2) update the_index as necessary;
	 * (3) write the_index out to the real index (still locked);
	 * (4) return the name of the locked index file.
	 *
	 * The caller should run hooks on the locked real index, and
	 * (A) if all goes well, commit the real index;
	 * (B) on failure, rollback the real index.
	 */
	if (all || (also && pathspec.nr)) {
		hold_locked_index(&index_lock, LOCK_DIE_ON_ERROR);
		add_files_to_cache(also ? prefix : NULL, &pathspec, 0);
		refresh_cache_or_die(refresh_flags);
		update_main_cache_tree(WRITE_TREE_SILENT);
		if (write_locked_index(&the_index, &index_lock, CLOSE_LOCK))
			die(_("unable to write new_index file"));
		commit_style = COMMIT_NORMAL;
		return get_lock_file_path(&index_lock);
	}

	/*
	 * As-is commit.
	 *
	 * (1) return the name of the real index file.
	 *
	 * The caller should run hooks on the real index,
	 * and create commit from the_index.
	 * We still need to refresh the index here.
	 */
	if (!only && !pathspec.nr) {
		hold_locked_index(&index_lock, LOCK_DIE_ON_ERROR);
		refresh_cache_or_die(refresh_flags);
		if (active_cache_changed
		    || !cache_tree_fully_valid(active_cache_tree))
			update_main_cache_tree(WRITE_TREE_SILENT);
		if (active_cache_changed) {
			if (write_locked_index(&the_index, &index_lock,
					       COMMIT_LOCK))
				die(_("unable to write new_index file"));
		} else {
			rollback_lock_file(&index_lock);
		}
		commit_style = COMMIT_AS_IS;
		return get_index_file();
	}

	/*
	 * A partial commit.
	 *
	 * (0) find the set of affected paths;
	 * (1) get lock on the real index file;
	 * (2) update the_index with the given paths;
	 * (3) write the_index out to the real index (still locked);
	 * (4) get lock on the false index file;
	 * (5) reset the_index from HEAD;
	 * (6) update the_index the same way as (2);
	 * (7) write the_index out to the false index file;
	 * (8) return the name of the false index file (still locked);
	 *
	 * The caller should run hooks on the locked false index, and
	 * create commit from it.  Then
	 * (A) if all goes well, commit the real index;
	 * (B) on failure, rollback the real index;
	 * In either case, rollback the false index.
	 */
	commit_style = COMMIT_PARTIAL;

	if (whence != FROM_COMMIT) {
		if (whence == FROM_MERGE)
			die(_("cannot do a partial commit during a merge."));
		else if (whence == FROM_CHERRY_PICK)
			die(_("cannot do a partial commit during a cherry-pick."));
	}

	string_list_init(&partial, 1);
	if (list_paths(&partial, !current_head ? NULL : "HEAD", prefix, &pathspec))
		exit(1);

	discard_cache();
	if (read_cache() < 0)
		die(_("cannot read the index"));

	hold_locked_index(&index_lock, LOCK_DIE_ON_ERROR);
	add_remove_files(&partial);
	refresh_cache(REFRESH_QUIET);
	update_main_cache_tree(WRITE_TREE_SILENT);
	if (write_locked_index(&the_index, &index_lock, CLOSE_LOCK))
		die(_("unable to write new_index file"));

	hold_lock_file_for_update(&false_lock,
				  git_path("next-index-%"PRIuMAX,
					   (uintmax_t) getpid()),
				  LOCK_DIE_ON_ERROR);

	create_base_index(current_head);
	add_remove_files(&partial);
	refresh_cache(REFRESH_QUIET);

	if (write_locked_index(&the_index, &false_lock, CLOSE_LOCK))
		die(_("unable to write temporary index file"));

	discard_cache();
	ret = get_lock_file_path(&false_lock);
	read_cache_from(ret);
	return ret;
}

static int run_status(FILE *fp, const char *index_file, const char *prefix, int nowarn,
		      struct wt_status *s)
{
	struct object_id oid;

	if (s->relative_paths)
		s->prefix = prefix;

	if (amend) {
		s->amend = 1;
		s->reference = "HEAD^1";
	}
	s->verbose = verbose;
	s->index_file = index_file;
	s->fp = fp;
	s->nowarn = nowarn;
	s->is_initial = get_sha1(s->reference, oid.hash) ? 1 : 0;
	if (!s->is_initial)
		hashcpy(s->sha1_commit, oid.hash);
	s->status_format = status_format;
	s->ignore_submodule_arg = ignore_submodule_arg;

	wt_status_collect(s);
	wt_status_print(s);

	return s->commitable;
}

static int is_a_merge(const struct commit *current_head)
{
	return !!(current_head->parents && current_head->parents->next);
}

static void assert_split_ident(struct ident_split *id, const struct strbuf *buf)
{
	if (split_ident_line(id, buf->buf, buf->len) || !id->date_begin)
		die("BUG: unable to parse our own ident: %s", buf->buf);
}

static void export_one(const char *var, const char *s, const char *e, int hack)
{
	struct strbuf buf = STRBUF_INIT;
	if (hack)
		strbuf_addch(&buf, hack);
	strbuf_addf(&buf, "%.*s", (int)(e - s), s);
	setenv(var, buf.buf, 1);
	strbuf_release(&buf);
}

static int parse_force_date(const char *in, struct strbuf *out)
{
	strbuf_addch(out, '@');

	if (parse_date(in, out) < 0) {
		int errors = 0;
		unsigned long t = approxidate_careful(in, &errors);
		if (errors)
			return -1;
		strbuf_addf(out, "%lu", t);
	}

	return 0;
}

static void set_ident_var(char **buf, char *val)
{
	free(*buf);
	*buf = val;
}

static void determine_author_info(struct strbuf *author_ident)
{
	char *name, *email, *date;
	struct ident_split author;

	name = xstrdup_or_null(getenv("GIT_AUTHOR_NAME"));
	email = xstrdup_or_null(getenv("GIT_AUTHOR_EMAIL"));
	date = xstrdup_or_null(getenv("GIT_AUTHOR_DATE"));

	if (author_message) {
		struct ident_split ident;
		size_t len;
		const char *a;

		a = find_commit_header(author_message_buffer, "author", &len);
		if (!a)
			die(_("commit '%s' lacks author header"), author_message);
		if (split_ident_line(&ident, a, len) < 0)
			die(_("commit '%s' has malformed author line"), author_message);

		set_ident_var(&name, xmemdupz(ident.name_begin, ident.name_end - ident.name_begin));
		set_ident_var(&email, xmemdupz(ident.mail_begin, ident.mail_end - ident.mail_begin));

		if (ident.date_begin) {
			struct strbuf date_buf = STRBUF_INIT;
			strbuf_addch(&date_buf, '@');
			strbuf_add(&date_buf, ident.date_begin, ident.date_end - ident.date_begin);
			strbuf_addch(&date_buf, ' ');
			strbuf_add(&date_buf, ident.tz_begin, ident.tz_end - ident.tz_begin);
			set_ident_var(&date, strbuf_detach(&date_buf, NULL));
		}
	}

	if (force_author) {
		struct ident_split ident;

		if (split_ident_line(&ident, force_author, strlen(force_author)) < 0)
			die(_("malformed --author parameter"));
		set_ident_var(&name, xmemdupz(ident.name_begin, ident.name_end - ident.name_begin));
		set_ident_var(&email, xmemdupz(ident.mail_begin, ident.mail_end - ident.mail_begin));
	}

	if (force_date) {
		struct strbuf date_buf = STRBUF_INIT;
		if (parse_force_date(force_date, &date_buf))
			die(_("invalid date format: %s"), force_date);
		set_ident_var(&date, strbuf_detach(&date_buf, NULL));
	}

	strbuf_addstr(author_ident, fmt_ident(name, email, date, IDENT_STRICT));
	assert_split_ident(&author, author_ident);
	export_one("GIT_AUTHOR_NAME", author.name_begin, author.name_end, 0);
	export_one("GIT_AUTHOR_EMAIL", author.mail_begin, author.mail_end, 0);
	export_one("GIT_AUTHOR_DATE", author.date_begin, author.tz_end, '@');
	free(name);
	free(email);
	free(date);
}

static int author_date_is_interesting(void)
{
	return author_message || force_date;
}

static void adjust_comment_line_char(const struct strbuf *sb)
{
	char candidates[] = "#;@!$%^&|:";
	char *candidate;
	const char *p;

	comment_line_char = candidates[0];
	if (!memchr(sb->buf, comment_line_char, sb->len))
		return;

	p = sb->buf;
	candidate = strchr(candidates, *p);
	if (candidate)
		*candidate = ' ';
	for (p = sb->buf; *p; p++) {
		if ((p[0] == '\n' || p[0] == '\r') && p[1]) {
			candidate = strchr(candidates, p[1]);
			if (candidate)
				*candidate = ' ';
		}
	}

	for (p = candidates; *p == ' '; p++)
		;
	if (!*p)
		die(_("unable to select a comment character that is not used\n"
		      "in the current commit message"));
	comment_line_char = *p;
}

static int prepare_to_commit(const char *index_file, const char *prefix,
			     struct commit *current_head,
			     struct wt_status *s,
			     struct strbuf *author_ident)
{
	struct stat statbuf;
	struct strbuf committer_ident = STRBUF_INIT;
	int commitable;
	struct strbuf sb = STRBUF_INIT;
	const char *hook_arg1 = NULL;
	const char *hook_arg2 = NULL;
	int clean_message_contents = (cleanup_mode != CLEANUP_NONE);
	int old_display_comment_prefix;

	/* This checks and barfs if author is badly specified */
	determine_author_info(author_ident);

	if (!no_verify && run_commit_hook(use_editor, index_file, "pre-commit", NULL))
		return 0;

	if (squash_message) {
		/*
		 * Insert the proper subject line before other commit
		 * message options add their content.
		 */
		if (use_message && !strcmp(use_message, squash_message))
			strbuf_addstr(&sb, "squash! ");
		else {
			struct pretty_print_context ctx = {0};
			struct commit *c;
			c = lookup_commit_reference_by_name(squash_message);
			if (!c)
				die(_("could not lookup commit %s"), squash_message);
			ctx.output_encoding = get_commit_output_encoding();
			format_commit_message(c, "squash! %s\n\n", &sb,
					      &ctx);
		}
	}

	if (have_option_m) {
		strbuf_addbuf(&sb, &message);
		hook_arg1 = "message";
	} else if (logfile && !strcmp(logfile, "-")) {
		if (isatty(0))
			fprintf(stderr, _("(reading log message from standard input)\n"));
		if (strbuf_read(&sb, 0, 0) < 0)
			die_errno(_("could not read log from standard input"));
		hook_arg1 = "message";
	} else if (logfile) {
		if (strbuf_read_file(&sb, logfile, 0) < 0)
			die_errno(_("could not read log file '%s'"),
				  logfile);
		hook_arg1 = "message";
	} else if (use_message) {
		char *buffer;
		buffer = strstr(use_message_buffer, "\n\n");
		if (buffer)
			strbuf_addstr(&sb, skip_blank_lines(buffer + 2));
		hook_arg1 = "commit";
		hook_arg2 = use_message;
	} else if (fixup_message) {
		struct pretty_print_context ctx = {0};
		struct commit *commit;
		commit = lookup_commit_reference_by_name(fixup_message);
		if (!commit)
			die(_("could not lookup commit %s"), fixup_message);
		ctx.output_encoding = get_commit_output_encoding();
		format_commit_message(commit, "fixup! %s\n\n",
				      &sb, &ctx);
		hook_arg1 = "message";
	} else if (!stat(git_path_merge_msg(), &statbuf)) {
		/*
		 * prepend SQUASH_MSG here if it exists and a
		 * "merge --squash" was originally performed
		 */
		if (!stat(git_path_squash_msg(), &statbuf)) {
			if (strbuf_read_file(&sb, git_path_squash_msg(), 0) < 0)
				die_errno(_("could not read SQUASH_MSG"));
			hook_arg1 = "squash";
		} else
			hook_arg1 = "merge";
		if (strbuf_read_file(&sb, git_path_merge_msg(), 0) < 0)
			die_errno(_("could not read MERGE_MSG"));
	} else if (!stat(git_path_squash_msg(), &statbuf)) {
		if (strbuf_read_file(&sb, git_path_squash_msg(), 0) < 0)
			die_errno(_("could not read SQUASH_MSG"));
		hook_arg1 = "squash";
	} else if (template_file) {
		if (strbuf_read_file(&sb, template_file, 0) < 0)
			die_errno(_("could not read '%s'"), template_file);
		hook_arg1 = "template";
		clean_message_contents = 0;
	}

	/*
	 * The remaining cases don't modify the template message, but
	 * just set the argument(s) to the prepare-commit-msg hook.
	 */
	else if (whence == FROM_MERGE)
		hook_arg1 = "merge";
	else if (whence == FROM_CHERRY_PICK) {
		hook_arg1 = "commit";
		hook_arg2 = "CHERRY_PICK_HEAD";
	}

	if (squash_message) {
		/*
		 * If squash_commit was used for the commit subject,
		 * then we're possibly hijacking other commit log options.
		 * Reset the hook args to tell the real story.
		 */
		hook_arg1 = "message";
		hook_arg2 = "";
	}

	s->fp = fopen_for_writing(git_path_commit_editmsg());
	if (s->fp == NULL)
		die_errno(_("could not open '%s'"), git_path_commit_editmsg());

	/* Ignore status.displayCommentPrefix: we do need comments in COMMIT_EDITMSG. */
	old_display_comment_prefix = s->display_comment_prefix;
	s->display_comment_prefix = 1;

	/*
	 * Most hints are counter-productive when the commit has
	 * already started.
	 */
	s->hints = 0;

	if (clean_message_contents)
		strbuf_stripspace(&sb, 0);

	if (signoff)
		append_signoff(&sb, ignore_non_trailer(sb.buf, sb.len), 0);

	if (fwrite(sb.buf, 1, sb.len, s->fp) < sb.len)
		die_errno(_("could not write commit template"));

	if (auto_comment_line_char)
		adjust_comment_line_char(&sb);
	strbuf_release(&sb);

	/* This checks if committer ident is explicitly given */
	strbuf_addstr(&committer_ident, git_committer_info(IDENT_STRICT));
	if (use_editor && include_status) {
		int ident_shown = 0;
		int saved_color_setting;
		struct ident_split ci, ai;

		if (whence != FROM_COMMIT) {
			if (cleanup_mode == CLEANUP_SCISSORS)
				wt_status_add_cut_line(s->fp);
			status_printf_ln(s, GIT_COLOR_NORMAL,
			    whence == FROM_MERGE
				? _("\n"
					"It looks like you may be committing a merge.\n"
					"If this is not correct, please remove the file\n"
					"	%s\n"
					"and try again.\n")
				: _("\n"
					"It looks like you may be committing a cherry-pick.\n"
					"If this is not correct, please remove the file\n"
					"	%s\n"
					"and try again.\n"),
				git_path(whence == FROM_MERGE
					 ? "MERGE_HEAD"
					 : "CHERRY_PICK_HEAD"));
		}

		fprintf(s->fp, "\n");
		if (cleanup_mode == CLEANUP_ALL)
			status_printf(s, GIT_COLOR_NORMAL,
				_("Please enter the commit message for your changes."
				  " Lines starting\nwith '%c' will be ignored, and an empty"
				  " message aborts the commit.\n"), comment_line_char);
		else if (cleanup_mode == CLEANUP_SCISSORS && whence == FROM_COMMIT)
			wt_status_add_cut_line(s->fp);
		else /* CLEANUP_SPACE, that is. */
			status_printf(s, GIT_COLOR_NORMAL,
				_("Please enter the commit message for your changes."
				  " Lines starting\n"
				  "with '%c' will be kept; you may remove them"
				  " yourself if you want to.\n"
				  "An empty message aborts the commit.\n"), comment_line_char);
		if (only_include_assumed)
			status_printf_ln(s, GIT_COLOR_NORMAL,
					"%s", only_include_assumed);

		/*
		 * These should never fail because they come from our own
		 * fmt_ident. They may fail the sane_ident test, but we know
		 * that the name and mail pointers will at least be valid,
		 * which is enough for our tests and printing here.
		 */
		assert_split_ident(&ai, author_ident);
		assert_split_ident(&ci, &committer_ident);

		if (ident_cmp(&ai, &ci))
			status_printf_ln(s, GIT_COLOR_NORMAL,
				_("%s"
				"Author:    %.*s <%.*s>"),
				ident_shown++ ? "" : "\n",
				(int)(ai.name_end - ai.name_begin), ai.name_begin,
				(int)(ai.mail_end - ai.mail_begin), ai.mail_begin);

		if (author_date_is_interesting())
			status_printf_ln(s, GIT_COLOR_NORMAL,
				_("%s"
				"Date:      %s"),
				ident_shown++ ? "" : "\n",
				show_ident_date(&ai, DATE_MODE(NORMAL)));

		if (!committer_ident_sufficiently_given())
			status_printf_ln(s, GIT_COLOR_NORMAL,
				_("%s"
				"Committer: %.*s <%.*s>"),
				ident_shown++ ? "" : "\n",
				(int)(ci.name_end - ci.name_begin), ci.name_begin,
				(int)(ci.mail_end - ci.mail_begin), ci.mail_begin);

		if (ident_shown)
			status_printf_ln(s, GIT_COLOR_NORMAL, "%s", "");

		saved_color_setting = s->use_color;
		s->use_color = 0;
		commitable = run_status(s->fp, index_file, prefix, 1, s);
		s->use_color = saved_color_setting;
	} else {
		struct object_id oid;
		const char *parent = "HEAD";

		if (!active_nr && read_cache() < 0)
			die(_("Cannot read index"));

		if (amend)
			parent = "HEAD^1";

		if (get_sha1(parent, oid.hash)) {
			int i, ita_nr = 0;

			for (i = 0; i < active_nr; i++)
				if (ce_intent_to_add(active_cache[i]))
					ita_nr++;
			commitable = active_nr - ita_nr > 0;
		} else {
			/*
			 * Unless the user did explicitly request a submodule
			 * ignore mode by passing a command line option we do
			 * not ignore any changed submodule SHA-1s when
			 * comparing index and parent, no matter what is
			 * configured. Otherwise we won't commit any
			 * submodules which were manually staged, which would
			 * be really confusing.
			 */
			int diff_flags = DIFF_OPT_OVERRIDE_SUBMODULE_CONFIG;
			if (ignore_submodule_arg &&
			    !strcmp(ignore_submodule_arg, "all"))
				diff_flags |= DIFF_OPT_IGNORE_SUBMODULES;
			commitable = index_differs_from(parent, diff_flags, 1);
		}
	}
	strbuf_release(&committer_ident);

	fclose(s->fp);

	/*
	 * Reject an attempt to record a non-merge empty commit without
	 * explicit --allow-empty. In the cherry-pick case, it may be
	 * empty due to conflict resolution, which the user should okay.
	 */
	if (!commitable && whence != FROM_MERGE && !allow_empty &&
	    !(amend && is_a_merge(current_head))) {
		s->display_comment_prefix = old_display_comment_prefix;
		run_status(stdout, index_file, prefix, 0, s);
		if (amend)
			fputs(_(empty_amend_advice), stderr);
		else if (whence == FROM_CHERRY_PICK) {
			fputs(_(empty_cherry_pick_advice), stderr);
			if (!sequencer_in_use)
				fputs(_(empty_cherry_pick_advice_single), stderr);
			else
				fputs(_(empty_cherry_pick_advice_multi), stderr);
		}
		return 0;
	}

	/*
	 * Re-read the index as pre-commit hook could have updated it,
	 * and write it out as a tree.  We must do this before we invoke
	 * the editor and after we invoke run_status above.
	 */
	discard_cache();
	read_cache_from(index_file);
	if (update_main_cache_tree(0)) {
		error(_("Error building trees"));
		return 0;
	}

	if (run_commit_hook(use_editor, index_file, "prepare-commit-msg",
			    git_path_commit_editmsg(), hook_arg1, hook_arg2, NULL))
		return 0;

	if (use_editor) {
		struct argv_array env = ARGV_ARRAY_INIT;

		argv_array_pushf(&env, "GIT_INDEX_FILE=%s", index_file);
		if (launch_editor(git_path_commit_editmsg(), NULL, env.argv)) {
			fprintf(stderr,
			_("Please supply the message using either -m or -F option.\n"));
			exit(1);
		}
		argv_array_clear(&env);
	}

	if (!no_verify &&
	    run_commit_hook(use_editor, index_file, "commit-msg", git_path_commit_editmsg(), NULL)) {
		return 0;
	}

	return 1;
}

static int rest_is_empty(struct strbuf *sb, int start)
{
	int i, eol;
	const char *nl;

	/* Check if the rest is just whitespace and Signed-of-by's. */
	for (i = start; i < sb->len; i++) {
		nl = memchr(sb->buf + i, '\n', sb->len - i);
		if (nl)
			eol = nl - sb->buf;
		else
			eol = sb->len;

		if (strlen(sign_off_header) <= eol - i &&
		    starts_with(sb->buf + i, sign_off_header)) {
			i = eol;
			continue;
		}
		while (i < eol)
			if (!isspace(sb->buf[i++]))
				return 0;
	}

	return 1;
}

/*
 * Find out if the message in the strbuf contains only whitespace and
 * Signed-off-by lines.
 */
static int message_is_empty(struct strbuf *sb)
{
	if (cleanup_mode == CLEANUP_NONE && sb->len)
		return 0;
	return rest_is_empty(sb, 0);
}

/*
 * See if the user edited the message in the editor or left what
 * was in the template intact
 */
static int template_untouched(struct strbuf *sb)
{
	struct strbuf tmpl = STRBUF_INIT;
	const char *start;

	if (cleanup_mode == CLEANUP_NONE && sb->len)
		return 0;

	if (!template_file || strbuf_read_file(&tmpl, template_file, 0) <= 0)
		return 0;

	strbuf_stripspace(&tmpl, cleanup_mode == CLEANUP_ALL);
	if (!skip_prefix(sb->buf, tmpl.buf, &start))
		start = sb->buf;
	strbuf_release(&tmpl);
	return rest_is_empty(sb, start - sb->buf);
}

static const char *find_author_by_nickname(const char *name)
{
	struct rev_info revs;
	struct commit *commit;
	struct strbuf buf = STRBUF_INIT;
	struct string_list mailmap = STRING_LIST_INIT_NODUP;
	const char *av[20];
	int ac = 0;

	init_revisions(&revs, NULL);
	strbuf_addf(&buf, "--author=%s", name);
	av[++ac] = "--all";
	av[++ac] = "-i";
	av[++ac] = buf.buf;
	av[++ac] = NULL;
	setup_revisions(ac, av, &revs, NULL);
	revs.mailmap = &mailmap;
	read_mailmap(revs.mailmap, NULL);

	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));
	commit = get_revision(&revs);
	if (commit) {
		struct pretty_print_context ctx = {0};
		ctx.date_mode.type = DATE_NORMAL;
		strbuf_release(&buf);
		format_commit_message(commit, "%aN <%aE>", &buf, &ctx);
		clear_mailmap(&mailmap);
		return strbuf_detach(&buf, NULL);
	}
	die(_("--author '%s' is not 'Name <email>' and matches no existing author"), name);
}


static void handle_untracked_files_arg(struct wt_status *s)
{
	if (!untracked_files_arg)
		; /* default already initialized */
	else if (!strcmp(untracked_files_arg, "no"))
		s->show_untracked_files = SHOW_NO_UNTRACKED_FILES;
	else if (!strcmp(untracked_files_arg, "normal"))
		s->show_untracked_files = SHOW_NORMAL_UNTRACKED_FILES;
	else if (!strcmp(untracked_files_arg, "all"))
		s->show_untracked_files = SHOW_ALL_UNTRACKED_FILES;
	else
		die(_("Invalid untracked files mode '%s'"), untracked_files_arg);
}

static const char *read_commit_message(const char *name)
{
	const char *out_enc;
	struct commit *commit;

	commit = lookup_commit_reference_by_name(name);
	if (!commit)
		die(_("could not lookup commit %s"), name);
	out_enc = get_commit_output_encoding();
	return logmsg_reencode(commit, NULL, out_enc);
}

/*
 * Enumerate what needs to be propagated when --porcelain
 * is not in effect here.
 */
static struct status_deferred_config {
	enum wt_status_format status_format;
	int show_branch;
} status_deferred_config = {
	STATUS_FORMAT_UNSPECIFIED,
	-1 /* unspecified */
};

static void finalize_deferred_config(struct wt_status *s)
{
	int use_deferred_config = (status_format != STATUS_FORMAT_PORCELAIN &&
				   status_format != STATUS_FORMAT_PORCELAIN_V2 &&
				   !s->null_termination);

	if (s->null_termination) {
		if (status_format == STATUS_FORMAT_NONE ||
		    status_format == STATUS_FORMAT_UNSPECIFIED)
			status_format = STATUS_FORMAT_PORCELAIN;
		else if (status_format == STATUS_FORMAT_LONG)
			die(_("--long and -z are incompatible"));
	}

	if (use_deferred_config && status_format == STATUS_FORMAT_UNSPECIFIED)
		status_format = status_deferred_config.status_format;
	if (status_format == STATUS_FORMAT_UNSPECIFIED)
		status_format = STATUS_FORMAT_NONE;

	if (use_deferred_config && s->show_branch < 0)
		s->show_branch = status_deferred_config.show_branch;
	if (s->show_branch < 0)
		s->show_branch = 0;
}

static int parse_and_validate_options(int argc, const char *argv[],
				      const struct option *options,
				      const char * const usage[],
				      const char *prefix,
				      struct commit *current_head,
				      struct wt_status *s)
{
	int f = 0;

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	finalize_deferred_config(s);

	if (force_author && !strchr(force_author, '>'))
		force_author = find_author_by_nickname(force_author);

	if (force_author && renew_authorship)
		die(_("Using both --reset-author and --author does not make sense"));

	if (logfile || have_option_m || use_message || fixup_message)
		use_editor = 0;
	if (0 <= edit_flag)
		use_editor = edit_flag;

	/* Sanity check options */
	if (amend && !current_head)
		die(_("You have nothing to amend."));
	if (amend && whence != FROM_COMMIT) {
		if (whence == FROM_MERGE)
			die(_("You are in the middle of a merge -- cannot amend."));
		else if (whence == FROM_CHERRY_PICK)
			die(_("You are in the middle of a cherry-pick -- cannot amend."));
	}
	if (fixup_message && squash_message)
		die(_("Options --squash and --fixup cannot be used together"));
	if (use_message)
		f++;
	if (edit_message)
		f++;
	if (fixup_message)
		f++;
	if (logfile)
		f++;
	if (f > 1)
		die(_("Only one of -c/-C/-F/--fixup can be used."));
	if (have_option_m && f > 0)
		die((_("Option -m cannot be combined with -c/-C/-F/--fixup.")));
	if (f || have_option_m)
		template_file = NULL;
	if (edit_message)
		use_message = edit_message;
	if (amend && !use_message && !fixup_message)
		use_message = "HEAD";
	if (!use_message && whence != FROM_CHERRY_PICK && renew_authorship)
		die(_("--reset-author can be used only with -C, -c or --amend."));
	if (use_message) {
		use_message_buffer = read_commit_message(use_message);
		if (!renew_authorship) {
			author_message = use_message;
			author_message_buffer = use_message_buffer;
		}
	}
	if (whence == FROM_CHERRY_PICK && !renew_authorship) {
		author_message = "CHERRY_PICK_HEAD";
		author_message_buffer = read_commit_message(author_message);
	}

	if (patch_interactive)
		interactive = 1;

	if (also + only + all + interactive > 1)
		die(_("Only one of --include/--only/--all/--interactive/--patch can be used."));
	if (argc == 0 && (also || (only && !amend && !allow_empty)))
		die(_("No paths with --include/--only does not make sense."));
	if (argc > 0 && !also && !only)
		only_include_assumed = _("Explicit paths specified without -i or -o; assuming --only paths...");
	if (!cleanup_arg || !strcmp(cleanup_arg, "default"))
		cleanup_mode = use_editor ? CLEANUP_ALL : CLEANUP_SPACE;
	else if (!strcmp(cleanup_arg, "verbatim"))
		cleanup_mode = CLEANUP_NONE;
	else if (!strcmp(cleanup_arg, "whitespace"))
		cleanup_mode = CLEANUP_SPACE;
	else if (!strcmp(cleanup_arg, "strip"))
		cleanup_mode = CLEANUP_ALL;
	else if (!strcmp(cleanup_arg, "scissors"))
		cleanup_mode = use_editor ? CLEANUP_SCISSORS : CLEANUP_SPACE;
	else
		die(_("Invalid cleanup mode %s"), cleanup_arg);

	handle_untracked_files_arg(s);

	if (all && argc > 0)
		die(_("Paths with -a does not make sense."));

	if (status_format != STATUS_FORMAT_NONE)
		dry_run = 1;

	return argc;
}

static int dry_run_commit(int argc, const char **argv, const char *prefix,
			  const struct commit *current_head, struct wt_status *s)
{
	int commitable;
	const char *index_file;

	index_file = prepare_index(argc, argv, prefix, current_head, 1);
	commitable = run_status(stdout, index_file, prefix, 0, s);
	rollback_index_files();

	return commitable ? 0 : 1;
}

static int parse_status_slot(const char *slot)
{
	if (!strcasecmp(slot, "header"))
		return WT_STATUS_HEADER;
	if (!strcasecmp(slot, "branch"))
		return WT_STATUS_ONBRANCH;
	if (!strcasecmp(slot, "updated") || !strcasecmp(slot, "added"))
		return WT_STATUS_UPDATED;
	if (!strcasecmp(slot, "changed"))
		return WT_STATUS_CHANGED;
	if (!strcasecmp(slot, "untracked"))
		return WT_STATUS_UNTRACKED;
	if (!strcasecmp(slot, "nobranch"))
		return WT_STATUS_NOBRANCH;
	if (!strcasecmp(slot, "unmerged"))
		return WT_STATUS_UNMERGED;
	return -1;
}

static int git_status_config(const char *k, const char *v, void *cb)
{
	struct wt_status *s = cb;
	const char *slot_name;

	if (starts_with(k, "column."))
		return git_column_config(k, v, "status", &s->colopts);
	if (!strcmp(k, "status.submodulesummary")) {
		int is_bool;
		s->submodule_summary = git_config_bool_or_int(k, v, &is_bool);
		if (is_bool && s->submodule_summary)
			s->submodule_summary = -1;
		return 0;
	}
	if (!strcmp(k, "status.short")) {
		if (git_config_bool(k, v))
			status_deferred_config.status_format = STATUS_FORMAT_SHORT;
		else
			status_deferred_config.status_format = STATUS_FORMAT_NONE;
		return 0;
	}
	if (!strcmp(k, "status.branch")) {
		status_deferred_config.show_branch = git_config_bool(k, v);
		return 0;
	}
	if (!strcmp(k, "status.color") || !strcmp(k, "color.status")) {
		s->use_color = git_config_colorbool(k, v);
		return 0;
	}
	if (!strcmp(k, "status.displaycommentprefix")) {
		s->display_comment_prefix = git_config_bool(k, v);
		return 0;
	}
	if (skip_prefix(k, "status.color.", &slot_name) ||
	    skip_prefix(k, "color.status.", &slot_name)) {
		int slot = parse_status_slot(slot_name);
		if (slot < 0)
			return 0;
		if (!v)
			return config_error_nonbool(k);
		return color_parse(v, s->color_palette[slot]);
	}
	if (!strcmp(k, "status.relativepaths")) {
		s->relative_paths = git_config_bool(k, v);
		return 0;
	}
	if (!strcmp(k, "status.showuntrackedfiles")) {
		if (!v)
			return config_error_nonbool(k);
		else if (!strcmp(v, "no"))
			s->show_untracked_files = SHOW_NO_UNTRACKED_FILES;
		else if (!strcmp(v, "normal"))
			s->show_untracked_files = SHOW_NORMAL_UNTRACKED_FILES;
		else if (!strcmp(v, "all"))
			s->show_untracked_files = SHOW_ALL_UNTRACKED_FILES;
		else
			return error(_("Invalid untracked files mode '%s'"), v);
		return 0;
	}
	return git_diff_ui_config(k, v, NULL);
}

int cmd_status(int argc, const char **argv, const char *prefix)
{
	static struct wt_status s;
	int fd;
	struct object_id oid;
	static struct option builtin_status_options[] = {
		OPT__VERBOSE(&verbose, N_("be verbose")),
		OPT_SET_INT('s', "short", &status_format,
			    N_("show status concisely"), STATUS_FORMAT_SHORT),
		OPT_BOOL('b', "branch", &s.show_branch,
			 N_("show branch information")),
		{ OPTION_CALLBACK, 0, "porcelain", &status_format,
		  N_("version"), N_("machine-readable output"),
		  PARSE_OPT_OPTARG, opt_parse_porcelain },
		OPT_SET_INT(0, "long", &status_format,
			    N_("show status in long format (default)"),
			    STATUS_FORMAT_LONG),
		OPT_BOOL('z', "null", &s.null_termination,
			 N_("terminate entries with NUL")),
		{ OPTION_STRING, 'u', "untracked-files", &untracked_files_arg,
		  N_("mode"),
		  N_("show untracked files, optional modes: all, normal, no. (Default: all)"),
		  PARSE_OPT_OPTARG, NULL, (intptr_t)"all" },
		OPT_BOOL(0, "ignored", &show_ignored_in_status,
			 N_("show ignored files")),
		{ OPTION_STRING, 0, "ignore-submodules", &ignore_submodule_arg, N_("when"),
		  N_("ignore changes to submodules, optional when: all, dirty, untracked. (Default: all)"),
		  PARSE_OPT_OPTARG, NULL, (intptr_t)"all" },
		OPT_COLUMN(0, "column", &s.colopts, N_("list untracked files in columns")),
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_status_usage, builtin_status_options);

	status_init_config(&s, git_status_config);
	argc = parse_options(argc, argv, prefix,
			     builtin_status_options,
			     builtin_status_usage, 0);
	finalize_colopts(&s.colopts, -1);
	finalize_deferred_config(&s);

	handle_untracked_files_arg(&s);
	if (show_ignored_in_status)
		s.show_ignored_files = 1;
	parse_pathspec(&s.pathspec, 0,
		       PATHSPEC_PREFER_FULL,
		       prefix, argv);

	read_cache_preload(&s.pathspec);
	refresh_index(&the_index, REFRESH_QUIET|REFRESH_UNMERGED, &s.pathspec, NULL, NULL);

	fd = hold_locked_index(&index_lock, 0);

	s.is_initial = get_sha1(s.reference, oid.hash) ? 1 : 0;
	if (!s.is_initial)
		hashcpy(s.sha1_commit, oid.hash);

	s.ignore_submodule_arg = ignore_submodule_arg;
	s.status_format = status_format;
	s.verbose = verbose;

	wt_status_collect(&s);

	if (0 <= fd)
		update_index_if_able(&the_index, &index_lock);

	if (s.relative_paths)
		s.prefix = prefix;

	wt_status_print(&s);
	return 0;
}

static const char *implicit_ident_advice(void)
{
	char *user_config = expand_user_path("~/.gitconfig");
	char *xdg_config = xdg_config_home("config");
	int config_exists = file_exists(user_config) || file_exists(xdg_config);

	free(user_config);
	free(xdg_config);

	if (config_exists)
		return _(implicit_ident_advice_config);
	else
		return _(implicit_ident_advice_noconfig);

}

static void print_summary(const char *prefix, const struct object_id *oid,
			  int initial_commit)
{
	struct rev_info rev;
	struct commit *commit;
	struct strbuf format = STRBUF_INIT;
	struct object_id junk_oid;
	const char *head;
	struct pretty_print_context pctx = {0};
	struct strbuf author_ident = STRBUF_INIT;
	struct strbuf committer_ident = STRBUF_INIT;

	commit = lookup_commit(oid->hash);
	if (!commit)
		die(_("couldn't look up newly created commit"));
	if (parse_commit(commit))
		die(_("could not parse newly created commit"));

	strbuf_addstr(&format, "format:%h] %s");

	format_commit_message(commit, "%an <%ae>", &author_ident, &pctx);
	format_commit_message(commit, "%cn <%ce>", &committer_ident, &pctx);
	if (strbuf_cmp(&author_ident, &committer_ident)) {
		strbuf_addstr(&format, "\n Author: ");
		strbuf_addbuf_percentquote(&format, &author_ident);
	}
	if (author_date_is_interesting()) {
		struct strbuf date = STRBUF_INIT;
		format_commit_message(commit, "%ad", &date, &pctx);
		strbuf_addstr(&format, "\n Date: ");
		strbuf_addbuf_percentquote(&format, &date);
		strbuf_release(&date);
	}
	if (!committer_ident_sufficiently_given()) {
		strbuf_addstr(&format, "\n Committer: ");
		strbuf_addbuf_percentquote(&format, &committer_ident);
		if (advice_implicit_identity) {
			strbuf_addch(&format, '\n');
			strbuf_addstr(&format, implicit_ident_advice());
		}
	}
	strbuf_release(&author_ident);
	strbuf_release(&committer_ident);

	init_revisions(&rev, prefix);
	setup_revisions(0, NULL, &rev, NULL);

	rev.diff = 1;
	rev.diffopt.output_format =
		DIFF_FORMAT_SHORTSTAT | DIFF_FORMAT_SUMMARY;

	rev.verbose_header = 1;
	rev.show_root_diff = 1;
	get_commit_format(format.buf, &rev);
	rev.always_show_header = 0;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.break_opt = 0;
	diff_setup_done(&rev.diffopt);

	head = resolve_ref_unsafe("HEAD", 0, junk_oid.hash, NULL);
	if (!strcmp(head, "HEAD"))
		head = _("detached HEAD");
	else
		skip_prefix(head, "refs/heads/", &head);
	printf("[%s%s ", head, initial_commit ? _(" (root-commit)") : "");

	if (!log_tree_commit(&rev, commit)) {
		rev.always_show_header = 1;
		rev.use_terminator = 1;
		log_tree_commit(&rev, commit);
	}

	strbuf_release(&format);
}

static int git_commit_config(const char *k, const char *v, void *cb)
{
	struct wt_status *s = cb;
	int status;

	if (!strcmp(k, "commit.template"))
		return git_config_pathname(&template_file, k, v);
	if (!strcmp(k, "commit.status")) {
		include_status = git_config_bool(k, v);
		return 0;
	}
	if (!strcmp(k, "commit.cleanup"))
		return git_config_string(&cleanup_arg, k, v);
	if (!strcmp(k, "commit.gpgsign")) {
		sign_commit = git_config_bool(k, v) ? "" : NULL;
		return 0;
	}
	if (!strcmp(k, "commit.verbose")) {
		int is_bool;
		config_commit_verbose = git_config_bool_or_int(k, v, &is_bool);
		return 0;
	}

	status = git_gpg_config(k, v, NULL);
	if (status)
		return status;
	return git_status_config(k, v, s);
}

static int run_rewrite_hook(const struct object_id *oldoid,
			    const struct object_id *newoid)
{
	struct child_process proc = CHILD_PROCESS_INIT;
	const char *argv[3];
	int code;
	struct strbuf sb = STRBUF_INIT;

	argv[0] = find_hook("post-rewrite");
	if (!argv[0])
		return 0;

	argv[1] = "amend";
	argv[2] = NULL;

	proc.argv = argv;
	proc.in = -1;
	proc.stdout_to_stderr = 1;

	code = start_command(&proc);
	if (code)
		return code;
	strbuf_addf(&sb, "%s %s\n", oid_to_hex(oldoid), oid_to_hex(newoid));
	sigchain_push(SIGPIPE, SIG_IGN);
	write_in_full(proc.in, sb.buf, sb.len);
	close(proc.in);
	strbuf_release(&sb);
	sigchain_pop(SIGPIPE);
	return finish_command(&proc);
}

int run_commit_hook(int editor_is_used, const char *index_file, const char *name, ...)
{
	struct argv_array hook_env = ARGV_ARRAY_INIT;
	va_list args;
	int ret;

	argv_array_pushf(&hook_env, "GIT_INDEX_FILE=%s", index_file);

	/*
	 * Let the hook know that no editor will be launched.
	 */
	if (!editor_is_used)
		argv_array_push(&hook_env, "GIT_EDITOR=:");

	va_start(args, name);
	ret = run_hook_ve(hook_env.argv,name, args);
	va_end(args);
	argv_array_clear(&hook_env);

	return ret;
}

int cmd_commit(int argc, const char **argv, const char *prefix)
{
	static struct wt_status s;
	static struct option builtin_commit_options[] = {
		OPT__QUIET(&quiet, N_("suppress summary after successful commit")),
		OPT__VERBOSE(&verbose, N_("show diff in commit message template")),

		OPT_GROUP(N_("Commit message options")),
		OPT_FILENAME('F', "file", &logfile, N_("read message from file")),
		OPT_STRING(0, "author", &force_author, N_("author"), N_("override author for commit")),
		OPT_STRING(0, "date", &force_date, N_("date"), N_("override date for commit")),
		OPT_CALLBACK('m', "message", &message, N_("message"), N_("commit message"), opt_parse_m),
		OPT_STRING('c', "reedit-message", &edit_message, N_("commit"), N_("reuse and edit message from specified commit")),
		OPT_STRING('C', "reuse-message", &use_message, N_("commit"), N_("reuse message from specified commit")),
		OPT_STRING(0, "fixup", &fixup_message, N_("commit"), N_("use autosquash formatted message to fixup specified commit")),
		OPT_STRING(0, "squash", &squash_message, N_("commit"), N_("use autosquash formatted message to squash specified commit")),
		OPT_BOOL(0, "reset-author", &renew_authorship, N_("the commit is authored by me now (used with -C/-c/--amend)")),
		OPT_BOOL('s', "signoff", &signoff, N_("add Signed-off-by:")),
		OPT_FILENAME('t', "template", &template_file, N_("use specified template file")),
		OPT_BOOL('e', "edit", &edit_flag, N_("force edit of commit")),
		OPT_STRING(0, "cleanup", &cleanup_arg, N_("default"), N_("how to strip spaces and #comments from message")),
		OPT_BOOL(0, "status", &include_status, N_("include status in commit message template")),
		{ OPTION_STRING, 'S', "gpg-sign", &sign_commit, N_("key-id"),
		  N_("GPG sign commit"), PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
		/* end commit message options */

		OPT_GROUP(N_("Commit contents options")),
		OPT_BOOL('a', "all", &all, N_("commit all changed files")),
		OPT_BOOL('i', "include", &also, N_("add specified files to index for commit")),
		OPT_BOOL(0, "interactive", &interactive, N_("interactively add files")),
		OPT_BOOL('p', "patch", &patch_interactive, N_("interactively add changes")),
		OPT_BOOL('o', "only", &only, N_("commit only specified files")),
		OPT_BOOL('n', "no-verify", &no_verify, N_("bypass pre-commit and commit-msg hooks")),
		OPT_BOOL(0, "dry-run", &dry_run, N_("show what would be committed")),
		OPT_SET_INT(0, "short", &status_format, N_("show status concisely"),
			    STATUS_FORMAT_SHORT),
		OPT_BOOL(0, "branch", &s.show_branch, N_("show branch information")),
		OPT_SET_INT(0, "porcelain", &status_format,
			    N_("machine-readable output"), STATUS_FORMAT_PORCELAIN),
		OPT_SET_INT(0, "long", &status_format,
			    N_("show status in long format (default)"),
			    STATUS_FORMAT_LONG),
		OPT_BOOL('z', "null", &s.null_termination,
			 N_("terminate entries with NUL")),
		OPT_BOOL(0, "amend", &amend, N_("amend previous commit")),
		OPT_BOOL(0, "no-post-rewrite", &no_post_rewrite, N_("bypass post-rewrite hook")),
		{ OPTION_STRING, 'u', "untracked-files", &untracked_files_arg, N_("mode"), N_("show untracked files, optional modes: all, normal, no. (Default: all)"), PARSE_OPT_OPTARG, NULL, (intptr_t)"all" },
		/* end commit contents options */

		OPT_HIDDEN_BOOL(0, "allow-empty", &allow_empty,
				N_("ok to record an empty change")),
		OPT_HIDDEN_BOOL(0, "allow-empty-message", &allow_empty_message,
				N_("ok to record a change with an empty message")),

		OPT_END()
	};

	struct strbuf sb = STRBUF_INIT;
	struct strbuf author_ident = STRBUF_INIT;
	const char *index_file, *reflog_msg;
	char *nl;
	struct object_id oid;
	struct commit_list *parents = NULL;
	struct stat statbuf;
	struct commit *current_head = NULL;
	struct commit_extra_header *extra = NULL;
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_commit_usage, builtin_commit_options);

	status_init_config(&s, git_commit_config);
	status_format = STATUS_FORMAT_NONE; /* Ignore status.short */
	s.colopts = 0;

	if (get_sha1("HEAD", oid.hash))
		current_head = NULL;
	else {
		current_head = lookup_commit_or_die(oid.hash, "HEAD");
		if (parse_commit(current_head))
			die(_("could not parse HEAD commit"));
	}
	verbose = -1; /* unspecified */
	argc = parse_and_validate_options(argc, argv, builtin_commit_options,
					  builtin_commit_usage,
					  prefix, current_head, &s);
	if (verbose == -1)
		verbose = (config_commit_verbose < 0) ? 0 : config_commit_verbose;

	if (dry_run)
		return dry_run_commit(argc, argv, prefix, current_head, &s);
	index_file = prepare_index(argc, argv, prefix, current_head, 0);

	/* Set up everything for writing the commit object.  This includes
	   running hooks, writing the trees, and interacting with the user.  */
	if (!prepare_to_commit(index_file, prefix,
			       current_head, &s, &author_ident)) {
		rollback_index_files();
		return 1;
	}

	/* Determine parents */
	reflog_msg = getenv("GIT_REFLOG_ACTION");
	if (!current_head) {
		if (!reflog_msg)
			reflog_msg = "commit (initial)";
	} else if (amend) {
		if (!reflog_msg)
			reflog_msg = "commit (amend)";
		parents = copy_commit_list(current_head->parents);
	} else if (whence == FROM_MERGE) {
		struct strbuf m = STRBUF_INIT;
		FILE *fp;
		int allow_fast_forward = 1;
		struct commit_list **pptr = &parents;

		if (!reflog_msg)
			reflog_msg = "commit (merge)";
		pptr = commit_list_append(current_head, pptr);
		fp = fopen(git_path_merge_head(), "r");
		if (fp == NULL)
			die_errno(_("could not open '%s' for reading"),
				  git_path_merge_head());
		while (strbuf_getline_lf(&m, fp) != EOF) {
			struct commit *parent;

			parent = get_merge_parent(m.buf);
			if (!parent)
				die(_("Corrupt MERGE_HEAD file (%s)"), m.buf);
			pptr = commit_list_append(parent, pptr);
		}
		fclose(fp);
		strbuf_release(&m);
		if (!stat(git_path_merge_mode(), &statbuf)) {
			if (strbuf_read_file(&sb, git_path_merge_mode(), 0) < 0)
				die_errno(_("could not read MERGE_MODE"));
			if (!strcmp(sb.buf, "no-ff"))
				allow_fast_forward = 0;
		}
		if (allow_fast_forward)
			parents = reduce_heads(parents);
	} else {
		if (!reflog_msg)
			reflog_msg = (whence == FROM_CHERRY_PICK)
					? "commit (cherry-pick)"
					: "commit";
		commit_list_insert(current_head, &parents);
	}

	/* Finally, get the commit message */
	strbuf_reset(&sb);
	if (strbuf_read_file(&sb, git_path_commit_editmsg(), 0) < 0) {
		int saved_errno = errno;
		rollback_index_files();
		die(_("could not read commit message: %s"), strerror(saved_errno));
	}

	if (verbose || /* Truncate the message just before the diff, if any. */
	    cleanup_mode == CLEANUP_SCISSORS)
		wt_status_truncate_message_at_cut_line(&sb);

	if (cleanup_mode != CLEANUP_NONE)
		strbuf_stripspace(&sb, cleanup_mode == CLEANUP_ALL);
	if (template_untouched(&sb) && !allow_empty_message) {
		rollback_index_files();
		fprintf(stderr, _("Aborting commit; you did not edit the message.\n"));
		exit(1);
	}
	if (message_is_empty(&sb) && !allow_empty_message) {
		rollback_index_files();
		fprintf(stderr, _("Aborting commit due to empty commit message.\n"));
		exit(1);
	}

	if (amend) {
		const char *exclude_gpgsig[2] = { "gpgsig", NULL };
		extra = read_commit_extra_headers(current_head, exclude_gpgsig);
	} else {
		struct commit_extra_header **tail = &extra;
		append_merge_tag_headers(parents, &tail);
	}

	if (commit_tree_extended(sb.buf, sb.len, active_cache_tree->sha1,
			 parents, oid.hash, author_ident.buf, sign_commit, extra)) {
		rollback_index_files();
		die(_("failed to write commit object"));
	}
	strbuf_release(&author_ident);
	free_commit_extra_headers(extra);

	nl = strchr(sb.buf, '\n');
	if (nl)
		strbuf_setlen(&sb, nl + 1 - sb.buf);
	else
		strbuf_addch(&sb, '\n');
	strbuf_insert(&sb, 0, reflog_msg, strlen(reflog_msg));
	strbuf_insert(&sb, strlen(reflog_msg), ": ", 2);

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, "HEAD", oid.hash,
				   current_head
				   ? current_head->object.oid.hash : null_sha1,
				   0, sb.buf, &err) ||
	    ref_transaction_commit(transaction, &err)) {
		rollback_index_files();
		die("%s", err.buf);
	}
	ref_transaction_free(transaction);

	unlink(git_path_cherry_pick_head());
	unlink(git_path_revert_head());
	unlink(git_path_merge_head());
	unlink(git_path_merge_msg());
	unlink(git_path_merge_mode());
	unlink(git_path_squash_msg());

	if (commit_index_files())
		die (_("Repository has been updated, but unable to write\n"
		     "new_index file. Check that disk is not full and quota is\n"
		     "not exceeded, and then \"git reset HEAD\" to recover."));

	rerere(0);
	run_commit_hook(use_editor, get_index_file(), "post-commit", NULL);
	if (amend && !no_post_rewrite) {
		struct notes_rewrite_cfg *cfg;
		cfg = init_copy_notes_for_rewrite("amend");
		if (cfg) {
			/* we are amending, so current_head is not NULL */
			copy_note_for_rewrite(cfg, current_head->object.oid.hash, oid.hash);
			finish_copy_notes_for_rewrite(cfg, "Notes added by 'git commit --amend'");
		}
		run_rewrite_hook(&current_head->object.oid, &oid);
	}
	if (!quiet)
		print_summary(prefix, &oid, !current_head);

	strbuf_release(&err);
	return 0;
}
