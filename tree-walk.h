#ifndef TREE_WALK_H
#define TREE_WALK_H

struct name_entry {
	const unsigned char *sha1;
	const char *path;
	unsigned int mode;
};

struct tree_desc {
	const void *buffer;
	struct name_entry entry;
	unsigned int size;
};

static inline const unsigned char *tree_entry_extract(struct tree_desc *desc, const char **pathp, unsigned int *modep)
{
	*pathp = desc->entry.path;
	*modep = canon_mode(desc->entry.mode);
	return desc->entry.sha1;
}

static inline int tree_entry_len(const char *name, const unsigned char *sha1)
{
	return (const char *)sha1 - name - 1;
}

void update_tree_entry(struct tree_desc *);
void init_tree_desc(struct tree_desc *desc, const void *buf, unsigned long size);

/*
 * Helper function that does both tree_entry_extract() and update_tree_entry()
 * and returns true for success
 */
int tree_entry(struct tree_desc *, struct name_entry *);

void *fill_tree_descriptor(struct tree_desc *desc, const unsigned char *sha1);

struct traverse_info;
typedef int (*traverse_callback_t)(int n, unsigned long mask, unsigned long dirmask, struct name_entry *entry, struct traverse_info *);
int traverse_trees(int n, struct tree_desc *t, struct traverse_info *info);

struct traverse_info {
	struct traverse_info *prev;
	struct name_entry name;
	int pathlen;
	struct pathspec *pathspec;

	unsigned long conflicts;
	traverse_callback_t fn;
	void *data;
	int show_all_errors;
};

int get_tree_entry(const unsigned char *, const char *, unsigned char *, unsigned *);
extern char *make_traverse_path(char *path, const struct traverse_info *info, const struct name_entry *n);
extern void setup_traverse_info(struct traverse_info *info, const char *base);

static inline int traverse_path_len(const struct traverse_info *info, const struct name_entry *n)
{
	return info->pathlen + tree_entry_len(n->path, n->sha1);
}

extern int tree_entry_interesting(const struct name_entry *, struct strbuf *, int, const struct pathspec *ps);

#endif
