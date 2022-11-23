/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFFCORE_H
#define DIFFCORE_H

#include "cache.h"

struct diff_options;
struct repository;
struct strintmap;
struct strmap;
struct userdiff_driver;

/* This header file is internal between diff.c and its diff transformers
 * (e.g. diffcore-rename, diffcore-pickaxe).  Never include this header
 * in anything else.
 */

/* We internally use unsigned short as the score value,
 * and rely on an int capable to hold 32-bits.  -B can take
 * -Bmerge_score/break_score format and the two scores are
 * passed around in one int (high 16-bit for merge and low 16-bit
 * for break).
 */
#define MAX_SCORE 60000.0
#define DEFAULT_RENAME_SCORE 30000 /* rename/copy similarity minimum (50%) */
#define DEFAULT_BREAK_SCORE  30000 /* minimum for break to happen (50%) */
#define DEFAULT_MERGE_SCORE  36000 /* maximum for break-merge to happen (60%) */

#define MINIMUM_BREAK_SIZE     400 /* do not break a file smaller than this */

/**
 * the internal representation for a single file (blob).  It records the blob
 * object name (if known -- for a work tree file it typically is a NUL SHA-1),
 * filemode and pathname.  This is what the `diff_addremove()`, `diff_change()`
 * and `diff_unmerge()` synthesize and feed `diff_queue()` function with.
 */
struct diff_filespec {
	struct object_id oid;
	char *path;
	void *data;
	void *cnt_data;
	unsigned long size;
	int count;               /* Reference count */
	int rename_used;         /* Count of rename users */
	unsigned short mode;	 /* file mode */
	unsigned oid_valid : 1;  /* if true, use oid and trust mode;
				  * if false, use the name and read from
				  * the filesystem.
				  */
#define DIFF_FILE_VALID(spec) (((spec)->mode) != 0)
	unsigned should_free : 1; /* data should be free()'ed */
	unsigned should_munmap : 1; /* data should be munmap()'ed */
	unsigned dirty_submodule : 2;  /* For submodules: its work tree is dirty */
#define DIRTY_SUBMODULE_UNTRACKED 1
#define DIRTY_SUBMODULE_MODIFIED  2
	unsigned is_stdin : 1;
	unsigned has_more_entries : 1; /* only appear in combined diff */
	/* data should be considered "binary"; -1 means "don't know yet" */
	signed int is_binary : 2;
	struct userdiff_driver *driver;
};

struct diff_filespec *alloc_filespec(const char *);
void free_filespec(struct diff_filespec *);
void fill_filespec(struct diff_filespec *, const struct object_id *,
		   int, unsigned short);

/*
 * Prefetch the entries in diff_queued_diff. The parameter is a pointer to a
 * struct repository.
 */
void diff_queued_diff_prefetch(void *repository);

struct diff_populate_filespec_options {
	unsigned check_size_only : 1;
	unsigned check_binary : 1;

	/*
	 * If an object is missing, diff_populate_filespec() will invoke this
	 * callback before attempting to read that object again.
	 */
	void (*missing_object_cb)(void *);
	void *missing_object_data;
};
int diff_populate_filespec(struct repository *, struct diff_filespec *,
			   const struct diff_populate_filespec_options *);
void diff_free_filespec_data(struct diff_filespec *);
void diff_free_filespec_blob(struct diff_filespec *);
int diff_filespec_is_binary(struct repository *, struct diff_filespec *);

/**
 * This records a pair of `struct diff_filespec`; the filespec for a file in
 * the "old" set (i.e. preimage) is called `one`, and the filespec for a file
 * in the "new" set (i.e. postimage) is called `two`.  A change that represents
 * file creation has NULL in `one`, and file deletion has NULL in `two`.
 *
 * A `filepair` starts pointing at `one` and `two` that are from the same
 * filename, but `diffcore_std()` can break pairs and match component filespecs
 * with other filespecs from a different filepair to form new filepair. This is
 * called 'rename detection'.
 */
struct diff_filepair {
	struct diff_filespec *one;
	struct diff_filespec *two;
	unsigned short int score;
	char status; /* M C R A D U etc. (see Documentation/diff-format.txt or DIFF_STATUS_* in diff.h) */
	unsigned broken_pair : 1;
	unsigned renamed_pair : 1;
	unsigned is_unmerged : 1;
	unsigned done_skip_stat_unmatch : 1;
	unsigned skip_stat_unmatch_result : 1;
};

#define DIFF_PAIR_UNMERGED(p) ((p)->is_unmerged)

#define DIFF_PAIR_RENAME(p) ((p)->renamed_pair)

#define DIFF_PAIR_BROKEN(p) \
	( (!DIFF_FILE_VALID((p)->one) != !DIFF_FILE_VALID((p)->two)) && \
	  ((p)->broken_pair != 0) )

#define DIFF_PAIR_TYPE_CHANGED(p) \
	((S_IFMT & (p)->one->mode) != (S_IFMT & (p)->two->mode))

#define DIFF_PAIR_MODE_CHANGED(p) ((p)->one->mode != (p)->two->mode)

void diff_free_filepair(struct diff_filepair *);
void pool_diff_free_filepair(struct mem_pool *pool,
			     struct diff_filepair *p);

int diff_unmodified_pair(struct diff_filepair *);

/**
 * This is a collection of filepairs.  Notable members are:
 *
 * - `queue`:
 * An array of pointers to `struct diff_filepair`. This dynamically grows as
 * you add filepairs;
 *
 * - `alloc`:
 * The allocated size of the `queue` array;
 *
 * - `nr`:
 * The number of elements in the `queue` array.
 */
struct diff_queue_struct {
	struct diff_filepair **queue;
	int alloc;
	int nr;
};

#define DIFF_QUEUE_CLEAR(q) \
	do { \
		(q)->queue = NULL; \
		(q)->nr = (q)->alloc = 0; \
	} while (0)

extern struct diff_queue_struct diff_queued_diff;
struct diff_filepair *diff_queue(struct diff_queue_struct *,
				 struct diff_filespec *,
				 struct diff_filespec *);
void diff_q(struct diff_queue_struct *, struct diff_filepair *);
void diff_free_queue(struct diff_queue_struct *q);

/* dir_rename_relevance: the reason we want rename information for a dir */
enum dir_rename_relevance {
	NOT_RELEVANT = 0,
	RELEVANT_FOR_ANCESTOR = 1,
	RELEVANT_FOR_SELF = 2
};
/* file_rename_relevance: the reason(s) we want rename information for a file */
enum file_rename_relevance {
	RELEVANT_NO_MORE = 0,  /* i.e. NOT relevant */
	RELEVANT_CONTENT = 1,
	RELEVANT_LOCATION = 2
};

void partial_clear_dir_rename_count(struct strmap *dir_rename_count);

void diffcore_break(struct repository *, int);
void diffcore_rename(struct diff_options *);
void diffcore_rename_extended(struct diff_options *options,
			      struct mem_pool *pool,
			      struct strintmap *relevant_sources,
			      struct strintmap *dirs_removed,
			      struct strmap *dir_rename_count,
			      struct strmap *cached_pairs);
void diffcore_merge_broken(void);
void diffcore_pickaxe(struct diff_options *);
void diffcore_order(const char *orderfile);
void diffcore_rotate(struct diff_options *);

/* low-level interface to diffcore_order */
struct obj_order {
	void *obj;	/* setup by caller */

	/* setup/used by order_objects() */
	int orig_order;
	int order;
};

typedef const char *(*obj_path_fn_t)(void *obj);

void order_objects(const char *orderfile, obj_path_fn_t obj_path,
		   struct obj_order *objs, int nr);

#define DIFF_DEBUG 0
#if DIFF_DEBUG
void diff_debug_filespec(struct diff_filespec *, int, const char *);
void diff_debug_filepair(const struct diff_filepair *, int);
void diff_debug_queue(const char *, struct diff_queue_struct *);
#else
#define diff_debug_filespec(a,b,c) do { /* nothing */ } while (0)
#define diff_debug_filepair(a,b) do { /* nothing */ } while (0)
#define diff_debug_queue(a,b) do { /* nothing */ } while (0)
#endif

int diffcore_count_changes(struct repository *r,
			   struct diff_filespec *src,
			   struct diff_filespec *dst,
			   void **src_count_p,
			   void **dst_count_p,
			   unsigned long *src_copied,
			   unsigned long *literal_added);

/*
 * If filespec contains an OID and if that object is missing from the given
 * repository, add that OID to to_fetch.
 */
void diff_add_if_missing(struct repository *r,
			 struct oid_array *to_fetch,
			 const struct diff_filespec *filespec);

#endif
