#ifndef APPLY_H
#define APPLY_H

#include "hash.h"
#include "lockfile.h"
#include "string-list.h"
#include "strmap.h"

struct repository;

enum apply_ws_error_action {
	nowarn_ws_error,
	warn_on_ws_error,
	die_on_ws_error,
	correct_ws_error
};

enum apply_ws_ignore { ignore_ws_none, ignore_ws_change };

enum apply_verbosity {
	verbosity_silent = -1,
	verbosity_normal = 0,
	verbosity_verbose = 1
};

struct apply_state {
	const char *prefix;

	/* Lock file */
	struct lock_file lock_file;

	/* These control what gets looked at and modified */
	int apply; /* this is not a dry-run */
	int cached; /* apply to the index only */
	int check; /* preimage must match working tree, don't actually apply */
	int check_index; /* preimage must match the indexed version */
	int update_index; /* check_index && apply */
	int ita_only; /* add intent-to-add entries to the index */

	/* These control cosmetic aspect of the output */
	int diffstat; /* just show a diffstat, and don't actually apply */
	int numstat; /* just show a numeric diffstat, and don't actually apply
		      */
	int summary; /* just report creation, deletion, etc, and don't actually
			apply */

	/* These boolean parameters control how the apply is done */
	int allow_overlap;
	int apply_in_reverse;
	int apply_with_reject;
	int no_add;
	int threeway;
	int unidiff_zero;
	int unsafe_paths;
	int allow_empty;

	/* Other non boolean parameters */
	struct repository *repo;
	const char *index_file;
	enum apply_verbosity apply_verbosity;
	int merge_variant;
	char *fake_ancestor;
	const char *patch_input_file;
	int line_termination;
	struct strbuf root;
	int p_value;
	int p_value_known;
	unsigned int p_context;

	/* Exclude and include path parameters */
	struct string_list limit_by_name;
	int has_include;

	/* Various "current state" */
	int linenr; /* current line number */
	/*
	 * We need to keep track of how symlinks in the preimage are
	 * manipulated by the patches.  A patch to add a/b/c where a/b
	 * is a symlink should not be allowed to affect the directory
	 * the symlink points at, but if the same patch removes a/b,
	 * it is perfectly fine, as the patch removes a/b to make room
	 * to create a directory a/b so that a/b/c can be created.
	 */
	struct strset removed_symlinks;
	struct strset kept_symlinks;

	/*
	 * For "diff-stat" like behaviour, we keep track of the biggest change
	 * we've seen, and the longest filename. That allows us to do simple
	 * scaling.
	 */
	int max_change;
	int max_len;

	/*
	 * Records filenames that have been touched, in order to handle
	 * the case where more than one patches touch the same file.
	 */
	struct string_list fn_table;

	/*
	 * This is to save reporting routines before using
	 * set_error_routine() or set_warn_routine() to install muting
	 * routines when in verbosity_silent mode.
	 */
	void (*saved_error_routine)(const char *err, va_list params);
	void (*saved_warn_routine)(const char *warn, va_list params);

	/* These control whitespace errors */
	enum apply_ws_error_action ws_error_action;
	enum apply_ws_ignore ws_ignore_action;
	const char *whitespace_option;
	int whitespace_error;
	int squelch_whitespace_errors;
	int applied_after_fixing_ws;
};

/*
 * This represents a "patch" to a file, both metainfo changes
 * such as creation/deletion, filemode and content changes represented
 * as a series of fragments.
 */
struct patch {
	char *new_name, *old_name, *def_name;
	unsigned int old_mode, new_mode;
	int is_new, is_delete; /* -1 = unknown, 0 = false, 1 = true */
	int rejected;
	unsigned ws_rule;
	int lines_added, lines_deleted;
	int score;
	int extension_linenr; /* first line specifying delete/new/rename/copy */
	unsigned int is_toplevel_relative:1;
	unsigned int inaccurate_eof:1;
	unsigned int is_binary:1;
	unsigned int is_copy:1;
	unsigned int is_rename:1;
	unsigned int recount:1;
	unsigned int conflicted_threeway:1;
	unsigned int direct_to_threeway:1;
	unsigned int crlf_in_old:1;
	struct fragment *fragments;
	char *result;
	size_t resultsize;
	char old_oid_prefix[GIT_MAX_HEXSZ + 1];
	char new_oid_prefix[GIT_MAX_HEXSZ + 1];
	struct patch *next;

	/* three-way fallback result */
	struct object_id threeway_stage[3];
};

int apply_parse_options(int argc, const char **argv, struct apply_state *state,
			int *force_apply, int *options,
			const char *const *apply_usage);
int init_apply_state(struct apply_state *state, struct repository *repo,
		     const char *prefix);
void clear_apply_state(struct apply_state *state);
int check_apply_state(struct apply_state *state, int force_apply);

/*
 * Parse a git diff header, starting at line.  Fills the relevant
 * metadata information in 'struct patch'.
 *
 * Returns -1 on failure, the length of the parsed header otherwise.
 */
int parse_git_diff_header(struct strbuf *root, int *linenr, int p_value,
			  const char *line, int len, unsigned int size,
			  struct patch *patch);

void release_patch(struct patch *patch);

/*
 * Some aspects of the apply behavior are controlled by the following
 * bits in the "options" parameter passed to apply_all_patches().
 */
#define APPLY_OPT_INACCURATE_EOF (1 << 0) /* accept inaccurate eof */
#define APPLY_OPT_RECOUNT (1 << 1) /* accept inaccurate line count */

int apply_all_patches(struct apply_state *state, int argc, const char **argv,
		      int options);

#endif
