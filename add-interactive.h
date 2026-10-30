#ifndef ADD_INTERACTIVE_H
#define ADD_INTERACTIVE_H

#include "add-patch.h"

struct pathspec;
struct repository;

int run_add_i(struct repository *r, const struct pathspec *ps,
	      struct interactive_options *opts);

#endif
