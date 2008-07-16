#ifndef RERERE_H
#define RERERE_H

#include "path-list.h"

extern int setup_rerere(struct path_list *);
extern int rerere(void);

#endif
