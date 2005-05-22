/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef _DIFFCORE_H_
#define _DIFFCORE_H_

/* This header file is internal between diff.c and its diff transformers
 * (e.g. diffcore-rename, diffcore-pickaxe).  Never include this header
 * in anything else.
 */
#define MAX_SCORE 10000
#define DEFAULT_MINIMUM_SCORE 5000

#define RENAME_DST_MATCHED 01
#define RENAME_SRC_GONE    02
#define RENAME_SCORE_SHIFT 8

struct diff_filespec {
	unsigned char sha1[20];
	char *path;
	void *data;
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

extern int diff_populate_filespec(struct diff_filespec *);
extern void diff_free_filespec_data(struct diff_filespec *);

struct diff_filepair {
	struct diff_filespec *one;
	struct diff_filespec *two;
	char *xfrm_msg;
	int orig_order; /* the original order of insertion into the queue */
	int xfrm_work; /* for use by tramsformers, not by diffcore */
};

struct diff_queue_struct {
	struct diff_filepair **queue;
	int alloc;
	int nr;
};

extern struct diff_queue_struct diff_queued_diff;
extern struct diff_filepair *diff_queue(struct diff_queue_struct *,
					struct diff_filespec *,
					struct diff_filespec *);

#endif
