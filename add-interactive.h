#ifndef ADD_INTERACTIVE_H
#define ADD_INTERACTIVE_H

struct repository;
struct pathspec;
int run_add_i(struct repository *r, const struct pathspec *ps);

#endif
