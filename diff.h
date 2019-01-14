/*
 * Copyright (C) 2005 Junio C Hamano
 */
#ifndef DIFF_H
#define DIFF_H

#include "tree-walk.h"
#include "pathspec.h"
#include "object.h"
#include "oidset.h"

struct rev_info;
struct diff_options;
struct diff_queue_struct;
struct strbuf;
struct diff_filespec;
struct userdiff_driver;
struct oid_array;
struct commit;
struct combine_diff_path;
struct repository;

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
	unsigned recursive:1;
	unsigned tree_in_recursive:1;
	unsigned binary:1;
	unsigned text:1;
	unsigned full_index:1;
	unsigned silent_on_remove:1;
	unsigned find_copies_harder:1;
	unsigned follow_renames:1;
	unsigned rename_empty:1;
	unsigned has_changes:1;
	unsigned quick:1;
	unsigned no_index:1;
	unsigned allow_external:1;
	unsigned exit_with_status:1;
	unsigned reverse_diff:1;
	unsigned check_failed:1;
	unsigned relative_name:1;
	unsigned ignore_submodules:1;
	unsigned dirstat_cumulative:1;
	unsigned dirstat_by_file:1;
	unsigned allow_textconv:1;
	unsigned textconv_set_via_cmdline:1;
	unsigned diff_from_contents:1;
	unsigned dirty_submodules:1;
	unsigned ignore_untracked_in_submodules:1;
	unsigned ignore_dirty_submodules:1;
	unsigned override_submodule_config:1;
	unsigned dirstat_by_line:1;
	unsigned funccontext:1;
	unsigned default_follow_renames:1;
	unsigned stat_with_summary:1;
	unsigned suppress_diff_headers:1;
	unsigned dual_color_diffed_diffs:1;
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

struct diff_options {
	const char *orderfile;
	const char *pickaxe;
	const char *single_follow;
	const char *a_prefix, *b_prefix;
	const char *line_prefix;
	size_t line_prefix_length;
	struct diff_flags flags;

	/* diff-filter bits */
	unsigned int filter;

	int use_color;
	int context;
	int interhunkcontext;
	int break_opt;
	int detect_rename;
	int irreversible_delete;
	int skip_stat_unmatch;
	int line_termination;
	int output_format;
	unsigned pickaxe_opts;
	int rename_score;
	int rename_limit;
	int needed_rename_limit;
	int degraded_cc_to_c;
	int show_rename_progress;
	int dirstat_permille;
	int setup;
	int abbrev;
	int ita_invisible_in_index;
/* white-space error highlighting */
#define WSEH_NEW (1<<12)
#define WSEH_CONTEXT (1<<13)
#define WSEH_OLD (1<<14)
	unsigned ws_error_highlight;
	const char *prefix;
	int prefix_length;
	const char *stat_sep;
	long xdl_opts;

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
};

void diff_emit_submodule_del(struct diff_options *o, const char *line);
void diff_emit_submodule_add(struct diff_options *o, const char *line);
void diff_emit_submodule_untracked(struct diff_options *o, const char *path);
void diff_emit_submodule_modified(struct diff_options *o, const char *path);
void diff_emit_submodule_header(struct diff_options *o, const char *header);
void diff_emit_submodule_error(struct diff_options *o, const char *err);
void diff_emit_submodule_pipethrough(struct diff_options *o,
				     const char *line, int len);

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
int diff_tree_oid(const struct object_id *old_oid,
		  const struct object_id *new_oid,
		  const char *base, struct diff_options *opt);
int diff_root_tree_oid(const struct object_id *new_oid, const char *base,
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
void diffcore_fix_diff_index(struct diff_options *);

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
int diff_flush_patch_id(struct diff_options *, struct object_id *, int);

int diff_result_code(struct diff_options *, int);

void diff_no_index(struct repository *, struct rev_info *, int, const char **);

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
