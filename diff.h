/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFF_H
#define DIFF_H

#define DIFF_FILE_CANON_MODE(mode) \
	(S_ISREG(mode) ? (S_IFREG | ce_permissions(mode)) : \
	S_ISLNK(mode) ? S_IFLNK : S_IFDIR)

struct tree_desc {
	void *buf;
	unsigned long size;
};

struct diff_options;

typedef void (*change_fn_t)(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const unsigned char *old_sha1,
		 const unsigned char *new_sha1,
		 const char *base, const char *path);

typedef void (*add_remove_fn_t)(struct diff_options *options,
		    int addremove, unsigned mode,
		    const unsigned char *sha1,
		    const char *base, const char *path);

struct diff_options {
	const char **paths;
	const char *filter;
	const char *orderfile;
	const char *pickaxe;
	unsigned recursive:1,
		 tree_in_recursive:1;
	int break_opt;
	int detect_rename;
	int find_copies_harder;
	int line_termination;
	int output_format;
	int pickaxe_opts;
	int rename_score;
	int reverse_diff;
	int rename_limit;
	int setup;

	change_fn_t change;
	add_remove_fn_t add_remove;
};

extern void diff_tree_setup_paths(const char **paths);
extern int diff_tree(struct tree_desc *t1, struct tree_desc *t2,
		     const char *base, struct diff_options *opt);
extern int diff_tree_sha1(const unsigned char *old, const unsigned char *new,
			  const char *base, struct diff_options *opt);

extern void diff_addremove(struct diff_options *,
			   int addremove,
			   unsigned mode,
			   const unsigned char *sha1,
			   const char *base,
			   const char *path);

extern void diff_change(struct diff_options *,
			unsigned mode1, unsigned mode2,
			const unsigned char *sha1,
			const unsigned char *sha2,
			const char *base, const char *path);

extern void diff_unmerge(struct diff_options *,
			 const char *path);

extern int diff_scoreopt_parse(const char *opt);

#define DIFF_SETUP_REVERSE      	1
#define DIFF_SETUP_USE_CACHE		2
#define DIFF_SETUP_USE_SIZE_CACHE	4

extern void diff_setup(struct diff_options *);
extern int diff_opt_parse(struct diff_options *, const char **, int);
extern int diff_setup_done(struct diff_options *);

#define DIFF_DETECT_RENAME	1
#define DIFF_DETECT_COPY	2

#define DIFF_PICKAXE_ALL	1

extern void diffcore_std(struct diff_options *);

extern void diffcore_std_no_resolve(struct diff_options *);

#define COMMON_DIFF_OPTIONS_HELP \
"\ncommon diff options:\n" \
"  -z            output diff-raw with lines terminated with NUL.\n" \
"  -p            output patch format.\n" \
"  -u            synonym for -p.\n" \
"  --name-only   show only names of changed files.\n" \
"  --name-status show names and status of changed files.\n" \
"  -R            swap input file pairs.\n" \
"  -B            detect complete rewrites.\n" \
"  -M            detect renames.\n" \
"  -C            detect copies.\n" \
"  --find-copies-harder\n" \
"                try unchanged files as candidate for copy detection.\n" \
"  -l<n>         limit rename attempts up to <n> paths.\n" \
"  -O<file>      reorder diffs according to the <file>.\n" \
"  -S<string>    find filepair whose only one side contains the string.\n" \
"  --pickaxe-all\n" \
"                show all files diff when -S is used and hit is found.\n"

extern int diff_queue_is_empty(void);

#define DIFF_FORMAT_RAW		1
#define DIFF_FORMAT_PATCH	2
#define DIFF_FORMAT_NO_OUTPUT	3
#define DIFF_FORMAT_NAME	4
#define DIFF_FORMAT_NAME_STATUS	5

extern void diff_flush(struct diff_options*);

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
#define DIFF_STATUS_FILTER_AON		'*'
#define DIFF_STATUS_FILTER_BROKEN	'B'

#endif /* DIFF_H */
