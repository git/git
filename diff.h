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

extern int diff_scoreopt_parse(const char *opt);

extern void diff_setup(int reverse, int diff_raw_output);

extern void diff_detect_rename(int, int);
extern void diff_pickaxe(const char *);

extern int diff_queue_is_empty(void);

extern void diff_flush(const char **, int);

#endif /* DIFF_H */
