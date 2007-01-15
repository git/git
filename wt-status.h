#ifndef STATUS_H
#define STATUS_H

enum color_wt_status {
	WT_STATUS_HEADER,
	WT_STATUS_UPDATED,
	WT_STATUS_CHANGED,
	WT_STATUS_UNTRACKED,
};

struct wt_status {
	int is_initial;
	char *branch;
	const char *reference;
	int verbose;
	int amend;
	int untracked;
	/* These are computed during processing of the individual sections */
	int commitable;
	int workdir_dirty;
	int workdir_untracked;
};

int git_status_config(const char *var, const char *value);
void wt_status_prepare(struct wt_status *s);
void wt_status_print(struct wt_status *s);

#endif /* STATUS_H */
