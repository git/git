#ifndef REVISION_H
#define REVISION_H

#include "parse-options.h"
#include "grep.h"
#include "notes.h"

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
#define ALL_REV_FLAGS	((1u<<10)-1)

#define DECORATE_SHORT_REFS	1
#define DECORATE_FULL_REFS	2

struct rev_info;
struct log_info;
struct string_list;

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
			REV_CMD_REV
		} whence;
		unsigned flags;
	} *rev;
};

struct rev_info {
	/* Starting list */
	struct commit_list *commits;
	struct object_array pending;

	/* Parents of shown commits */
	struct object_array boundary_commits;

	/* The end-points specified by the end user */
	struct rev_cmdline_info cmdline;

	/* Basic information */
	const char *prefix;
	const char *def;
	struct pathspec prune_data;
	unsigned int	early_output:1,
			ignore_missing:1;

	/* Traversal flags */
	unsigned int	dense:1,
			prune:1,
			no_walk:1,
			show_all:1,
			remove_empty_trees:1,
			simplify_history:1,
			lifo:1,
			topo_order:1,
			simplify_merges:1,
			simplify_by_decoration:1,
			tag_objects:1,
			tree_objects:1,
			blob_objects:1,
			edge_hint:1,
			limited:1,
			unpacked:1,
			boundary:2,
			count:1,
			left_right:1,
			left_only:1,
			right_only:1,
			rewrite_parents:1,
			print_parents:1,
			show_source:1,
			show_decorations:1,
			reverse:1,
			reverse_output_stage:1,
			cherry_pick:1,
			cherry_mark:1,
			bisect:1,
			ancestry_path:1,
			first_parent_only:1;

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
			show_merge:1,
			show_notes:1,
			show_notes_given:1,
			pretty_given:1,
			abbrev_commit:1,
			abbrev_commit_given:1,
			use_terminator:1,
			missing_newline:1,
			date_mode_explicit:1,
			preserve_subject:1;
	unsigned int	disable_stdin:1;

	enum date_mode date_mode;

	unsigned int	abbrev;
	enum cmit_fmt	commit_format;
	struct log_info *loginfo;
	int		nr, total;
	const char	*mime_boundary;
	const char	*patch_suffix;
	int		numbered_files;
	char		*message_id;
	struct string_list *ref_message_ids;
	const char	*add_signoff;
	const char	*extra_headers;
	const char	*log_reencode;
	const char	*subject_prefix;
	int		no_inline;
	int		show_log_size;

	/* Filter by commit log message */
	struct grep_opt	grep_filter;

	/* Display history graph */
	struct git_graph *graph;

	/* special limits */
	int skip_count;
	int max_count;
	unsigned long max_age;
	unsigned long min_age;
	int min_parents;
	int max_parents;

	/* diff info for patches and for paths limiting */
	struct diff_options diffopt;
	struct diff_options pruning;

	struct reflog_walk_info *reflog_info;
	struct decoration children;
	struct decoration merge_simplification;

	/* notes-specific options: which refs to show */
	struct display_notes_opt notes_opt;

	/* commit counts */
	int count_left;
	int count_right;
	int count_same;
};

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
};

extern void init_revisions(struct rev_info *revs, const char *prefix);
extern int setup_revisions(int argc, const char **argv, struct rev_info *revs, struct setup_revision_opt *);
extern void parse_revision_opt(struct rev_info *revs, struct parse_opt_ctx_t *ctx,
				 const struct option *options,
				 const char * const usagestr[]);
extern int handle_revision_arg(const char *arg, struct rev_info *revs,int flags,int cant_be_filename);

extern int prepare_revision_walk(struct rev_info *revs);
extern struct commit *get_revision(struct rev_info *revs);
extern char *get_revision_mark(const struct rev_info *revs, const struct commit *commit);
extern void put_revision_mark(const struct rev_info *revs, const struct commit *commit);

extern void mark_parents_uninteresting(struct commit *commit);
extern void mark_tree_uninteresting(struct tree *tree);

struct name_path {
	struct name_path *up;
	int elem_len;
	const char *elem;
};

char *path_name(const struct name_path *path, const char *name);

extern void show_object_with_name(FILE *, struct object *, const struct name_path *, const char *);

extern void add_object(struct object *obj,
		       struct object_array *p,
		       struct name_path *path,
		       const char *name);

extern void add_pending_object(struct rev_info *revs, struct object *obj, const char *name);

extern void add_head_to_pending(struct rev_info *);

enum commit_action {
	commit_ignore,
	commit_show,
	commit_error
};

extern enum commit_action get_commit_action(struct rev_info *revs, struct commit *commit);
extern enum commit_action simplify_commit(struct rev_info *revs, struct commit *commit);

#endif
