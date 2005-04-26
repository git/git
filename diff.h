/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFF_H
#define DIFF_H

/* These two are for backward compatibility with show-diff;
 * new users should not use them.
 */
extern void show_differences(const struct cache_entry *ce, int reverse);
extern void show_diff_empty(const struct cache_entry *ce, int reverse);

struct diff_spec {
	union {
		const char *name;       /* path on the filesystem */
		unsigned char sha1[20]; /* blob object ID */
	} u;
	unsigned short mode;	 /* file mode */
	unsigned sha1_valid : 1; /* if true, use u.sha1 and trust mode.
				  * (however with a NULL SHA1, read them
				  * from the file!).
				  * if false, use u.name and read mode from
				  * the filesystem.
				  */
	unsigned file_valid : 1; /* if false the file does not even exist */
};

extern void run_external_diff(const char *name,
			      struct diff_spec *, struct diff_spec *);

#endif /* DIFF_H */
