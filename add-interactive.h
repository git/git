#ifndef ADD_INTERACTIVE_H
#define ADD_INTERACTIVE_H

#include "add-patch.h"
#include "color.h"

struct pathspec;
struct repository;

struct add_i_state {
	struct repository *r;
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

void init_add_i_state(struct add_i_state *s, struct repository *r,
		      struct add_p_opt *add_p_opt);
void clear_add_i_state(struct add_i_state *s);

int run_add_i(struct repository *r, const struct pathspec *ps,
	      struct add_p_opt *add_p_opt);

#endif
