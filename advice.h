#ifndef ADVICE_H
#define ADVICE_H

extern int advice_push_nonfastforward;
extern int advice_status_hints;
extern int advice_commit_before_merge;
extern int advice_implicit_identity;

int git_default_advice_config(const char *var, const char *value);

#endif /* ADVICE_H */
