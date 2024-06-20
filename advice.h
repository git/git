#ifndef ADVICE_H
#define ADVICE_H

struct string_list;

/*
 * To add a new advice, you need to:
 * Define a new advice_type.
 * Add a new entry to advice_setting array.
 * Add the new config variable to Documentation/config/advice.txt.
 * Call advise_if_enabled to print your advice.
 */
enum advice_type {
	ADVICE_ADD_EMBEDDED_REPO,
	ADVICE_ADD_EMPTY_PATHSPEC,
	ADVICE_ADD_IGNORED_FILE,
	ADVICE_AMBIGUOUS_FETCH_REFSPEC,
	ADVICE_AM_WORK_DIR,
	ADVICE_CHECKOUT_AMBIGUOUS_REMOTE_BRANCH_NAME,
	ADVICE_COMMIT_BEFORE_MERGE,
	ADVICE_DETACHED_HEAD,
	ADVICE_DIVERGING,
	ADVICE_FETCH_SHOW_FORCED_UPDATES,
	ADVICE_FORCE_DELETE_BRANCH,
	ADVICE_GRAFT_FILE_DEPRECATED,
	ADVICE_IGNORED_HOOK,
	ADVICE_IMPLICIT_IDENTITY,
	ADVICE_MERGE_CONFLICT,
	ADVICE_NESTED_TAG,
	ADVICE_OBJECT_NAME_WARNING,
	ADVICE_PUSH_ALREADY_EXISTS,
	ADVICE_PUSH_FETCH_FIRST,
	ADVICE_PUSH_NEEDS_FORCE,
	ADVICE_PUSH_NON_FF_CURRENT,
	ADVICE_PUSH_NON_FF_MATCHING,
	ADVICE_PUSH_REF_NEEDS_UPDATE,
	ADVICE_PUSH_UNQUALIFIED_REF_NAME,
	ADVICE_PUSH_UPDATE_REJECTED,
	ADVICE_PUSH_UPDATE_REJECTED_ALIAS,
	ADVICE_REBASE_TODO_ERROR,
	ADVICE_REF_SYNTAX,
	ADVICE_RESET_NO_REFRESH_WARNING,
	ADVICE_RESOLVE_CONFLICT,
	ADVICE_RM_HINTS,
	ADVICE_SEQUENCER_IN_USE,
	ADVICE_SET_UPSTREAM_FAILURE,
	ADVICE_SKIPPED_CHERRY_PICKS,
	ADVICE_STATUS_AHEAD_BEHIND_WARNING,
	ADVICE_STATUS_HINTS,
	ADVICE_STATUS_U_OPTION,
	ADVICE_SUBMODULES_NOT_UPDATED,
	ADVICE_SUBMODULE_ALTERNATE_ERROR_STRATEGY_DIE,
	ADVICE_SUBMODULE_MERGE_CONFLICT,
	ADVICE_SUGGEST_DETACHING_HEAD,
	ADVICE_UPDATE_SPARSE_PATH,
	ADVICE_WAITING_FOR_EDITOR,
	ADVICE_WORKTREE_ADD_ORPHAN,
};

int git_default_advice_config(const char *var, const char *value);
__attribute__((format (printf, 1, 2)))
void advise(const char *advice, ...);

/**
 * Checks if advice type is enabled (can be printed to the user).
 * Should be called before advise().
 */
int advice_enabled(enum advice_type type);

/**
 * Checks the visibility of the advice before printing.
 */
__attribute__((format (printf, 2, 3)))
void advise_if_enabled(enum advice_type type, const char *advice, ...);

int error_resolve_conflict(const char *me);
void NORETURN die_resolve_conflict(const char *me);
void NORETURN die_conclude_merge(void);
void NORETURN die_ff_impossible(void);
void advise_on_updating_sparse_paths(struct string_list *pathspec_list);
void detach_advice(const char *new_name);
void advise_on_moving_dirty_path(struct string_list *pathspec_list);

#endif /* ADVICE_H */
