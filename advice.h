#ifndef ADVICE_H
#define ADVICE_H

extern int advice_push_nonfastforward;
extern int advice_status_hints;

int git_default_advice_config(const char *var, const char *value);

#endif /* ADVICE_H */
