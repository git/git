#ifndef TREE_WALK_H
#define TREE_WALK_H

#include "cache.h"

struct name_entry {
	struct object_id oid;
	const char *path;
	int pathlen;
	unsigned int mode;
};

struct tree_desc {
	const void *buffer;
	struct name_entry entry;
	unsigned int size;
};

static inline const struct object_id *tree_entry_extract(struct tree_desc *desc, const char **pathp, unsigned short *modep)
{
	*pathp = desc->entry.path;
	*modep = desc->entry.mode;
	return &desc->entry.oid;
}

static inline int tree_entry_len(const struct name_entry *ne)
{
	return ne->pathlen;
}

/*
 * The _gently versions of these functions warn and return false on a
 * corrupt tree entry rather than dying,
 */

void update_tree_entry(struct tree_desc *);
int update_tree_entry_gently(struct tree_desc *);
void init_tree_desc(struct tree_desc *desc, const void *buf, unsigned long size);
int init_tree_desc_gently(struct tree_desc *desc, const void *buf, unsigned long size);

/*
 * Helper function that does both tree_entry_extract() and update_tree_entry()
 * and returns true for success
 */
int tree_entry(struct tree_desc *, struct name_entry *);
int tree_entry_gently(struct tree_desc *, struct name_entry *);

void *fill_tree_descriptor(struct repository *r,
			   struct tree_desc *desc,
			   const struct object_id *oid);

struct traverse_info;
typedef int (*traverse_callback_t)(int n, unsigned long mask, unsigned long dirmask, struct name_entry *entry, struct traverse_info *);
int traverse_trees(struct index_state *istate, int n, struct tree_desc *t, struct traverse_info *info);

enum get_oid_result get_tree_entry_follow_symlinks(struct repository *r, struct object_id *tree_oid, const char *name, struct object_id *result, struct strbuf *result_path, unsigned short *mode);

struct traverse_info {
	const char *traverse_path;
	struct traverse_info *prev;
	struct name_entry name;
	int pathlen;
	struct pathspec *pathspec;

	unsigned long df_conflicts;
	traverse_callback_t fn;
	void *data;
	int show_all_errors;
};

int get_tree_entry(struct repository *, const struct object_id *, const char *, struct object_id *, unsigned short *);
char *make_traverse_path(char *path, const struct traverse_info *info, const struct name_entry *n);
void setup_traverse_info(struct traverse_info *info, const char *base);

static inline int traverse_path_len(const struct traverse_info *info, const struct name_entry *n)
{
	return info->pathlen + tree_entry_len(n);
}

/* in general, positive means "kind of interesting" */
enum interesting {
	all_entries_not_interesting = -1, /* no, and no subsequent entries will be either */
	entry_not_interesting = 0,
	entry_interesting = 1,
	all_entries_interesting = 2 /* yes, and all subsequent entries will be */
};

enum interesting tree_entry_interesting(struct index_state *istate,
					const struct name_entry *,
					struct strbuf *, int,
					const struct pathspec *ps);

#endif
