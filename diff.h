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

extern void diff_guif(unsigned mode1,
		      unsigned mode2,
		      const unsigned char *sha1,
		      const unsigned char *sha2,
		      const char *path1,
		      const char *path2);

extern void diff_unmerge(const char *path);

extern int diff_scoreopt_parse(const char *opt);

#define DIFF_FORMAT_HUMAN	0
#define DIFF_FORMAT_MACHINE	1
#define DIFF_FORMAT_PATCH	2
#define DIFF_FORMAT_NO_OUTPUT	3
extern void diff_setup(int reverse);

#define DIFF_DETECT_RENAME	1
#define DIFF_DETECT_COPY	2

extern void diffcore_rename(int rename_copy, int minimum_score);

extern void diffcore_prune(void);

extern void diffcore_pickaxe(const char *needle);
extern void diffcore_pathspec(const char **pathspec);

extern int diff_queue_is_empty(void);

extern void diff_flush(int output_style);

#endif /* DIFF_H */
