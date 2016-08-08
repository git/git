#include "cache.h"
#include "lockfile.h"
#include "apply.h"

static void git_apply_config(void)
{
	git_config_get_string_const("apply.whitespace", &apply_default_whitespace);
	git_config_get_string_const("apply.ignorewhitespace", &apply_default_ignorewhitespace);
	git_config(git_default_config, NULL);
}

int parse_whitespace_option(struct apply_state *state, const char *option)
{
	if (!option) {
		state->ws_error_action = warn_on_ws_error;
		return 0;
	}
	if (!strcmp(option, "warn")) {
		state->ws_error_action = warn_on_ws_error;
		return 0;
	}
	if (!strcmp(option, "nowarn")) {
		state->ws_error_action = nowarn_ws_error;
		return 0;
	}
	if (!strcmp(option, "error")) {
		state->ws_error_action = die_on_ws_error;
		return 0;
	}
	if (!strcmp(option, "error-all")) {
		state->ws_error_action = die_on_ws_error;
		state->squelch_whitespace_errors = 0;
		return 0;
	}
	if (!strcmp(option, "strip") || !strcmp(option, "fix")) {
		state->ws_error_action = correct_ws_error;
		return 0;
	}
	return error(_("unrecognized whitespace option '%s'"), option);
}

int parse_ignorewhitespace_option(struct apply_state *state,
				  const char *option)
{
	if (!option || !strcmp(option, "no") ||
	    !strcmp(option, "false") || !strcmp(option, "never") ||
	    !strcmp(option, "none")) {
		state->ws_ignore_action = ignore_ws_none;
		return 0;
	}
	if (!strcmp(option, "change")) {
		state->ws_ignore_action = ignore_ws_change;
		return 0;
	}
	return error(_("unrecognized whitespace ignore option '%s'"), option);
}

int init_apply_state(struct apply_state *state,
		     const char *prefix,
		     struct lock_file *lock_file)
{
	memset(state, 0, sizeof(*state));
	state->prefix = prefix;
	state->prefix_length = state->prefix ? strlen(state->prefix) : 0;
	state->lock_file = lock_file;
	state->newfd = -1;
	state->apply = 1;
	state->line_termination = '\n';
	state->p_value = 1;
	state->p_context = UINT_MAX;
	state->squelch_whitespace_errors = 5;
	state->ws_error_action = warn_on_ws_error;
	state->ws_ignore_action = ignore_ws_none;
	state->linenr = 1;
	string_list_init(&state->fn_table, 0);
	string_list_init(&state->limit_by_name, 0);
	string_list_init(&state->symlink_changes, 0);
	strbuf_init(&state->root, 0);

	git_apply_config();
	if (apply_default_whitespace && parse_whitespace_option(state, apply_default_whitespace))
		return -1;
	if (apply_default_ignorewhitespace && parse_ignorewhitespace_option(state, apply_default_ignorewhitespace))
		return -1;
	return 0;
}

void clear_apply_state(struct apply_state *state)
{
	string_list_clear(&state->limit_by_name, 0);
	string_list_clear(&state->symlink_changes, 0);
	strbuf_release(&state->root);

	/* &state->fn_table is cleared at the end of apply_patch() */
}

int check_apply_state(struct apply_state *state, int force_apply)
{
	int is_not_gitdir = !startup_info->have_repository;

	if (state->apply_with_reject && state->threeway)
		return error("--reject and --3way cannot be used together.");
	if (state->cached && state->threeway)
		return error("--cached and --3way cannot be used together.");
	if (state->threeway) {
		if (is_not_gitdir)
			return error(_("--3way outside a repository"));
		state->check_index = 1;
	}
	if (state->apply_with_reject)
		state->apply = state->apply_verbosely = 1;
	if (!force_apply && (state->diffstat || state->numstat || state->summary || state->check || state->fake_ancestor))
		state->apply = 0;
	if (state->check_index && is_not_gitdir)
		return error(_("--index outside a repository"));
	if (state->cached) {
		if (is_not_gitdir)
			return error(_("--cached outside a repository"));
		state->check_index = 1;
	}
	if (state->check_index)
		state->unsafe_paths = 0;
	if (!state->lock_file)
		return error("BUG: state->lock_file should not be NULL");

	return 0;
}
