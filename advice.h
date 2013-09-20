#ifndef ADVICE_H
#define ADVICE_H

#include "git-compat-util.h"

extern int advice_push_update_rejected;
extern int advice_push_non_ff_current;
extern int advice_push_non_ff_matching;
extern int advice_push_already_exists;
extern int advice_push_fetch_first;
extern int advice_push_needs_force;
extern int advice_status_hints;
extern int advice_status_u_option;
extern int advice_commit_before_merge;
extern int advice_resolve_conflict;
extern int advice_implicit_identity;
extern int advice_detached_head;
extern int advice_set_upstream_failure;
extern int advice_object_name_warning;
extern int advice_rm_hints;

int git_default_advice_config(const char *var, const char *value);
__attribute__((format (printf, 1, 2)))
void advise(const char *advice, ...);
int error_resolve_conflict(const char *me);
extern void NORETURN die_resolve_conflict(const char *me);
void detach_advice(const char *new_name);

#endif /* ADVICE_H */
