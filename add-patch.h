#ifndef ADD_PATCH_H
#define ADD_PATCH_H

struct pathspec;
struct repository;

struct add_p_opt {
	int context;
	int interhunkcontext;
};

#define ADD_P_OPT_INIT { .context = -1, .interhunkcontext = -1 }

enum add_p_mode {
	ADD_P_ADD,
	ADD_P_STASH,
	ADD_P_RESET,
	ADD_P_CHECKOUT,
	ADD_P_WORKTREE,
};

int run_add_p(struct repository *r, enum add_p_mode mode,
	      struct add_p_opt *o, const char *revision,
	      const struct pathspec *ps);

#endif
