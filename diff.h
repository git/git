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

extern void diff_setup(int detect_rename, int minimum_score,
		       int reverse,
		       const char **spec, int cnt);

extern void diff_flush(void);

#endif /* DIFF_H */
