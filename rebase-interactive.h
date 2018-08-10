#ifndef REBASE_INTERACTIVE_H
#define REBASE_INTERACTIVE_H

void append_todo_help(unsigned edit_todo, unsigned keep_empty,
		      struct strbuf *buf);
int append_todo_help_to_file(unsigned edit_todo, unsigned keep_empty);
int edit_todo_list(unsigned flags);

#endif
