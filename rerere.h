#ifndef RERERE_H
#define RERERE_H

#include "string-list.h"

extern int setup_rerere(struct string_list *);
extern int rerere(void);
extern const char *rerere_path(const char *hex, const char *file);
extern int has_rerere_resolution(const char *hex);

#endif
