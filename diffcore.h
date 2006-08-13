/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef _DIFFCORE_H_
#define _DIFFCORE_H_

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
#define DEFAULT_MERGE_SCORE  36000 /* maximum for break-merge to happen 60%) */

#define MINIMUM_BREAK_SIZE     400 /* do not break a file smaller than this */

struct diff_filespec {
	unsigned char sha1[20];
	char *path;
	void *data;
	void *cnt_data;
	unsigned long size;
	int xfrm_flags;		 /* for use by the xfrm */
	unsigned short mode;	 /* file mode */
	unsigned sha1_valid : 1; /* if true, use sha1 and trust mode;
				  * if false, use the name and read from
				  * the filesystem.
				  */
#define DIFF_FILE_VALID(spec) (((spec)->mode) != 0)
	unsigned should_free : 1; /* data should be free()'ed */
	unsigned should_munmap : 1; /* data should be munmap()'ed */
};

extern struct diff_filespec *alloc_filespec(const char *);
extern void fill_filespec(struct diff_filespec *, const unsigned char *,
			  unsigned short);

extern int diff_populate_filespec(struct diff_filespec *, int);
extern void diff_free_filespec_data(struct diff_filespec *);

struct diff_filepair {
	struct diff_filespec *one;
	struct diff_filespec *two;
	unsigned short int score;
	char status; /* M C R N D U (see Documentation/diff-format.txt) */
	unsigned source_stays : 1; /* all of R/C are copies */
	unsigned broken_pair : 1;
	unsigned renamed_pair : 1;
};
#define DIFF_PAIR_UNMERGED(p) \
	(!DIFF_FILE_VALID((p)->one) && !DIFF_FILE_VALID((p)->two))

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

extern struct diff_queue_struct diff_queued_diff;
extern struct diff_filepair *diff_queue(struct diff_queue_struct *,
					struct diff_filespec *,
					struct diff_filespec *);
extern void diff_q(struct diff_queue_struct *, struct diff_filepair *);

extern void diffcore_pathspec(const char **pathspec);
extern void diffcore_break(int);
extern void diffcore_rename(struct diff_options *);
extern void diffcore_merge_broken(void);
extern void diffcore_pickaxe(const char *needle, int opts);
extern void diffcore_order(const char *orderfile);

#define DIFF_DEBUG 0
#if DIFF_DEBUG
void diff_debug_filespec(struct diff_filespec *, int, const char *);
void diff_debug_filepair(const struct diff_filepair *, int);
void diff_debug_queue(const char *, struct diff_queue_struct *);
#else
#define diff_debug_filespec(a,b,c) do {} while(0)
#define diff_debug_filepair(a,b) do {} while(0)
#define diff_debug_queue(a,b) do {} while(0)
#endif

extern int diffcore_count_changes(void *src, unsigned long src_size,
				  void *dst, unsigned long dst_size,
				  void **src_count_p,
				  void **dst_count_p,
				  unsigned long delta_limit,
				  unsigned long *src_copied,
				  unsigned long *literal_added);

#endif
