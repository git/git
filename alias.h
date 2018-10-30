#ifndef ALIAS_H
#define ALIAS_H

struct string_list;

char *alias_lookup(const char *alias);
int split_cmdline(char *cmdline, const char ***argv);
/* Takes a negative value returned by split_cmdline */
const char *split_cmdline_strerror(int cmdline_errno);
void list_aliases(struct string_list *list);

#endif
