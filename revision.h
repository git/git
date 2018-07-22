#ifndef REVISION_H
#define REVISION_H

#include "parse-options.h"
#include "grep.h"
#include "notes.h"
#include "pretty.h"
#include "diff.h"
#include "commit-slab-decl.h"

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
#define USER_GIVEN	(1u<<25) /* given directly by the user */
#define TRACK_LINEAR	(1u<<26)
#define ALL_REV_FLAGS	(((1u<<11)-1) | USER_GIVEN | TRACK_LINEAR)

#define DECORATE_SHORT_REFS	1
#define DECORATE_FULL_REFS	2

struct rev_info;
struct log_info;
struct string_list;
struct saved_parents;
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

#define REVISION_WALK_WALK 0
#define REVISION_WALK_NO_WALK_SORTED 1
#define REVISION_WALK_NO_WALK_UNSORTED 2

struct rev_info {
	/* Starting list */
	struct commit_list *commits;
	struct object_array pending;

	/* Parents of shown commits */
	struct object_array boundary_commits;

	/* The end-points specified by the end user */
	struct rev_cmdline_info cmdline;

	/* excluding from --branches, --refs, etc. expansion */
	struct string_list *ref_excludes;

	/* Basic information */
	const char *prefix;
	const char *def;
	struct pathspec prune_data;

	/*
	 * Whether the arguments parsed by setup_revisions() included any
	 * "input" revisions that might still have yielded an empty pending
	 * list (e.g., patterns like "--all" or "--glob").
	 */
	int rev_input_given;

	/* topo-sort */
	enum rev_sort_order sort_order;

	unsigned int early_output;

	unsigned int	ignore_missing:1,
			ignore_missing_links:1;

	/* Traversal flags */
	unsigned int	dense:1,
			prune:1,
			no_walk:2,
			remove_empty_trees:1,
			simplify_history:1,
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
			first_parent_only:1,
			line_level_traverse:1,
			tree_blobs_in_commit_order:1,

			/* for internal use only */
			exclude_promisor_objects:1;

	/* Diff flags */
	unsigned int	diff:1,
			full_diff:1,
			show_root_diff:1,
			no_commit_id:1,
			verbose_header:1,
			ignore_merges:1,
			combine_merges:1,
			dense_combined_merges:1,
			always_show_header:1;

	/* Format info */
	unsigned int	shown_one:1,
			shown_dashes:1,
			show_merge:1,
			show_notes:1,
			show_notes_given:1,
			show_signature:1,
			pretty_given:1,
			abbrev_commit:1,
			abbrev_commit_given:1,
			zero_commit:1,
			use_terminator:1,
			missing_newline:1,
			date_mode_explicit:1,
			preserve_subject:1;
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
	int		reroll_count;
	char		*message_id;
	struct ident_split from_ident;
	struct string_list *ref_message_ids;
	int		add_signoff;
	const char	*extra_headers;
	const char	*log_reencode;
	const char	*subject_prefix;
	int		no_inline;
	int		show_log_size;
	struct string_list *mailmap;

	/* Filter by commit log message */
	struct grep_opt	grep_filter;
	/* Negate the match of grep_filter */
	int invert_grep;

	/* Display history graph */
	struct git_graph *graph;

	/* special limits */
	int skip_count;
	int max_count;
	timestamp_t max_age;
	timestamp_t min_age;
	int min_parents;
	int max_parents;
	int (*include_check)(struct commit *, void *);
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
	const char *break_bar;

	struct revision_sources *sources;
};

extern int ref_excluded(struct string_list *, const char *path);
void clear_ref_exclusion(struct string_list **);
void add_ref_exclusion(struct string_list **, const char *exclude);


#define REV_TREE_SAME		0
#define REV_TREE_NEW		1	/* Only new files */
#define REV_TREE_OLD		2	/* Only files removed */
#define REV_TREE_DIFFERENT	3	/* Mixed changes */

/* revision.c */
typedef void (*show_early_output_fn_t)(struct rev_info *, struct commit_list *);
extern volatile show_early_output_fn_t show_early_output;

struct setup_revision_opt {
	const char *def;
	void (*tweak)(struct rev_info *, struct setup_revision_opt *);
	const char *submodule;
	int assume_dashdash;
	unsigned revarg_opt;
};

extern void init_revisions(struct rev_info *revs, const char *prefix);
extern int setup_revisions(int argc, const char **argv, struct rev_info *revs,
			   struct setup_revision_opt *);
extern void parse_revision_opt(struct rev_info *revs, struct parse_opt_ctx_t *ctx,
			       const struct option *options,
			       const char * const usagestr[]);
#define REVARG_CANNOT_BE_FILENAME 01
#define REVARG_COMMITTISH 02
extern int handle_revision_arg(const char *arg, struct rev_info *revs,
			       int flags, unsigned revarg_opt);

extern void reset_revision_walk(void);
extern int prepare_revision_walk(struct rev_info *revs);
extern struct commit *get_revision(struct rev_info *revs);
extern char *get_revision_mark(const struct rev_info *revs,
			       const struct commit *commit);
extern void put_revision_mark(const struct rev_info *revs,
			      const struct commit *commit);

extern void mark_parents_uninteresting(struct commit *commit);
extern void mark_tree_uninteresting(struct tree *tree);

extern void show_object_with_name(FILE *, struct object *, const char *);

extern void add_pending_object(struct rev_info *revs,
			       struct object *obj, const char *name);
extern void add_pending_oid(struct rev_info *revs,
			    const char *name, const struct object_id *oid,
			    unsigned int flags);

extern void add_head_to_pending(struct rev_info *);
extern void add_reflogs_to_pending(struct rev_info *, unsigned int flags);
extern void add_index_objects_to_pending(struct rev_info *, unsigned int flags);

enum commit_action {
	commit_ignore,
	commit_show,
	commit_error
};

extern enum commit_action get_commit_action(struct rev_info *revs,
					    struct commit *commit);
extern enum commit_action simplify_commit(struct rev_info *revs,
					  struct commit *commit);

enum rewrite_result {
	rewrite_one_ok,
	rewrite_one_noparents,
	rewrite_one_error
};

typedef enum rewrite_result (*rewrite_parent_fn_t)(struct rev_info *revs, struct commit **pp);

extern int rewrite_parents(struct rev_info *revs, struct commit *commit,
	rewrite_parent_fn_t rewrite_parent);

/*
 * The log machinery saves the original parent list so that
 * get_saved_parents() can later tell what the real parents of the
 * commits are, when commit->parents has been modified by history
 * simpification.
 *
 * get_saved_parents() will transparently return commit->parents if
 * history simplification is off.
 */
extern struct commit_list *get_saved_parents(struct rev_info *revs, const struct commit *commit);

#endif
