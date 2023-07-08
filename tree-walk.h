#ifndef TREE_WALK_H
#define TREE_WALK_H

#include "hash-ll.h"

struct index_state;
struct repository;

#define MAX_TRAVERSE_TREES 8

/**
 * The tree walking API is used to traverse and inspect trees.
 */

/**
 * An entry in a tree. Each entry has a sha1 identifier, pathname, and mode.
 */
struct name_entry {
	struct object_id oid;
	const char *path;
	int pathlen;
	unsigned int mode;
};

/**
 * A semi-opaque data structure used to maintain the current state of the walk.
 */
struct tree_desc {
	/*
	 * pointer into the memory representation of the tree. It always
	 * points at the current entry being visited.
	 */
	const void *buffer;

	/* points to the current entry being visited. */
	struct name_entry entry;

	/* counts the number of bytes left in the `buffer`. */
	unsigned int size;

	/* option flags passed via init_tree_desc_gently() */
	enum tree_desc_flags {
		TREE_DESC_RAW_MODES = (1 << 0),
	} flags;
};

/**
 * Decode the entry currently being visited (the one pointed to by
 * `tree_desc's` `entry` member) and return the sha1 of the entry. The
 * `pathp` and `modep` arguments are set to the entry's pathname and mode
 * respectively.
 */
static inline const struct object_id *tree_entry_extract(struct tree_desc *desc, const char **pathp, unsigned short *modep)
{
	*pathp = desc->entry.path;
	*modep = desc->entry.mode;
	return &desc->entry.oid;
}

/**
 * Calculate the length of a tree entry's pathname. This utilizes the
 * memory structure of a tree entry to avoid the overhead of using a
 * generic strlen().
 */
static inline int tree_entry_len(const struct name_entry *ne)
{
	return ne->pathlen;
}

/*
 * The _gently versions of these functions warn and return false on a
 * corrupt tree entry rather than dying,
 */

/**
 * Walk to the next entry in a tree. This is commonly used in conjunction
 * with `tree_entry_extract` to inspect the current entry.
 */
void update_tree_entry(struct tree_desc *);

int update_tree_entry_gently(struct tree_desc *);

/**
 * Initialize a `tree_desc` and decode its first entry. The buffer and
 * size parameters are assumed to be the same as the buffer and size
 * members of `struct tree`.
 */
void init_tree_desc(struct tree_desc *desc, const void *buf, unsigned long size);

int init_tree_desc_gently(struct tree_desc *desc, const void *buf, unsigned long size,
			  enum tree_desc_flags flags);

/*
 * Visit the next entry in a tree. Returns 1 when there are more entries
 * left to visit and 0 when all entries have been visited. This is
 * commonly used in the test of a while loop.
 */
int tree_entry(struct tree_desc *, struct name_entry *);

int tree_entry_gently(struct tree_desc *, struct name_entry *);

/**
 * Initialize a `tree_desc` and decode its first entry given the
 * object ID of a tree. Returns the `buffer` member if the latter
 * is a valid tree identifier and NULL otherwise.
 */
void *fill_tree_descriptor(struct repository *r,
			   struct tree_desc *desc,
			   const struct object_id *oid);

struct traverse_info;
typedef int (*traverse_callback_t)(int n, unsigned long mask, unsigned long dirmask, struct name_entry *entry, struct traverse_info *);

/**
 * Traverse `n` number of trees in parallel. The `fn` callback member of
 * `traverse_info` is called once for each tree entry.
 */
int traverse_trees(struct index_state *istate, int n, struct tree_desc *t, struct traverse_info *info);

enum get_oid_result get_tree_entry_follow_symlinks(struct repository *r, struct object_id *tree_oid, const char *name, struct object_id *result, struct strbuf *result_path, unsigned short *mode);

/**
 * A structure used to maintain the state of a traversal.
 */
struct traverse_info {
	const char *traverse_path;

	/*
	 * points to the traverse_info which was used to descend into the
	 * current tree. If this is the top-level tree `prev` will point to
	 * a dummy traverse_info.
	 */
	struct traverse_info *prev;

	/* is the entry for the current tree (if the tree is a subtree). */
	const char *name;

	size_t namelen;
	unsigned mode;

	/* is the length of the full path for the current tree. */
	size_t pathlen;

	struct pathspec *pathspec;

	/* can be used by callbacks to maintain directory-file conflicts. */
	unsigned long df_conflicts;

	/* a callback called for each entry in the tree.
	 *
	 * The arguments passed to the traverse callback are as follows:
	 *
	 * - `n` counts the number of trees being traversed.
	 *
	 * - `mask` has its nth bit set if something exists in the nth entry.
	 *
	 * - `dirmask` has its nth bit set if the nth tree's entry is a directory.
	 *
	 * - `entry` is an array of size `n` where the nth entry is from the nth tree.
	 *
	 * - `info` maintains the state of the traversal.
	 *
	 * Returning a negative value will terminate the traversal. Otherwise the
	 * return value is treated as an update mask. If the nth bit is set the nth tree
	 * will be updated and if the bit is not set the nth tree entry will be the
	 * same in the next callback invocation.
	 */
	traverse_callback_t fn;

	/* can be anything the `fn` callback would want to use. */
	void *data;

	/* tells whether to stop at the first error or not. */
	int show_all_errors;
};

/**
 * Find an entry in a tree given a pathname and the sha1 of a tree to
 * search. Returns 0 if the entry is found and -1 otherwise. The third
 * and fourth parameters are set to the entry's sha1 and mode respectively.
 */
int get_tree_entry(struct repository *, const struct object_id *, const char *, struct object_id *, unsigned short *);

/**
 * Generate the full pathname of a tree entry based from the root of the
 * traversal. For example, if the traversal has recursed into another
 * tree named "bar" the pathname of an entry "baz" in the "bar"
 * tree would be "bar/baz".
 */
char *make_traverse_path(char *path, size_t pathlen, const struct traverse_info *info,
			 const char *name, size_t namelen);

/**
 * Convenience wrapper to `make_traverse_path` into a strbuf.
 */
void strbuf_make_traverse_path(struct strbuf *out,
			       const struct traverse_info *info,
			       const char *name, size_t namelen);

/**
 * Initialize a `traverse_info` given the pathname of the tree to start
 * traversing from.
 */
void setup_traverse_info(struct traverse_info *info, const char *base);

/**
 * Calculate the length of a pathname returned by `make_traverse_path`.
 * This utilizes the memory structure of a tree entry to avoid the
 * overhead of using a generic strlen().
 */
static inline size_t traverse_path_len(const struct traverse_info *info,
				       size_t namelen)
{
	return st_add(info->pathlen, namelen);
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
