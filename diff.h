/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFF_H
#define DIFF_H

extern void diff_addremove(int addremove,
			   unsigned mode,
			   const unsigned char *sha1,
			   const char *base,
			   const char *path);

extern void diff_change(unsigned mode1, unsigned mode2,
			     const unsigned char *sha1,
			     const unsigned char *sha2,
			     const char *base, const char *path);

extern void diff_unmerge(const char *path);

/* These are for diff-helper */

struct diff_spec {
	unsigned char blob_sha1[20];
	unsigned short mode;	 /* file mode */
	unsigned sha1_valid : 1; /* if true, use blob_sha1 and trust mode;
				  * however with a NULL SHA1, read them
				  * from the file system.
				  * if false, use the name and read mode from
				  * the filesystem.
				  */
	unsigned file_valid : 1; /* if false the file does not even exist */
};

extern void run_external_diff(const char *name, const char *other,
			      struct diff_spec *, struct diff_spec *);

#endif /* DIFF_H */
