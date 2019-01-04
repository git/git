#include "cache.h"
#include "commit.h"
#include "rebase-interactive.h"
#include "sequencer.h"
#include "strbuf.h"

void append_todo_help(unsigned edit_todo, unsigned keep_empty,
		      struct strbuf *buf)
{
	const char *msg = _("\nCommands:\n"
"p, pick <commit> = use commit\n"
"r, reword <commit> = use commit, but edit the commit message\n"
"e, edit <commit> = use commit, but stop for amending\n"
"s, squash <commit> = use commit, but meld into previous commit\n"
"f, fixup <commit> = like \"squash\", but discard this commit's log message\n"
"x, exec <command> = run command (the rest of the line) using shell\n"
"b, break = stop here (continue rebase later with 'git rebase --continue')\n"
"d, drop <commit> = remove commit\n"
"l, label <label> = label current HEAD with a name\n"
"t, reset <label> = reset HEAD to a label\n"
"m, merge [-C <commit> | -c <commit>] <label> [# <oneline>]\n"
".       create a merge commit using the original merge commit's\n"
".       message (or the oneline, if no original merge commit was\n"
".       specified). Use -c <commit> to reword the commit message.\n"
"\n"
"These lines can be re-ordered; they are executed from top to bottom.\n");

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

	if (!keep_empty) {
		msg = _("Note that empty commits are commented out");
		strbuf_add_commented_lines(buf, msg, strlen(msg));
	}
}

int edit_todo_list(struct repository *r, unsigned flags)
{
	struct strbuf buf = STRBUF_INIT;
	const char *todo_file = rebase_path_todo();

	if (strbuf_read_file(&buf, todo_file, 0) < 0)
		return error_errno(_("could not read '%s'."), todo_file);

	strbuf_stripspace(&buf, 1);
	if (write_message(buf.buf, buf.len, todo_file, 0)) {
		strbuf_release(&buf);
		return -1;
	}

	strbuf_release(&buf);

	transform_todos(r, flags | TODO_LIST_SHORTEN_IDS);

	if (strbuf_read_file(&buf, todo_file, 0) < 0)
		return error_errno(_("could not read '%s'."), todo_file);

	append_todo_help(1, 0, &buf);
	if (write_message(buf.buf, buf.len, todo_file, 0)) {
		strbuf_release(&buf);
		return -1;
	}

	strbuf_release(&buf);

	if (launch_sequence_editor(todo_file, NULL, NULL))
		return -1;

	transform_todos(r, flags & ~(TODO_LIST_SHORTEN_IDS));

	return 0;
}
