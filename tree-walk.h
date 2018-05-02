#ifndef TREE_WALK_H
#define TREE_WALK_H

struct name_entry {
	const struct object_id *oid;
	const char *path;
	unsigned int mode;
};

struct tree_desc {
	const void *buffer;
	struct name_entry entry;
	unsigned int size;
};

static inline const struct object_id *tree_entry_extract(struct tree_desc *desc, const char **pathp, unsigned int *modep)
{
	*pathp = desc->entry.path;
	*modep = desc->entry.mode;
	return desc->entry.oid;
}

static inline int tree_entry_len(const struct name_entry *ne)
{
	return (const char *)ne->oid - ne->path - 1;
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

void *fill_tree_descriptor(struct tree_desc *desc, const struct object_id *oid);

struct traverse_info;
typedef int (*traverse_callback_t)(int n, unsigned long mask, unsigned long dirmask, struct name_entry *entry, struct traverse_info *);
int traverse_trees(int n, struct tree_desc *t, struct traverse_info *info);

enum follow_symlinks_result {
	FOUND = 0, /* This includes out-of-tree links */
	MISSING_OBJECT = -1, /* The initial symlink is missing */
	DANGLING_SYMLINK = -2, /*
				* The initial symlink is there, but
				* (transitively) points to a missing
				* in-tree file
				*/
	SYMLINK_LOOP = -3,
	NOT_DIR = -4, /*
		       * Somewhere along the symlink chain, a path is
		       * requested which contains a file as a
		       * non-final element.
		       */
};

enum follow_symlinks_result get_tree_entry_follow_symlinks(struct object_id *tree_oid, const char *name, struct object_id *result, struct strbuf *result_path, unsigned *mode);

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

int get_tree_entry(const struct object_id *, const char *, struct object_id *, unsigned *);
extern char *make_traverse_path(char *path, const struct traverse_info *info, const struct name_entry *n);
extern void setup_traverse_info(struct traverse_info *info, const char *base);

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

extern enum interesting tree_entry_interesting(const struct name_entry *,
					       struct strbuf *, int,
					       const struct pathspec *ps);

#endif
