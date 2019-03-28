#ifndef ADD_INTERACTIVE_H
#define ADD_INTERACTIVE_H

#include "color.h"

struct add_i_state {
	struct repository *r;
	int use_color;
	char header_color[COLOR_MAXLEN];
	char help_color[COLOR_MAXLEN];
	char prompt_color[COLOR_MAXLEN];
	char error_color[COLOR_MAXLEN];
	char reset_color[COLOR_MAXLEN];
	char fraginfo_color[COLOR_MAXLEN];
	char context_color[COLOR_MAXLEN];
	char file_old_color[COLOR_MAXLEN];
	char file_new_color[COLOR_MAXLEN];

	int use_single_key;
	char *interactive_diff_filter, *interactive_diff_algorithm;
};

void init_add_i_state(struct add_i_state *s, struct repository *r);
void clear_add_i_state(struct add_i_state *s);

struct repository;
struct pathspec;
int run_add_i(struct repository *r, const struct pathspec *ps);

enum add_p_mode {
	ADD_P_ADD,
	ADD_P_STASH,
	ADD_P_RESET,
	ADD_P_CHECKOUT,
	ADD_P_WORKTREE,
};

int run_add_p(struct repository *r, enum add_p_mode mode,
	      const char *revision, const struct pathspec *ps);

#endif
