/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFF_H
#define DIFF_H

#define DIFF_FILE_CANON_MODE(mode) \
	(S_ISREG(mode) ? (S_IFREG | ce_permissions(mode)) : \
	S_ISLNK(mode) ? S_IFLNK : S_IFDIR)

extern void diff_addremove(int addremove,
			   unsigned mode,
			   const unsigned char *sha1,
			   const char *base,
			   const char *path);

extern void diff_change(unsigned mode1, unsigned mode2,
			     const unsigned char *sha1,
			     const unsigned char *sha2,
			     const char *base, const char *path);

extern void diff_helper_input(unsigned mode1,
			      unsigned mode2,
			      const unsigned char *sha1,
			      const unsigned char *sha2,
			      const char *path1,
			      int status,
			      int score,
			      const char *path2);

extern void diff_unmerge(const char *path);

extern int diff_scoreopt_parse(const char *opt);

#define DIFF_SETUP_REVERSE      	1
#define DIFF_SETUP_USE_CACHE		2
#define DIFF_SETUP_USE_SIZE_CACHE	4

extern void diff_setup(int flags);

#define DIFF_DETECT_RENAME	1
#define DIFF_DETECT_COPY	2

#define DIFF_PICKAXE_ALL	1

extern void diffcore_std(const char **paths,
			 int detect_rename, int rename_score,
			 const char *pickaxe, int pickaxe_opts,
			 int break_opt,
			 const char *orderfile, const char *filter);

extern void diffcore_std_no_resolve(const char **paths,
				    const char *pickaxe, int pickaxe_opts,
				    const char *orderfile, const char *filter);

#define COMMON_DIFF_OPTIONS_HELP \
"\ncommon diff options:\n" \
"  -r		diff recursively (only meaningful in diff-tree)\n" \
"  -z		output diff-raw with lines terminated with NUL.\n" \
"  -p		output patch format.\n" \
"  -u		synonym for -p.\n" \
"  --name-only	show only names of changed files.\n" \
"  --name-only-z\n" \
"		same as --name-only but terminate lines with NUL.\n" \
"  -R		swap input file pairs.\n" \
"  -B		detect complete rewrites.\n" \
"  -M		detect renames.\n" \
"  -C		detect copies.\n" \
"  --find-copies-harder\n" \
"		try unchanged files as candidate for copy detection.\n" \
"  -O<file>	reorder diffs according to the <file>.\n" \
"  -S<string>	find filepair whose only one side contains the string.\n" \
"  --pickaxe-all\n" \
"		show all files diff when -S is used and hit is found.\n"

extern int diff_queue_is_empty(void);

#define DIFF_FORMAT_RAW		1
#define DIFF_FORMAT_PATCH	2
#define DIFF_FORMAT_NO_OUTPUT	3
#define DIFF_FORMAT_NAME	4

extern void diff_flush(int output_style, int line_terminator);

/* diff-raw status letters */
#define DIFF_STATUS_ADDED		'A'
#define DIFF_STATUS_COPIED		'C'
#define DIFF_STATUS_DELETED		'D'
#define DIFF_STATUS_MODIFIED		'M'
#define DIFF_STATUS_RENAMED		'R'
#define DIFF_STATUS_TYPE_CHANGED	'T'
#define DIFF_STATUS_UNKNOWN		'X'
#define DIFF_STATUS_UNMERGED		'U'

/* these are not diff-raw status letters proper, but used by
 * diffcore-filter insn to specify additional restrictions.
 */
#define DIFF_STATUS_FILTER_AON		'A'
#define DIFF_STATUS_FILTER_BROKEN	'B'

#endif /* DIFF_H */
