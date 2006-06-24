/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFF_H
#define DIFF_H

#include "tree-walk.h"

struct rev_info;
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

#define DIFF_FORMAT_RAW		0x0001
#define DIFF_FORMAT_DIFFSTAT	0x0002
#define DIFF_FORMAT_SUMMARY	0x0004
#define DIFF_FORMAT_PATCH	0x0008

/* These override all above */
#define DIFF_FORMAT_NAME	0x0010
#define DIFF_FORMAT_NAME_STATUS	0x0020
#define DIFF_FORMAT_CHECKDIFF	0x0040

/* Same as output_format = 0 but we know that -s flag was given
 * and we should not give default value to output_format.
 */
#define DIFF_FORMAT_NO_OUTPUT	0x0080

struct diff_options {
	const char *filter;
	const char *orderfile;
	const char *pickaxe;
	unsigned recursive:1,
		 tree_in_recursive:1,
		 binary:1,
		 full_index:1,
		 silent_on_remove:1,
		 find_copies_harder:1,
		 color_diff:1;
	int context;
	int break_opt;
	int detect_rename;
	int line_termination;
	int output_format;
	int pickaxe_opts;
	int rename_score;
	int reverse_diff;
	int rename_limit;
	int setup;
	int abbrev;
	const char *stat_sep;
	long xdl_opts;

	int nr_paths;
	const char **paths;
	int *pathlens;
	change_fn_t change;
	add_remove_fn_t add_remove;
};

extern const char mime_boundary_leader[];

extern void diff_tree_setup_paths(const char **paths, struct diff_options *);
extern void diff_tree_release_paths(struct diff_options *);
extern int diff_tree(struct tree_desc *t1, struct tree_desc *t2,
		     const char *base, struct diff_options *opt);
extern int diff_tree_sha1(const unsigned char *old, const unsigned char *new,
			  const char *base, struct diff_options *opt);

struct combine_diff_path {
	struct combine_diff_path *next;
	int len;
	char *path;
	unsigned int mode;
	unsigned char sha1[20];
	struct combine_diff_parent {
		char status;
		unsigned int mode;
		unsigned char sha1[20];
	} parent[FLEX_ARRAY];
};
#define combine_diff_path_size(n, l) \
	(sizeof(struct combine_diff_path) + \
	 sizeof(struct combine_diff_parent) * (n) + (l) + 1)

extern void show_combined_diff(struct combine_diff_path *elem, int num_parent,
			      int dense, struct rev_info *);

extern void diff_tree_combined(const unsigned char *sha1, const unsigned char parent[][20], int num_parent, int dense, struct rev_info *rev);

extern void diff_tree_combined_merge(const unsigned char *sha1, int, struct rev_info *);

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

extern int git_diff_config(const char *var, const char *value);
extern void diff_setup(struct diff_options *);
extern int diff_opt_parse(struct diff_options *, const char **, int);
extern int diff_setup_done(struct diff_options *);

#define DIFF_DETECT_RENAME	1
#define DIFF_DETECT_COPY	2

#define DIFF_PICKAXE_ALL	1
#define DIFF_PICKAXE_REGEX	2

extern void diffcore_std(struct diff_options *);

extern void diffcore_std_no_resolve(struct diff_options *);

#define COMMON_DIFF_OPTIONS_HELP \
"\ncommon diff options:\n" \
"  -z            output diff-raw with lines terminated with NUL.\n" \
"  -p            output patch format.\n" \
"  -u            synonym for -p.\n" \
"  --patch-with-raw\n" \
"                output both a patch and the diff-raw format.\n" \
"  --stat        show diffstat instead of patch.\n" \
"  --patch-with-stat\n" \
"                output a patch and prepend its diffstat.\n" \
"  --name-only   show only names of changed files.\n" \
"  --name-status show names and status of changed files.\n" \
"  --full-index  show full object name on index lines.\n" \
"  --abbrev=<n>  abbreviate object names in diff-tree header and diff-raw.\n" \
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

extern const char *diff_unique_abbrev(const unsigned char *, int);

extern int run_diff_files(struct rev_info *revs, int silent_on_removed);

extern int run_diff_index(struct rev_info *revs, int cached);

#endif /* DIFF_H */
