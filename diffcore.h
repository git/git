/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFFCORE_H
#define DIFFCORE_H

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

struct userdiff_driver;

struct diff_filespec {
	unsigned char sha1[20];
	char *path;
	void *data;
	void *cnt_data;
	unsigned long size;
	int count;               /* Reference count */
	int rename_used;         /* Count of rename users */
	unsigned short mode;	 /* file mode */
	unsigned sha1_valid : 1; /* if true, use sha1 and trust mode;
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

extern struct diff_filespec *alloc_filespec(const char *);
extern void free_filespec(struct diff_filespec *);
extern void fill_filespec(struct diff_filespec *, const unsigned char *,
			  int, unsigned short);

extern int diff_populate_filespec(struct diff_filespec *, int);
extern void diff_free_filespec_data(struct diff_filespec *);
extern void diff_free_filespec_blob(struct diff_filespec *);
extern int diff_filespec_is_binary(struct diff_filespec *);

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

extern void diff_free_filepair(struct diff_filepair *);

extern int diff_unmodified_pair(struct diff_filepair *);

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
extern struct diff_filepair *diff_queue(struct diff_queue_struct *,
					struct diff_filespec *,
					struct diff_filespec *);
extern void diff_q(struct diff_queue_struct *, struct diff_filepair *);

extern void diffcore_break(int);
extern void diffcore_rename(struct diff_options *);
extern void diffcore_merge_broken(void);
extern void diffcore_pickaxe(struct diff_options *);
extern void diffcore_order(const char *orderfile);

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

extern int diffcore_count_changes(struct diff_filespec *src,
				  struct diff_filespec *dst,
				  void **src_count_p,
				  void **dst_count_p,
				  unsigned long delta_limit,
				  unsigned long *src_copied,
				  unsigned long *literal_added);

#endif
