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
};

int init_add_i_state(struct repository *r, struct add_i_state *s);

enum color_add_i {
	COLOR_HEADER = 0,
	COLOR_HELP,
	COLOR_PROMPT,
	COLOR_ERROR,
	COLOR_RESET,
};
const char *get_add_i_color(enum color_add_i ix);

struct repository;
struct pathspec;
int run_add_i(struct repository *r, const struct pathspec *ps);
int run_add_p(struct repository *r, const struct pathspec *ps);

#endif
