#ifndef STATUS_H
#define STATUS_H

#include "string-list.h"
#include "color.h"
#include "pathspec.h"
#include "remote.h"

struct repository;
struct worktree;

enum color_wt_status {
	WT_STATUS_HEADER = 0,
	WT_STATUS_UPDATED,
	WT_STATUS_CHANGED,
	WT_STATUS_UNTRACKED,
	WT_STATUS_NOBRANCH,
	WT_STATUS_UNMERGED,
	WT_STATUS_LOCAL_BRANCH,
	WT_STATUS_REMOTE_BRANCH,
	WT_STATUS_ONBRANCH,
	WT_STATUS_MAXSLOT
};

enum untracked_status_type {
	SHOW_NO_UNTRACKED_FILES,
	SHOW_NORMAL_UNTRACKED_FILES,
	SHOW_ALL_UNTRACKED_FILES
};

enum show_ignored_type {
	SHOW_NO_IGNORED,
	SHOW_TRADITIONAL_IGNORED,
	SHOW_MATCHING_IGNORED,
};

/* from where does this commit originate */
enum commit_whence {
	FROM_COMMIT,     /* normal */
	FROM_MERGE,      /* commit came from merge */
	FROM_CHERRY_PICK /* commit came from cherry-pick */
};

struct wt_status_change_data {
	int worktree_status;
	int index_status;
	int stagemask;
	int mode_head, mode_index, mode_worktree;
	struct object_id oid_head, oid_index;
	int rename_status;
	int rename_score;
	char *rename_source;
	unsigned dirty_submodule       : 2;
	unsigned new_submodule_commits : 1;
};

enum wt_status_format {
	STATUS_FORMAT_NONE = 0,
	STATUS_FORMAT_LONG,
	STATUS_FORMAT_SHORT,
	STATUS_FORMAT_PORCELAIN,
	STATUS_FORMAT_PORCELAIN_V2,

	STATUS_FORMAT_UNSPECIFIED
};

#define HEAD_DETACHED_AT _("HEAD detached at ")
#define HEAD_DETACHED_FROM _("HEAD detached from ")

struct wt_status_state {
	int merge_in_progress;
	int am_in_progress;
	int am_empty_patch;
	int rebase_in_progress;
	int rebase_interactive_in_progress;
	int cherry_pick_in_progress;
	int bisect_in_progress;
	int revert_in_progress;
	int detached_at;
	char *branch;
	char *onto;
	char *detached_from;
	struct object_id detached_oid;
	struct object_id revert_head_oid;
	struct object_id cherry_pick_head_oid;
};

struct wt_status {
	struct repository *repo;
	int is_initial;
	char *branch;
	const char *reference;
	struct pathspec pathspec;
	int verbose;
	int amend;
	enum commit_whence whence;
	int nowarn;
	int use_color;
	int no_gettext;
	int display_comment_prefix;
	int relative_paths;
	int submodule_summary;
	enum show_ignored_type show_ignored_mode;
	enum untracked_status_type show_untracked_files;
	const char *ignore_submodule_arg;
	char color_palette[WT_STATUS_MAXSLOT][COLOR_MAXLEN];
	unsigned colopts;
	int null_termination;
	int commit_template;
	int show_branch;
	int show_stash;
	int hints;
	enum ahead_behind_flags ahead_behind_flags;
	int detect_rename;
	int rename_score;
	int rename_limit;
	enum wt_status_format status_format;
	struct wt_status_state state;
	unsigned char sha1_commit[GIT_MAX_RAWSZ]; /* when not Initial */

	/* These are computed during processing of the individual sections */
	int committable;
	int workdir_dirty;
	const char *index_file;
	FILE *fp;
	const char *prefix;
	struct string_list change;
	struct string_list untracked;
	struct string_list ignored;
	uint32_t untracked_in_ms;
};

size_t wt_status_locate_end(const char *s, size_t len);
void wt_status_append_cut_line(struct strbuf *buf);
void wt_status_add_cut_line(FILE *fp);
void wt_status_prepare(struct repository *r, struct wt_status *s);
void wt_status_print(struct wt_status *s);
void wt_status_collect(struct wt_status *s);
void wt_status_collect_free_buffers(struct wt_status *s);
void wt_status_get_state(struct repository *repo,
			 struct wt_status_state *state,
			 int get_detached_from);
int wt_status_check_rebase(const struct worktree *wt,
			   struct wt_status_state *state);
int wt_status_check_bisect(const struct worktree *wt,
			   struct wt_status_state *state);

__attribute__((format (printf, 3, 4)))
void status_printf_ln(struct wt_status *s, const char *color, const char *fmt, ...);
__attribute__((format (printf, 3, 4)))
void status_printf(struct wt_status *s, const char *color, const char *fmt, ...);

/* The following functions expect that the caller took care of reading the index. */
int has_unstaged_changes(struct repository *repo,
			 int ignore_submodules);
int has_uncommitted_changes(struct repository *repo,
			    int ignore_submodules);
int require_clean_work_tree(struct repository *repo,
			    const char *action,
			    const char *hint,
			    int ignore_submodules,
			    int gently);

#endif /* STATUS_H */
