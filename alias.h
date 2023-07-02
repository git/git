#ifndef ALIAS_H
#define ALIAS_H

struct strbuf;
struct string_list;

char *alias_lookup(const char *alias);
/* Quote argv so buf can be parsed by split_cmdline() */
void quote_cmdline(struct strbuf *buf, const char **argv);
int split_cmdline(char *cmdline, const char ***argv);
/* Takes a negative value returned by split_cmdline */
const char *split_cmdline_strerror(int cmdline_errno);
void list_aliases(struct string_list *list);

#endif
