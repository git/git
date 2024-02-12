#ifndef MATCH_TREES_H
#define MATCH_TREES_H

struct object_id;
struct repository;

void shift_tree(struct repository *, const struct object_id *, const struct object_id *, struct object_id *, int);
void shift_tree_by(struct repository *, const struct object_id *, const struct object_id *, struct object_id *, const char *);

#endif /* MATCH_TREES_H */
