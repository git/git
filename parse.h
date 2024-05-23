#ifndef PARSE_H
#define PARSE_H

int git_parse_signed(const char *value, intmax_t *ret, intmax_t max);
int git_parse_ssize_t(const char *, ssize_t *);
int git_parse_ulong(const char *, unsigned long *);
int git_parse_int(const char *value, int *ret);
int git_parse_int64(const char *value, int64_t *ret);
int git_parse_double(const char *value, double *ret);

/**
 * Same as `git_config_bool`, except that it returns -1 on error rather
 * than dying.
 */
int git_parse_maybe_bool(const char *);
int git_parse_maybe_bool_text(const char *value);

int git_env_bool(const char *, int);
unsigned long git_env_ulong(const char *, unsigned long);

#endif /* PARSE_H */
