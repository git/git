/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFF_H
#define DIFF_H

#include "tree-walk.h"
#include "pathspec.h"
#include "object.h"
#include "oidset.h"

/**
 * The diff API is for programs that compare two sets of files (e.g. two trees,
 * one tree and the index) and present the found difference in various ways.
 * The calling program is responsible for feeding the API pairs of files, one
 * from the "old" set and the corresponding one from "new" set, that are
 * different.
 * The library called through this API is called diffcore, and is responsible
 * for two things.
 *
 * - finding total rewrites (`-B`), renames (`-M`) and copies (`-C`), and
 * changes that touch a string (`-S`), as specified by the caller.
 *
 * - outputting the differences in various formats, as specified by the caller.
 *
 * Calling sequence
 * ----------------
 *
 * - Prepare `struct diff_options` to record the set of diff options, and then
 * call `repo_diff_setup()` to initialize this structure.  This sets up the
 * vanilla default.
 *
 * - Fill in the options structure to specify desired output format, rename
 * detection, etc.  `diff_opt_parse()` can be used to parse options given
 * from the command line in a way consistent with existing git-diff family
 * of programs.
 *
 * - Call `diff_setup_done()`; this inspects the options set up so far for
 * internal consistency and make necessary tweaking to it (e.g. if textual
 * patch output was asked, recursive behaviour is turned on); the callback
 * set_default in diff_options can be used to tweak this more.
 *
 * - As you find different pairs of files, call `diff_change()` to feed
 * modified files, `diff_addremove()` to feed created or deleted files, or
 * `diff_unmerge()` to feed a file whose state is 'unmerged' to the API.
 * These are thin wrappers to a lower-level `diff_queue()` function that is
 * flexible enough to record any of these kinds of changes.
 *
 * - Once you finish feeding the pairs of files, call `diffcore_std()`.
 * This will tell the diffcore library to go ahead and do its work.
 *
 * - Calling `diff_flush()` will produce the output.
 */

struct combine_diff_path;
struct commit;
struct diff_filespec;
struct diff_options;
struct diff_queue_struct;
struct oid_array;
struct option;
struct repository;
struct rev_info;
struct strbuf;
struct userdiff_driver;

typedef int (*pathchange_fn_t)(struct diff_options *options,
		 struct combine_diff_path *path);

typedef void (*change_fn_t)(struct diff_options *options,
		 unsigned old_mode, unsigned new_mode,
		 const struct object_id *old_oid,
		 const struct object_id *new_oid,
		 int old_oid_valid, int new_oid_valid,
		 const char *fullpath,
		 unsigned old_dirty_submodule, unsigned new_dirty_submodule);

typedef void (*add_remove_fn_t)(struct diff_options *options,
		    int addremove, unsigned mode,
		    const struct object_id *oid,
		    int oid_valid,
		    const char *fullpath, unsigned dirty_submodule);

typedef void (*diff_format_fn_t)(struct diff_queue_struct *q,
		struct diff_options *options, void *data);

typedef struct strbuf *(*diff_prefix_fn_t)(struct diff_options *opt, void *data);

#define DIFF_FORMAT_RAW		0x0001
#define DIFF_FORMAT_DIFFSTAT	0x0002
#define DIFF_FORMAT_NUMSTAT	0x0004
#define DIFF_FORMAT_SUMMARY	0x0008
#define DIFF_FORMAT_PATCH	0x0010
#define DIFF_FORMAT_SHORTSTAT	0x0020
#define DIFF_FORMAT_DIRSTAT	0x0040

/* These override all above */
#define DIFF_FORMAT_NAME	0x0100
#define DIFF_FORMAT_NAME_STATUS	0x0200
#define DIFF_FORMAT_CHECKDIFF	0x0400

/* Same as output_format = 0 but we know that -s flag was given
 * and we should not give default value to output_format.
 */
#define DIFF_FORMAT_NO_OUTPUT	0x0800

#define DIFF_FORMAT_CALLBACK	0x1000

#define DIFF_FLAGS_INIT { 0 }
struct diff_flags {

	/**
	 * Tells if tree traversal done by tree-diff should recursively descend
	 * into a tree object pair that are different in preimage and postimage set.
	 */
	unsigned recursive;
	unsigned tree_in_recursive;

	/* Affects the way how a file that is seemingly binary is treated. */
	unsigned binary;
	unsigned text;

	/**
	 * Tells the patch output format not to use abbreviated object names on the
	 * "index" lines.
	 */
	unsigned full_index;

	/* Affects if diff-files shows removed files. */
	unsigned silent_on_remove;

	/**
	 * Tells the diffcore library that the caller is feeding unchanged
	 * filepairs to allow copies from unmodified files be detected.
	 */
	unsigned find_copies_harder;

	unsigned follow_renames;
	unsigned rename_empty;

	/* Internal; used for optimization to see if there is any change. */
	unsigned has_changes;

	unsigned quick;

	/**
	 * Tells diff-files that the input is not tracked files but files in random
	 * locations on the filesystem.
	 */
	unsigned no_index;

	/**
	 * Tells output routine that it is Ok to call user specified patch output
	 * routine.  Plumbing disables this to ensure stable output.
	 */
	unsigned allow_external;

	/**
	 * For communication between the calling program and the options parser;
	 * tell the calling program to signal the presence of difference using
	 * program exit code.
	 */
	unsigned exit_with_status;

	/**
	 * Tells the library that the calling program is feeding the filepairs
	 * reversed; `one` is two, and `two` is one.
	 */
	unsigned reverse_diff;

	unsigned check_failed;
	unsigned relative_name;
	unsigned ignore_submodules;
	unsigned dirstat_cumulative;
	unsigned dirstat_by_file;
	unsigned allow_textconv;
	unsigned textconv_set_via_cmdline;
	unsigned diff_from_contents;
	unsigned dirty_submodules;
	unsigned ignore_untracked_in_submodules;
	unsigned ignore_dirty_submodules;
	unsigned override_submodule_config;
	unsigned dirstat_by_line;
	unsigned funccontext;
	unsigned default_follow_renames;
	unsigned stat_with_summary;
	unsigned suppress_diff_headers;
	unsigned dual_color_diffed_diffs;
	unsigned suppress_hunk_header_line_count;
};

static inline void diff_flags_or(struct diff_flags *a,
				 const struct diff_flags *b)
{
	char *tmp_a = (char *)a;
	const char *tmp_b = (const char *)b;
	int i;

	for (i = 0; i < sizeof(struct diff_flags); i++)
		tmp_a[i] |= tmp_b[i];
}

#define DIFF_XDL_TST(opts, flag)    ((opts)->xdl_opts & XDF_##flag)
#define DIFF_XDL_SET(opts, flag)    ((opts)->xdl_opts |= XDF_##flag)
#define DIFF_XDL_CLR(opts, flag)    ((opts)->xdl_opts &= ~XDF_##flag)

#define DIFF_WITH_ALG(opts, flag)   (((opts)->xdl_opts & ~XDF_DIFF_ALGORITHM_MASK) | XDF_##flag)

enum diff_words_type {
	DIFF_WORDS_NONE = 0,
	DIFF_WORDS_PORCELAIN,
	DIFF_WORDS_PLAIN,
	DIFF_WORDS_COLOR
};

enum diff_submodule_format {
	DIFF_SUBMODULE_SHORT = 0,
	DIFF_SUBMODULE_LOG,
	DIFF_SUBMODULE_INLINE_DIFF
};

/**
 * the set of options the calling program wants to affect the operation of
 * diffcore library with.
 */
struct diff_options {
	const char *orderfile;

	/**
	 * A constant string (can and typically does contain newlines to look for
	 * a block of text, not just a single line) to filter out the filepairs
	 * that do not change the number of strings contained in its preimage and
	 * postimage of the diff_queue.
	 */
	const char *pickaxe;

	const char *single_follow;
	const char *a_prefix, *b_prefix;
	const char *line_prefix;
	size_t line_prefix_length;

	/**
	 * collection of boolean options that affects the operation, but some do
	 * not have anything to do with the diffcore library.
	 */
	struct diff_flags flags;

	/* diff-filter bits */
	unsigned int filter;

	int use_color;

	/* Number of context lines to generate in patch output. */
	int context;

	int interhunkcontext;

	/* Affects the way detection logic for complete rewrites, renames and
	 * copies.
	 */
	int break_opt;
	int detect_rename;

	int irreversible_delete;
	int skip_stat_unmatch;
	int line_termination;

	/* The output format used when `diff_flush()` is run. */
	int output_format;

	unsigned pickaxe_opts;

	/* Affects the way detection logic for complete rewrites, renames and
	 * copies.
	 */
	int rename_score;
	int rename_limit;

	int needed_rename_limit;
	int degraded_cc_to_c;
	int show_rename_progress;
	int dirstat_permille;
	int setup;

	/* Number of hexdigits to abbreviate raw format output to. */
	int abbrev;

	/* If non-zero, then stop computing after this many changes. */
	int max_changes;
	/* For internal use only. */
	int num_changes;

	int ita_invisible_in_index;
/* white-space error highlighting */
#define WSEH_NEW (1<<12)
#define WSEH_CONTEXT (1<<13)
#define WSEH_OLD (1<<14)
	unsigned ws_error_highlight;
	const char *prefix;
	int prefix_length;
	const char *stat_sep;
	int xdl_opts;

	/* see Documentation/diff-options.txt */
	char **anchors;
	size_t anchors_nr, anchors_alloc;

	int stat_width;
	int stat_name_width;
	int stat_graph_width;
	int stat_count;
	const char *word_regex;
	enum diff_words_type word_diff;
	enum diff_submodule_format submodule_format;

	struct oidset *objfind;

	/* this is set by diffcore for DIFF_FORMAT_PATCH */
	int found_changes;

	/* to support internal diff recursion by --follow hack*/
	int found_follow;

	/* Callback which allows tweaking the options in diff_setup_done(). */
	void (*set_default)(struct diff_options *);

	FILE *file;
	int close_file;

#define OUTPUT_INDICATOR_NEW 0
#define OUTPUT_INDICATOR_OLD 1
#define OUTPUT_INDICATOR_CONTEXT 2
	char output_indicators[3];

	struct pathspec pathspec;
	pathchange_fn_t pathchange;
	change_fn_t change;
	add_remove_fn_t add_remove;
	void *change_fn_data;
	diff_format_fn_t format_callback;
	void *format_callback_data;
	diff_prefix_fn_t output_prefix;
	void *output_prefix_data;

	int diff_path_counter;

	struct emitted_diff_symbols *emitted_symbols;
	enum {
		COLOR_MOVED_NO = 0,
		COLOR_MOVED_PLAIN = 1,
		COLOR_MOVED_BLOCKS = 2,
		COLOR_MOVED_ZEBRA = 3,
		COLOR_MOVED_ZEBRA_DIM = 4,
	} color_moved;
	#define COLOR_MOVED_DEFAULT COLOR_MOVED_ZEBRA
	#define COLOR_MOVED_MIN_ALNUM_COUNT 20

	/* XDF_WHITESPACE_FLAGS regarding block detection are set at 2, 3, 4 */
	#define COLOR_MOVED_WS_ALLOW_INDENTATION_CHANGE (1<<5)
	#define COLOR_MOVED_WS_ERROR (1<<0)
	unsigned color_moved_ws_handling;

	struct repository *repo;
	struct option *parseopts;
};

unsigned diff_filter_bit(char status);

void diff_emit_submodule_del(struct diff_options *o, const char *line);
void diff_emit_submodule_add(struct diff_options *o, const char *line);
void diff_emit_submodule_untracked(struct diff_options *o, const char *path);
void diff_emit_submodule_modified(struct diff_options *o, const char *path);
void diff_emit_submodule_header(struct diff_options *o, const char *header);
void diff_emit_submodule_error(struct diff_options *o, const char *err);
void diff_emit_submodule_pipethrough(struct diff_options *o,
				     const char *line, int len);

struct diffstat_t {
	int nr;
	int alloc;
	struct diffstat_file {
		char *from_name;
		char *name;
		char *print_name;
		const char *comments;
		unsigned is_unmerged:1;
		unsigned is_binary:1;
		unsigned is_renamed:1;
		unsigned is_interesting:1;
		uintmax_t added, deleted;
	} **files;
};

enum color_diff {
	DIFF_RESET = 0,
	DIFF_CONTEXT = 1,
	DIFF_METAINFO = 2,
	DIFF_FRAGINFO = 3,
	DIFF_FILE_OLD = 4,
	DIFF_FILE_NEW = 5,
	DIFF_COMMIT = 6,
	DIFF_WHITESPACE = 7,
	DIFF_FUNCINFO = 8,
	DIFF_FILE_OLD_MOVED = 9,
	DIFF_FILE_OLD_MOVED_ALT = 10,
	DIFF_FILE_OLD_MOVED_DIM = 11,
	DIFF_FILE_OLD_MOVED_ALT_DIM = 12,
	DIFF_FILE_NEW_MOVED = 13,
	DIFF_FILE_NEW_MOVED_ALT = 14,
	DIFF_FILE_NEW_MOVED_DIM = 15,
	DIFF_FILE_NEW_MOVED_ALT_DIM = 16,
	DIFF_CONTEXT_DIM = 17,
	DIFF_FILE_OLD_DIM = 18,
	DIFF_FILE_NEW_DIM = 19,
	DIFF_CONTEXT_BOLD = 20,
	DIFF_FILE_OLD_BOLD = 21,
	DIFF_FILE_NEW_BOLD = 22,
};

const char *diff_get_color(int diff_use_color, enum color_diff ix);
#define diff_get_color_opt(o, ix) \
	diff_get_color((o)->use_color, ix)


const char *diff_line_prefix(struct diff_options *);


extern const char mime_boundary_leader[];

struct combine_diff_path *diff_tree_paths(
	struct combine_diff_path *p, const struct object_id *oid,
	const struct object_id **parents_oid, int nparent,
	struct strbuf *base, struct diff_options *opt);
void diff_tree_oid(const struct object_id *old_oid,
		   const struct object_id *new_oid,
		   const char *base, struct diff_options *opt);
void diff_root_tree_oid(const struct object_id *new_oid, const char *base,
			struct diff_options *opt);

struct combine_diff_path {
	struct combine_diff_path *next;
	char *path;
	unsigned int mode;
	struct object_id oid;
	struct combine_diff_parent {
		char status;
		unsigned int mode;
		struct object_id oid;
		struct strbuf path;
	} parent[FLEX_ARRAY];
};
#define combine_diff_path_size(n, l) \
	st_add4(sizeof(struct combine_diff_path), (l), 1, \
		st_mult(sizeof(struct combine_diff_parent), (n)))

void show_combined_diff(struct combine_diff_path *elem, int num_parent,
			int dense, struct rev_info *);

void diff_tree_combined(const struct object_id *oid, const struct oid_array *parents, int dense, struct rev_info *rev);

void diff_tree_combined_merge(const struct commit *commit, int dense, struct rev_info *rev);

void diff_set_mnemonic_prefix(struct diff_options *options, const char *a, const char *b);

int diff_can_quit_early(struct diff_options *);

void diff_addremove(struct diff_options *,
		    int addremove,
		    unsigned mode,
		    const struct object_id *oid,
		    int oid_valid,
		    const char *fullpath, unsigned dirty_submodule);

void diff_change(struct diff_options *,
		 unsigned mode1, unsigned mode2,
		 const struct object_id *old_oid,
		 const struct object_id *new_oid,
		 int old_oid_valid, int new_oid_valid,
		 const char *fullpath,
		 unsigned dirty_submodule1, unsigned dirty_submodule2);

struct diff_filepair *diff_unmerge(struct diff_options *, const char *path);

void compute_diffstat(struct diff_options *options, struct diffstat_t *diffstat,
		      struct diff_queue_struct *q);
void free_diffstat_info(struct diffstat_t *diffstat);

#define DIFF_SETUP_REVERSE      	1
#define DIFF_SETUP_USE_SIZE_CACHE	4

/*
 * Poor man's alternative to parse-option, to allow both stuck form
 * (--option=value) and separate form (--option value).
 */
int parse_long_opt(const char *opt, const char **argv,
		   const char **optarg);

int git_diff_basic_config(const char *var, const char *value, void *cb);
int git_diff_heuristic_config(const char *var, const char *value, void *cb);
void init_diff_ui_defaults(void);
int git_diff_ui_config(const char *var, const char *value, void *cb);
#ifndef NO_THE_REPOSITORY_COMPATIBILITY_MACROS
#define diff_setup(diffopts) repo_diff_setup(the_repository, diffopts)
#endif
void repo_diff_setup(struct repository *, struct diff_options *);
int diff_opt_parse(struct diff_options *, const char **, int, const char *);
void diff_setup_done(struct diff_options *);
int git_config_rename(const char *var, const char *value);

#define DIFF_DETECT_RENAME	1
#define DIFF_DETECT_COPY	2

#define DIFF_PICKAXE_ALL	1
#define DIFF_PICKAXE_REGEX	2

#define DIFF_PICKAXE_KIND_S	4 /* traditional plumbing counter */
#define DIFF_PICKAXE_KIND_G	8 /* grep in the patch */
#define DIFF_PICKAXE_KIND_OBJFIND	16 /* specific object IDs */

#define DIFF_PICKAXE_KINDS_MASK (DIFF_PICKAXE_KIND_S | \
				 DIFF_PICKAXE_KIND_G | \
				 DIFF_PICKAXE_KIND_OBJFIND)

#define DIFF_PICKAXE_IGNORE_CASE	32

void diffcore_std(struct diff_options *);
void diffcore_fix_diff_index(void);

#define COMMON_DIFF_OPTIONS_HELP \
"\ncommon diff options:\n" \
"  -z            output diff-raw with lines terminated with NUL.\n" \
"  -p            output patch format.\n" \
"  -u            synonym for -p.\n" \
"  --patch-with-raw\n" \
"                output both a patch and the diff-raw format.\n" \
"  --stat        show diffstat instead of patch.\n" \
"  --numstat     show numeric diffstat instead of patch.\n" \
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
"                show all files diff when -S is used and hit is found.\n" \
"  -a  --text    treat all files as text.\n"

int diff_queue_is_empty(void);
void diff_flush(struct diff_options*);
void diff_warn_rename_limit(const char *varname, int needed, int degraded_cc);

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

/*
 * This is different from find_unique_abbrev() in that
 * it stuffs the result with dots for alignment.
 */
const char *diff_aligned_abbrev(const struct object_id *sha1, int);

/* do not report anything on removed paths */
#define DIFF_SILENT_ON_REMOVED 01
/* report racily-clean paths as modified */
#define DIFF_RACY_IS_MODIFIED 02
int run_diff_files(struct rev_info *revs, unsigned int option);
int run_diff_index(struct rev_info *revs, int cached);

int do_diff_cache(const struct object_id *, struct diff_options *);
int diff_flush_patch_id(struct diff_options *, struct object_id *, int, int);
void flush_one_hunk(struct object_id *result, git_hash_ctx *ctx);

int diff_result_code(struct diff_options *, int);

int diff_no_index(struct rev_info *,
		  int implicit_no_index, int, const char **);

int index_differs_from(struct repository *r, const char *def,
		       const struct diff_flags *flags,
		       int ita_invisible_in_index);

/*
 * Fill the contents of the filespec "df", respecting any textconv defined by
 * its userdiff driver.  The "driver" parameter must come from a
 * previous call to get_textconv(), and therefore should either be NULL or have
 * textconv enabled.
 *
 * Note that the memory ownership of the resulting buffer depends on whether
 * the driver field is NULL. If it is, then the memory belongs to the filespec
 * struct. If it is non-NULL, then "outbuf" points to a newly allocated buffer
 * that should be freed by the caller.
 */
size_t fill_textconv(struct repository *r,
		     struct userdiff_driver *driver,
		     struct diff_filespec *df,
		     char **outbuf);

/*
 * Look up the userdiff driver for the given filespec, and return it if
 * and only if it has textconv enabled (otherwise return NULL). The result
 * can be passed to fill_textconv().
 */
struct userdiff_driver *get_textconv(struct repository *r,
				     struct diff_filespec *one);

/*
 * Prepare diff_filespec and convert it using diff textconv API
 * if the textconv driver exists.
 * Return 1 if the conversion succeeds, 0 otherwise.
 */
int textconv_object(struct repository *repo,
		    const char *path,
		    unsigned mode,
		    const struct object_id *oid, int oid_valid,
		    char **buf, unsigned long *buf_size);

int parse_rename_score(const char **cp_p);

long parse_algorithm_value(const char *value);

void print_stat_summary(FILE *fp, int files,
			int insertions, int deletions);
void setup_diff_pager(struct diff_options *);

#endif /* DIFF_H */
