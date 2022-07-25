#ifndef TREE_H
#define TREE_H

#include "object.h"
#include "pathspec.h"

struct repository;
struct strbuf;

struct tree {
	struct object object;
	void *buffer;
	unsigned long size;
};

struct show_tree_data {
	unsigned mode;
	enum object_type type;
	const struct object_id *oid;
	const char *pathname;
	struct strbuf *base;
};

extern const char *tree_type;

struct tree *lookup_tree(struct repository *r, const struct object_id *oid);

struct tree *lookup_tree_by_path(struct repository *r,
				 struct object_id *commit_oid,
				 struct pathspec *pathspec,
				 const char *path);

int parse_tree_buffer(struct tree *item, void *buffer, unsigned long size);

int parse_tree_gently(struct tree *tree, int quiet_on_missing);
static inline int parse_tree(struct tree *tree)
{
	return parse_tree_gently(tree, 0);
}
void free_tree_buffer(struct tree *tree);

/* Parses and returns the tree in the given ent, chasing tags and commits. */
struct tree *parse_tree_indirect(const struct object_id *oid);

int cmp_cache_name_compare(const void *a_, const void *b_);

#define READ_TREE_RECURSIVE 1
typedef int (*read_tree_fn_t)(const struct object_id *, struct strbuf *, const char *, unsigned int, void *);

int read_tree_at(struct repository *r,
		 struct tree *tree, struct strbuf *base,
		 const struct pathspec *pathspec,
		 read_tree_fn_t fn, void *context);

int read_tree(struct repository *r,
	      struct tree *tree,
	      const struct pathspec *pathspec,
	      read_tree_fn_t fn, void *context);

int show_tree_common(struct show_tree_data *data, int *recurse,
			    const struct object_id *oid, struct strbuf *base,
			    const char *pathname, unsigned mode, struct pathspec pathspec, int ls_options);

int show_recursive(const char *base, size_t baselen, const char *pathname, struct pathspec pathspec, int ls_options);

int show_tree_name_only(const struct object_id *oid, struct strbuf *base,
			       const char *pathname, unsigned mode, void *context);
#define LS_RECURSIVE 1
#define LS_TREE_ONLY (1 << 1)
#define LS_SHOW_TREES (1 << 2)

#endif /* TREE_H */
