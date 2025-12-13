#ifndef ADD_PATCH_H
#define ADD_PATCH_H

#include "color.h"

struct index_state;
struct pathspec;
struct repository;

struct interactive_options {
	int context;
	int interhunkcontext;
};

#define INTERACTIVE_OPTIONS_INIT { \
	.context = -1, \
	.interhunkcontext = -1, \
}

struct interactive_config {
	enum git_colorbool use_color_interactive;
	enum git_colorbool use_color_diff;
	char header_color[COLOR_MAXLEN];
	char help_color[COLOR_MAXLEN];
	char prompt_color[COLOR_MAXLEN];
	char error_color[COLOR_MAXLEN];
	char reset_color_interactive[COLOR_MAXLEN];

	char fraginfo_color[COLOR_MAXLEN];
	char context_color[COLOR_MAXLEN];
	char file_old_color[COLOR_MAXLEN];
	char file_new_color[COLOR_MAXLEN];
	char reset_color_diff[COLOR_MAXLEN];

	int use_single_key;
	char *interactive_diff_filter, *interactive_diff_algorithm;
	int context, interhunkcontext;
};

void interactive_config_init(struct interactive_config *cfg,
			     struct repository *r,
			     struct interactive_options *opts);
void interactive_config_clear(struct interactive_config *cfg);

enum add_p_mode {
	ADD_P_ADD,
	ADD_P_STASH,
	ADD_P_RESET,
	ADD_P_CHECKOUT,
	ADD_P_WORKTREE,
};

enum add_p_flags {
	/* Disallow "editing" hunks. */
	ADD_P_DISALLOW_EDIT = (1 << 0),
};

int run_add_p(struct repository *r, enum add_p_mode mode,
	      struct interactive_options *opts, const char *revision,
	      const struct pathspec *ps,
	      unsigned flags);

int run_add_p_index(struct repository *r,
		    struct index_state *index,
		    const char *index_file,
		    struct interactive_options *opts,
		    const char *revision,
		    const struct pathspec *ps,
		    unsigned flags);

#endif
