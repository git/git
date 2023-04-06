#include "cache.h"
#include "commit.h"
#include "environment.h"
#include "gettext.h"
#include "sequencer.h"
#include "rebase-interactive.h"
#include "strbuf.h"
#include "commit-slab.h"
#include "config.h"
#include "dir.h"
#include "wrapper.h"

static const char edit_todo_list_advice[] =
N_("You can fix this with 'git rebase --edit-todo' "
"and then run 'git rebase --continue'.\n"
"Or you can abort the rebase with 'git rebase"
" --abort'.\n");

enum missing_commit_check_level {
	MISSING_COMMIT_CHECK_IGNORE = 0,
	MISSING_COMMIT_CHECK_WARN,
	MISSING_COMMIT_CHECK_ERROR
};

static enum missing_commit_check_level get_missing_commit_check_level(void)
{
	const char *value;

	if (git_config_get_value("rebase.missingcommitscheck", &value) ||
			!strcasecmp("ignore", value))
		return MISSING_COMMIT_CHECK_IGNORE;
	if (!strcasecmp("warn", value))
		return MISSING_COMMIT_CHECK_WARN;
	if (!strcasecmp("error", value))
		return MISSING_COMMIT_CHECK_ERROR;
	warning(_("unrecognized setting %s for option "
		  "rebase.missingCommitsCheck. Ignoring."), value);
	return MISSING_COMMIT_CHECK_IGNORE;
}

void append_todo_help(int command_count,
		      const char *shortrevisions, const char *shortonto,
		      struct strbuf *buf)
{
	const char *msg = _("\nCommands:\n"
"p, pick <commit> = use commit\n"
"r, reword <commit> = use commit, but edit the commit message\n"
"e, edit <commit> = use commit, but stop for amending\n"
"s, squash <commit> = use commit, but meld into previous commit\n"
"f, fixup [-C | -c] <commit> = like \"squash\" but keep only the previous\n"
"                   commit's log message, unless -C is used, in which case\n"
"                   keep only this commit's message; -c is same as -C but\n"
"                   opens the editor\n"
"x, exec <command> = run command (the rest of the line) using shell\n"
"b, break = stop here (continue rebase later with 'git rebase --continue')\n"
"d, drop <commit> = remove commit\n"
"l, label <label> = label current HEAD with a name\n"
"t, reset <label> = reset HEAD to a label\n"
"m, merge [-C <commit> | -c <commit>] <label> [# <oneline>]\n"
"        create a merge commit using the original merge commit's\n"
"        message (or the oneline, if no original merge commit was\n"
"        specified); use -c <commit> to reword the commit message\n"
"u, update-ref <ref> = track a placeholder for the <ref> to be updated\n"
"                      to this position in the new commits. The <ref> is\n"
"                      updated at the end of the rebase\n"
"\n"
"These lines can be re-ordered; they are executed from top to bottom.\n");
	unsigned edit_todo = !(shortrevisions && shortonto);

	if (!edit_todo) {
		strbuf_addch(buf, '\n');
		strbuf_commented_addf(buf, Q_("Rebase %s onto %s (%d command)",
					      "Rebase %s onto %s (%d commands)",
					      command_count),
				      shortrevisions, shortonto, command_count);
	}

	strbuf_add_commented_lines(buf, msg, strlen(msg));

	if (get_missing_commit_check_level() == MISSING_COMMIT_CHECK_ERROR)
		msg = _("\nDo not remove any line. Use 'drop' "
			 "explicitly to remove a commit.\n");
	else
		msg = _("\nIf you remove a line here "
			 "THAT COMMIT WILL BE LOST.\n");

	strbuf_add_commented_lines(buf, msg, strlen(msg));

	if (edit_todo)
		msg = _("\nYou are editing the todo file "
			"of an ongoing interactive rebase.\n"
			"To continue rebase after editing, run:\n"
			"    git rebase --continue\n\n");
	else
		msg = _("\nHowever, if you remove everything, "
			"the rebase will be aborted.\n\n");

	strbuf_add_commented_lines(buf, msg, strlen(msg));
}

int edit_todo_list(struct repository *r, struct todo_list *todo_list,
		   struct todo_list *new_todo, const char *shortrevisions,
		   const char *shortonto, unsigned flags)
{
	const char *todo_file = rebase_path_todo(),
		*todo_backup = rebase_path_todo_backup();
	unsigned initial = shortrevisions && shortonto;
	int incorrect = 0;

	/* If the user is editing the todo list, we first try to parse
	 * it.  If there is an error, we do not return, because the user
	 * might want to fix it in the first place. */
	if (!initial)
		incorrect = todo_list_parse_insn_buffer(r, todo_list->buf.buf, todo_list) |
			file_exists(rebase_path_dropped());

	if (todo_list_write_to_file(r, todo_list, todo_file, shortrevisions, shortonto,
				    -1, flags | TODO_LIST_SHORTEN_IDS | TODO_LIST_APPEND_TODO_HELP))
		return error_errno(_("could not write '%s'"), todo_file);

	if (!incorrect &&
	    todo_list_write_to_file(r, todo_list, todo_backup,
				    shortrevisions, shortonto, -1,
				    (flags | TODO_LIST_APPEND_TODO_HELP) & ~TODO_LIST_SHORTEN_IDS) < 0)
		return error(_("could not write '%s'."), rebase_path_todo_backup());

	if (launch_sequence_editor(todo_file, &new_todo->buf, NULL))
		return -2;

	strbuf_stripspace(&new_todo->buf, 1);
	if (initial && new_todo->buf.len == 0)
		return -3;

	if (todo_list_parse_insn_buffer(r, new_todo->buf.buf, new_todo)) {
		fprintf(stderr, _(edit_todo_list_advice));
		return -4;
	}

	if (incorrect) {
		if (todo_list_check_against_backup(r, new_todo)) {
			write_file(rebase_path_dropped(), "%s", "");
			return -4;
		}

		if (incorrect > 0)
			unlink(rebase_path_dropped());
	} else if (todo_list_check(todo_list, new_todo)) {
		write_file(rebase_path_dropped(), "%s", "");
		return -4;
	}

	/*
	 * See if branches need to be added or removed from the update-refs
	 * file based on the new todo list.
	 */
	todo_list_filter_update_refs(r, new_todo);

	return 0;
}

define_commit_slab(commit_seen, unsigned char);
/*
 * Check if the user dropped some commits by mistake
 * Behaviour determined by rebase.missingCommitsCheck.
 * Check if there is an unrecognized command or a
 * bad SHA-1 in a command.
 */
int todo_list_check(struct todo_list *old_todo, struct todo_list *new_todo)
{
	enum missing_commit_check_level check_level = get_missing_commit_check_level();
	struct strbuf missing = STRBUF_INIT;
	int res = 0, i;
	struct commit_seen commit_seen;

	init_commit_seen(&commit_seen);

	if (check_level == MISSING_COMMIT_CHECK_IGNORE)
		goto leave_check;

	/* Mark the commits in git-rebase-todo as seen */
	for (i = 0; i < new_todo->nr; i++) {
		struct commit *commit = new_todo->items[i].commit;
		if (commit)
			*commit_seen_at(&commit_seen, commit) = 1;
	}

	/* Find commits in git-rebase-todo.backup yet unseen */
	for (i = old_todo->nr - 1; i >= 0; i--) {
		struct todo_item *item = old_todo->items + i;
		struct commit *commit = item->commit;
		if (commit && !*commit_seen_at(&commit_seen, commit)) {
			strbuf_addf(&missing, " - %s %.*s\n",
				    repo_find_unique_abbrev(the_repository, &commit->object.oid, DEFAULT_ABBREV),
				    item->arg_len,
				    todo_item_get_arg(old_todo, item));
			*commit_seen_at(&commit_seen, commit) = 1;
		}
	}

	/* Warn about missing commits */
	if (!missing.len)
		goto leave_check;

	if (check_level == MISSING_COMMIT_CHECK_ERROR)
		res = 1;

	fprintf(stderr,
		_("Warning: some commits may have been dropped accidentally.\n"
		"Dropped commits (newer to older):\n"));

	/* Make the list user-friendly and display */
	fputs(missing.buf, stderr);
	strbuf_release(&missing);

	fprintf(stderr, _("To avoid this message, use \"drop\" to "
		"explicitly remove a commit.\n\n"
		"Use 'git config rebase.missingCommitsCheck' to change "
		"the level of warnings.\n"
		"The possible behaviours are: ignore, warn, error.\n\n"));

	fprintf(stderr, _(edit_todo_list_advice));

leave_check:
	clear_commit_seen(&commit_seen);
	return res;
}

int todo_list_check_against_backup(struct repository *r, struct todo_list *todo_list)
{
	struct todo_list backup = TODO_LIST_INIT;
	int res = 0;

	if (strbuf_read_file(&backup.buf, rebase_path_todo_backup(), 0) > 0) {
		todo_list_parse_insn_buffer(r, backup.buf.buf, &backup);
		res = todo_list_check(&backup, todo_list);
	}

	todo_list_release(&backup);
	return res;
}
