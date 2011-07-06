#ifndef RERERE_H
#define RERERE_H

#include "string-list.h"

extern int setup_rerere(struct string_list *);
extern int rerere(void);

#endif
