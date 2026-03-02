#ifndef ADD_INTERACTIVE_H
#define ADD_INTERACTIVE_H

#include "add-patch.h"

struct pathspec;
struct repository;

struct add_i_state {
	struct repository *r;
	struct interactive_config cfg;
};

void init_add_i_state(struct add_i_state *s, struct repository *r,
		      struct interactive_options *opts);
void clear_add_i_state(struct add_i_state *s);

int run_add_i(struct repository *r, const struct pathspec *ps,
	      struct interactive_options *opts);

#endif
