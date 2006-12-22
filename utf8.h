#ifndef GIT_UTF8_H
#define GIT_UTF8_H

int utf8_width(const char **start);
int is_utf8(const char *text);
void print_wrapped_text(const char *text, int indent, int indent2, int len);

#endif
