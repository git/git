#ifndef STRUTIL_H
#define STRUTIL_H

extern int starts_with(const char *str, const char *prefix);

/*
 * If the string "str" begins with the string found in "prefix", return 1.
 * The "out" parameter is set to "str + strlen(prefix)" (i.e., to the point in
 * the string right after the prefix).
 *
 * Otherwise, return 0 and leave "out" untouched.
 *
 * Examples:
 *
 *   [extract branch name, fail if not a branch]
 *   if (!skip_prefix(ref, "refs/heads/", &branch)
 *	return -1;
 *
 *   [skip prefix if present, otherwise use whole string]
 *   skip_prefix(name, "refs/heads/", &name);
 */
static inline int skip_prefix(const char *str, const char *prefix,
			      const char **out)
{
	do {
		if (!*prefix) {
			*out = str;
			return 1;
		}
	} while (*str++ == *prefix++);
	return 0;
}

/*
 * If buf ends with suffix, return 1 and subtract the length of the suffix
 * from *len. Otherwise, return 0 and leave *len untouched.
 */
static inline int strip_suffix_mem(const char *buf, size_t *len,
				   const char *suffix)
{
	size_t suflen = strlen(suffix);
	if (*len < suflen || memcmp(buf + (*len - suflen), suffix, suflen))
		return 0;
	*len -= suflen;
	return 1;
}

/*
 * If str ends with suffix, return 1 and set *len to the size of the string
 * without the suffix. Otherwise, return 0 and set *len to the size of the
 * string.
 *
 * Note that we do _not_ NUL-terminate str to the new length.
 */
static inline int strip_suffix(const char *str, const char *suffix, size_t *len)
{
	*len = strlen(str);
	return strip_suffix_mem(str, len, suffix);
}

static inline int ends_with(const char *str, const char *suffix)
{
	size_t len;
	return strip_suffix(str, suffix, &len);
}

#endif /* STRUTIL_H */
