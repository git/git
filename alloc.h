#ifndef ALLOC_H
#define ALLOC_H

struct alloc_state;
struct tree;
struct commit;
struct tag;
struct repository;

void *alloc_blob_node(struct repository *r);
void *alloc_tree_node(struct repository *r);
void init_commit_node(struct repository *r, struct commit *c);
void *alloc_commit_node(struct repository *r);
void *alloc_tag_node(struct repository *r);
void *alloc_object_node(struct repository *r);
void alloc_report(struct repository *r);

struct alloc_state *allocate_alloc_state(void);
void clear_alloc_state(struct alloc_state *s);

#endif
