#ifndef PARSE_H
#define PARSE_H

bool git_parse_signed(const char *value, intmax_t *ret, intmax_t max);
bool git_parse_unsigned(const char *value, uintmax_t *ret, uintmax_t max);
bool git_parse_ssize_t(const char *, ssize_t *);
bool git_parse_ulong(const char *, unsigned long *);
bool git_parse_int(const char *value, int *ret);
bool git_parse_int64(const char *value, int64_t *ret);
bool git_parse_double(const char *value, double *ret);

/**
 * Same as `git_config_bool`, except that it returns -1 on error rather
 * than dying.
 */
int git_parse_maybe_bool(const char *);
int git_parse_maybe_bool_text(const char *value);

int git_env_bool(const char *, int);
unsigned long git_env_ulong(const char *, unsigned long);

/*
 * These functions parse an integer from a buffer that does not need to be
 * NUL-terminated. They return true on success, or false if no integer is found
 * (in which case errno is set to EINVAL) or if the integer is out of the
 * allowable range (in which case errno is ERANGE).
 *
 * You must pass in a non-NULL value for "ep", which returns a pointer to the
 * next character in the buf (similar to strtol(), etc).
 *
 * These functions always parse in base 10 (and do not allow input like "0xff"
 * to switch to base 16). They do not allow unit suffixes like git_parse_int(),
 * above.
 */
bool parse_unsigned_from_buf(const char *buf, size_t len, const char **ep, uintmax_t *ret, uintmax_t max);
bool parse_signed_from_buf(const char *buf, size_t len, const char **ep, intmax_t *ret, intmax_t max);
bool parse_int_from_buf(const char *buf, size_t len, const char **ep, int *ret);

#endif /* PARSE_H */
