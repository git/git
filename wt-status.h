#ifndef STATUS_H
#define STATUS_H

#include <stdio.h>
#include "string-list.h"
#include "color.h"

enum color_wt_status {
	WT_STATUS_HEADER = 0,
	WT_STATUS_UPDATED,
	WT_STATUS_CHANGED,
	WT_STATUS_UNTRACKED,
	WT_STATUS_NOBRANCH,
	WT_STATUS_UNMERGED,
};

enum untracked_status_type {
	SHOW_NO_UNTRACKED_FILES,
	SHOW_NORMAL_UNTRACKED_FILES,
	SHOW_ALL_UNTRACKED_FILES
};

struct wt_status_change_data {
	int worktree_status;
	int index_status;
	int stagemask;
	char *head_path;
};

struct wt_status {
	int is_initial;
	char *branch;
	const char *reference;
	const char **pathspec;
	int verbose;
	int amend;
	int in_merge;
	int nowarn;
	int use_color;
	int relative_paths;
	int submodule_summary;
	enum untracked_status_type show_untracked_files;
	char color_palette[WT_STATUS_UNMERGED+1][COLOR_MAXLEN];

	/* These are computed during processing of the individual sections */
	int commitable;
	int workdir_dirty;
	int workdir_untracked;
	const char *index_file;
	FILE *fp;
	const char *prefix;
	struct string_list change;
	struct string_list untracked;
};

void wt_status_prepare(struct wt_status *s);
void wt_status_print(struct wt_status *s);
void wt_status_collect(struct wt_status *s);

void wt_shortstatus_print(struct wt_status *s, int null_termination);
void wt_porcelain_print(struct wt_status *s, int null_termination);

#endif /* STATUS_H */
