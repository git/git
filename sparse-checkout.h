#ifndef SPARSE_CHECKOUT_H
#define SPARSE_CHECKOUT_H

struct repository;

extern int opt_restrict_to_sparse_paths;

/* Whether or not cmds should restrict behavior on sparse paths, in this repo */
int restrict_to_sparse_paths(struct repository *repo);

#endif /* SPARSE_CHECKOUT_H */
