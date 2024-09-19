#ifndef REVISION_H
#define REVISION_H

#include "commit.h"
#include "grep.h"
#include "notes.h"
#include "oidset.h"
#include "pretty.h"
#include "diff.h"
#include "commit-slab-decl.h"
#include "decorate.h"
#include "ident.h"
#include "list-objects-filter-options.h"
#include "strvec.h"

/**
 * The revision walking API offers functions to build a list of revisions
 * and then iterate over that list.
 *
 * Calling sequence
 * ----------------
 *
 * The walking API has a given calling sequence: first you need to initialize
 * a rev_info structure, then add revisions to control what kind of revision
 * list do you want to get, finally you can iterate over the revision list.
 *
 */

/* Remember to update object flag allocation in object.h */
#define SEEN		(1u<<0)
#define UNINTERESTING   (1u<<1)
#define TREESAME	(1u<<2)
#define SHOWN		(1u<<3)
#define TMP_MARK	(1u<<4) /* for isolated cases; clean after use */
#define BOUNDARY	(1u<<5)
#define CHILD_SHOWN	(1u<<6)
#define ADDED		(1u<<7)	/* Parents already parsed and added? */
#define SYMMETRIC_LEFT	(1u<<8)
#define PATCHSAME	(1u<<9)
#define BOTTOM		(1u<<10)

/* WARNING: This is also used as REACHABLE in commit-graph.c. */
#define PULL_MERGE	(1u<<15)

#define TOPO_WALK_EXPLORED	(1u<<23)
#define TOPO_WALK_INDEGREE	(1u<<24)

/*
 * Indicates object was reached by traversal. i.e. not given by user on
 * command-line or stdin.
 */
#define NOT_USER_GIVEN	(1u<<25)
#define TRACK_LINEAR	(1u<<26)
#define ANCESTRY_PATH	(1u<<27)
#define ALL_REV_FLAGS	(((1u<<11)-1) | NOT_USER_GIVEN | TRACK_LINEAR | PULL_MERGE)

#define DECORATE_SHORT_REFS	1
#define DECORATE_FULL_REFS	2

struct log_info;
struct repository;
struct rev_info;
struct string_list;
struct saved_parents;
struct bloom_key;
struct bloom_filter_settings;
struct option;
struct parse_opt_ctx_t;
define_shared_commit_slab(revision_sources, char *);

struct rev_cmdline_info {
	unsigned int nr;
	unsigned int alloc;
	struct rev_cmdline_entry {
		struct object *item;
		const char *name;
		enum {
			REV_CMD_REF,
			REV_CMD_PARENTS_ONLY,
			REV_CMD_LEFT,
			REV_CMD_RIGHT,
			REV_CMD_MERGE_BASE,
			REV_CMD_REV
		} whence;
		unsigned flags;
	} *rev;
};

struct ref_exclusions {
	/*
	 * Excluded refs is a list of wildmatch patterns. If any of the
	 * patterns match, the reference will be excluded.
	 */
	struct string_list excluded_refs;

	/*
	 * Hidden refs is a list of patterns that is to be hidden via
	 * `ref_is_hidden()`.
	 */
	struct strvec hidden_refs;

	/*
	 * Indicates whether hidden refs have been configured. This is to
	 * distinguish between no hidden refs existing and hidden refs not
	 * being parsed.
	 */
	char hidden_refs_configured;
};

/**
 * Initialize a `struct ref_exclusions` with a macro.
 */
#define REF_EXCLUSIONS_INIT { \
	.excluded_refs = STRING_LIST_INIT_DUP, \
	.hidden_refs = STRVEC_INIT, \
}

struct oidset;
struct topo_walk_info;

struct rev_info {
	/* Starting list */
	struct commit_list *commits;
	struct object_array pending;
	struct repository *repo;

	/* Parents of shown commits */
	struct object_array boundary_commits;

	/* The end-points specified by the end user */
	struct rev_cmdline_info cmdline;

	/*
	 * Object filter options. No filtering is specified
	 * if and only if filter.choice is zero.
	 */
	struct list_objects_filter_options filter;

	/* excluding from --branches, --refs, etc. expansion */
	struct ref_exclusions ref_excludes;

	/* Basic information */
	const char *prefix;
	const char *def;
	char *ps_matched; /* optionally record matches of prune_data */
	struct pathspec prune_data;

	/*
	 * Whether the arguments parsed by setup_revisions() included any
	 * "input" revisions that might still have yielded an empty pending
	 * list (e.g., patterns like "--all" or "--glob").
	 */
	int rev_input_given;

	/*
	 * Whether we read from stdin due to the --stdin option.
	 */
	int read_from_stdin;

	/* topo-sort */
	enum rev_sort_order sort_order;

	unsigned int early_output;

	unsigned int	ignore_missing:1,
			ignore_missing_links:1;

	/* Traversal flags */
	unsigned int	dense:1,
			prune:1,
			no_walk:1,
			unsorted_input:1,
			remove_empty_trees:1,
			simplify_history:1,
			show_pulls:1,
			topo_order:1,
			simplify_merges:1,
			simplify_by_decoration:1,
			single_worktree:1,
			tag_objects:1,
			tree_objects:1,
			blob_objects:1,
			verify_objects:1,
			edge_hint:1,
			edge_hint_aggressive:1,
			limited:1,
			unpacked:1,
			no_kept_objects:1,
			boundary:2,
			count:1,
			left_right:1,
			left_only:1,
			right_only:1,
			rewrite_parents:1,
			print_parents:1,
			show_decorations:1,
			reverse:1,
			reverse_output_stage:1,
			cherry_pick:1,
			cherry_mark:1,
			bisect:1,
			ancestry_path:1,

			/* True if --ancestry-path was specified without an
			 * argument. The bottom revisions are implicitly
			 * the arguments in this case.
			 */
			ancestry_path_implicit_bottoms:1,

			first_parent_only:1,
			exclude_first_parent_only:1,
			line_level_traverse:1,
			tree_blobs_in_commit_order:1,

			/*
			 * Blobs are shown without regard for their existence.
			 * But not so for trees/commits: unless exclude_promisor_objects
			 * is set and the tree in question is a promisor object;
			 * OR ignore_missing_links is set, the revision walker
			 * dies with a "bad <type> object HASH" message when
			 * encountering a missing object. For callers that can
			 * handle missing trees/commits and want them to be filterable
			 * and showable, set this to true. The revision walker
			 * will filter and show such a missing object as usual,
			 * but will not attempt to recurse into this tree/commit
			 * object. The revision walker will also set the MISSING
			 * flag for such objects.
			 */
			do_not_die_on_missing_objects:1,

			/* for internal use only */
			exclude_promisor_objects:1;

	/* Diff flags */
	unsigned int	diff:1,
			full_diff:1,
			show_root_diff:1,
			match_missing:1,
			no_commit_id:1,
			verbose_header:1,
			always_show_header:1,
			/* Diff-merge flags */
			explicit_diff_merges: 1,
			merges_need_diff: 1,
			merges_imply_patch:1,
			separate_merges: 1,
			combine_merges:1,
			combined_all_paths:1,
			dense_combined_merges:1,
			first_parent_merges:1,
			remerge_diff:1;

	/* Format info */
	int		show_notes;
	unsigned int	shown_one:1,
			shown_dashes:1,
			show_merge:1,
			show_notes_given:1,
			show_notes_by_default:1,
			show_signature:1,
			pretty_given:1,
			abbrev_commit:1,
			abbrev_commit_given:1,
			zero_commit:1,
			use_terminator:1,
			missing_newline:1,
			date_mode_explicit:1,
			preserve_subject:1,
			force_in_body_from:1,
			encode_email_headers:1,
			include_header:1;
	unsigned int	disable_stdin:1;
	/* --show-linear-break */
	unsigned int	track_linear:1,
			track_first_time:1,
			linear:1;

	struct date_mode date_mode;
	int		expand_tabs_in_log; /* unset if negative */
	int		expand_tabs_in_log_default;

	unsigned int	abbrev;
	enum cmit_fmt	commit_format;
	struct log_info *loginfo;
	int		nr, total;
	const char	*mime_boundary;
	const char	*patch_suffix;
	int		numbered_files;
	const char	*reroll_count;
	char		*message_id;
	struct ident_split from_ident;
	struct string_list *ref_message_ids;
	int		add_signoff;
	const char	*extra_headers;
	const char	*log_reencode;
	const char	*subject_prefix;
	int		patch_name_max;
	int		no_inline;
	int		show_log_size;
	struct string_list *mailmap;

	/* Filter by commit log message */
	struct grep_opt	grep_filter;

	/* Display history graph */
	struct git_graph *graph;

	/* special limits */
	int skip_count;
	int max_count;
	timestamp_t max_age;
	timestamp_t max_age_as_filter;
	timestamp_t min_age;
	int min_parents;
	int max_parents;
	int (*include_check)(struct commit *, void *);
	int (*include_check_obj)(struct object *obj, void *);
	void *include_check_data;

	/* diff info for patches and for paths limiting */
	struct diff_options diffopt;
	struct diff_options pruning;

	struct reflog_walk_info *reflog_info;
	struct decoration children;
	struct decoration merge_simplification;
	struct decoration treesame;

	/* notes-specific options: which refs to show */
	struct display_notes_opt notes_opt;

	/* interdiff */
	const struct object_id *idiff_oid1;
	const struct object_id *idiff_oid2;
	const char *idiff_title;

	/* range-diff */
	const char *rdiff1;
	const char *rdiff2;
	int creation_factor;
	const char *rdiff_title;

	/* commit counts */
	int count_left;
	int count_right;
	int count_same;

	/* line level range that we are chasing */
	struct decoration line_log_data;

	/* copies of the parent lists, for --full-diff display */
	struct saved_parents *saved_parents_slab;

	struct commit_list *previous_parents;
	struct commit_list *ancestry_path_bottoms;
	const char *break_bar;

	struct revision_sources *sources;

	struct topo_walk_info *topo_walk_info;

	/* Commit graph bloom filter fields */
	/* The bloom filter key(s) for the pathspec */
	struct bloom_key *bloom_keys;
	int bloom_keys_nr;

	/*
	 * The bloom filter settings used to generate the key.
	 * This is loaded from the commit-graph being used.
	 */
	struct bloom_filter_settings *bloom_filter_settings;

	/* misc. flags related to '--no-kept-objects' */
	unsigned keep_pack_cache_flags;

	/* Location where temporary objects for remerge-diff are written. */
	struct tmp_objdir *remerge_objdir;

	/* Missing commits to be tracked without failing traversal. */
	struct oidset missing_commits;
};

/**
 * Initialize the "struct rev_info" structure with a macro.
 *
 * This will not fully initialize a "struct rev_info", the
 * repo_init_revisions() function needs to be called before
 * setup_revisions() and any revision walking takes place.
 *
 * Use REV_INFO_INIT to make the "struct rev_info" safe for passing to
 * release_revisions() when it's inconvenient (e.g. due to a "goto
 * cleanup" pattern) to arrange for repo_init_revisions() to be called
 * before release_revisions() is called.
 *
 * Initializing with this REV_INFO_INIT is redundant to invoking
 * repo_init_revisions(). If repo_init_revisions() is guaranteed to be
 * called before release_revisions() the "struct rev_info" can be left
 * uninitialized.
 */
#define REV_INFO_INIT { \
	.abbrev = DEFAULT_ABBREV, \
	.simplify_history = 1, \
	.pruning.flags.recursive = 1, \
	.pruning.flags.quick = 1, \
	.sort_order = REV_SORT_IN_GRAPH_ORDER, \
	.dense = 1, \
	.max_age = -1, \
	.max_age_as_filter = -1, \
	.min_age = -1, \
	.skip_count = -1, \
	.max_count = -1, \
	.max_parents = -1, \
	.expand_tabs_in_log = -1, \
	.commit_format = CMIT_FMT_DEFAULT, \
	.expand_tabs_in_log_default = 8, \
}

/**
 * Initialize a rev_info structure with default values. The third parameter may
 * be NULL or can be prefix path, and then the `.prefix` variable will be set
 * to it. This is typically the first function you want to call when you want
 * to deal with a revision list. After calling this function, you are free to
 * customize options, like set `.ignore_merges` to 0 if you don't want to
 * ignore merges, and so on.
 */
void repo_init_revisions(struct repository *r,
			 struct rev_info *revs,
			 const char *prefix);

/**
 * Parse revision information, filling in the `rev_info` structure, and
 * removing the used arguments from the argument list. Returns the number
 * of arguments left that weren't recognized, which are also moved to the
 * head of the argument list. The last parameter is used in case no
 * parameter given by the first two arguments.
 */
struct setup_revision_opt {
	const char *def;
	void (*tweak)(struct rev_info *);
	unsigned int	assume_dashdash:1,
			allow_exclude_promisor_objects:1,
			free_removed_argv_elements:1;
	unsigned revarg_opt;
};
int setup_revisions(int argc, const char **argv, struct rev_info *revs,
		    struct setup_revision_opt *);

/**
 * Free data allocated in a "struct rev_info" after it's been
 * initialized with repo_init_revisions() or REV_INFO_INIT.
 */
void release_revisions(struct rev_info *revs);

void parse_revision_opt(struct rev_info *revs, struct parse_opt_ctx_t *ctx,
			const struct option *options,
			const char * const usagestr[]);
#define REVARG_CANNOT_BE_FILENAME 01
#define REVARG_COMMITTISH 02
int handle_revision_arg(const char *arg, struct rev_info *revs,
			int flags, unsigned revarg_opt);
void revision_opts_finish(struct rev_info *revs);

/**
 * Reset the flags used by the revision walking api. You can use this to do
 * multiple sequential revision walks.
 */
void reset_revision_walk(void);

/**
 * Prepares the rev_info structure for a walk. You should check if it returns
 * any error (non-zero return code) and if it does not, you can start using
 * get_revision() to do the iteration.
 */
int prepare_revision_walk(struct rev_info *revs);

/**
 * Takes a pointer to a `rev_info` structure and iterates over it, returning a
 * `struct commit *` each time you call it. The end of the revision list is
 * indicated by returning a NULL pointer.
 */
struct commit *get_revision(struct rev_info *revs);

const char *get_revision_mark(const struct rev_info *revs,
			      const struct commit *commit);
void put_revision_mark(const struct rev_info *revs,
		       const struct commit *commit);

void mark_parents_uninteresting(struct rev_info *revs, struct commit *commit);
void mark_tree_uninteresting(struct repository *r, struct tree *tree);
void mark_trees_uninteresting_sparse(struct repository *r, struct oidset *trees);

void show_object_with_name(FILE *, struct object *, const char *);

/**
 * Helpers to check if a reference should be excluded.
 */

int ref_excluded(const struct ref_exclusions *exclusions, const char *path);
void init_ref_exclusions(struct ref_exclusions *);
void clear_ref_exclusions(struct ref_exclusions *);
void add_ref_exclusion(struct ref_exclusions *, const char *exclude);
void exclude_hidden_refs(struct ref_exclusions *, const char *section);

/**
 * This function can be used if you want to add commit objects as revision
 * information. You can use the `UNINTERESTING` object flag to indicate if
 * you want to include or exclude the given commit (and commits reachable
 * from the given commit) from the revision list.
 *
 * NOTE: If you have the commits as a string list then you probably want to
 * use setup_revisions(), instead of parsing each string and using this
 * function.
 */
void add_pending_object(struct rev_info *revs,
			struct object *obj, const char *name);

void add_pending_oid(struct rev_info *revs,
		     const char *name, const struct object_id *oid,
		     unsigned int flags);

void add_head_to_pending(struct rev_info *);
void add_reflogs_to_pending(struct rev_info *, unsigned int flags);
void add_index_objects_to_pending(struct rev_info *, unsigned int flags);

enum commit_action {
	commit_ignore,
	commit_show,
	commit_error
};

enum commit_action get_commit_action(struct rev_info *revs,
				     struct commit *commit);
enum commit_action simplify_commit(struct rev_info *revs,
				   struct commit *commit);

enum rewrite_result {
	rewrite_one_ok,
	rewrite_one_noparents,
	rewrite_one_error
};

typedef enum rewrite_result (*rewrite_parent_fn_t)(struct rev_info *revs, struct commit **pp);

int rewrite_parents(struct rev_info *revs,
		    struct commit *commit,
		    rewrite_parent_fn_t rewrite_parent);

/*
 * The log machinery saves the original parent list so that
 * get_saved_parents() can later tell what the real parents of the
 * commits are, when commit->parents has been modified by history
 * simplification.
 *
 * get_saved_parents() will transparently return commit->parents if
 * history simplification is off.
 */
struct commit_list *get_saved_parents(struct rev_info *revs, const struct commit *commit);

/**
 * Global for the (undocumented) "--early-output" flag for "git log".
 */
typedef void (*show_early_output_fn_t)(struct rev_info *, struct commit_list *);
extern volatile show_early_output_fn_t show_early_output;

#endif
