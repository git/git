#include "git-compat-util.h"
#include "gettext.h"
#include "parse.h"

static uintmax_t get_unit_factor(const char *end)
{
	if (!*end)
		return 1;
	else if (!strcasecmp(end, "k"))
		return 1024;
	else if (!strcasecmp(end, "m"))
		return 1024 * 1024;
	else if (!strcasecmp(end, "g"))
		return 1024 * 1024 * 1024;
	return 0;
}

int git_parse_signed(const char *value, intmax_t *ret, intmax_t max)
{
	if (value && *value) {
		char *end;
		intmax_t val;
		intmax_t factor;

		if (max < 0)
			BUG("max must be a positive integer");

		errno = 0;
		val = strtoimax(value, &end, 0);
		if (errno == ERANGE)
			return 0;
		if (end == value) {
			errno = EINVAL;
			return 0;
		}
		factor = get_unit_factor(end);
		if (!factor) {
			errno = EINVAL;
			return 0;
		}
		if ((val < 0 && -max / factor > val) ||
		    (val > 0 && max / factor < val)) {
			errno = ERANGE;
			return 0;
		}
		val *= factor;
		*ret = val;
		return 1;
	}
	errno = EINVAL;
	return 0;
}

static int git_parse_unsigned(const char *value, uintmax_t *ret, uintmax_t max)
{
	if (value && *value) {
		char *end;
		uintmax_t val;
		uintmax_t factor;

		/* negative values would be accepted by strtoumax */
		if (strchr(value, '-')) {
			errno = EINVAL;
			return 0;
		}
		errno = 0;
		val = strtoumax(value, &end, 0);
		if (errno == ERANGE)
			return 0;
		if (end == value) {
			errno = EINVAL;
			return 0;
		}
		factor = get_unit_factor(end);
		if (!factor) {
			errno = EINVAL;
			return 0;
		}
		if (unsigned_mult_overflows(factor, val) ||
		    factor * val > max) {
			errno = ERANGE;
			return 0;
		}
		val *= factor;
		*ret = val;
		return 1;
	}
	errno = EINVAL;
	return 0;
}

int git_parse_int(const char *value, int *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(int)))
		return 0;
	*ret = tmp;
	return 1;
}

int git_parse_int64(const char *value, int64_t *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(int64_t)))
		return 0;
	*ret = tmp;
	return 1;
}

int git_parse_ulong(const char *value, unsigned long *ret)
{
	uintmax_t tmp;
	if (!git_parse_unsigned(value, &tmp, maximum_unsigned_value_of_type(long)))
		return 0;
	*ret = tmp;
	return 1;
}

int git_parse_ssize_t(const char *value, ssize_t *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(ssize_t)))
		return 0;
	*ret = tmp;
	return 1;
}

int git_parse_double(const char *value, double *ret)
{
	char *end;
	double val;
	uintmax_t factor;

	if (!value || !*value) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;
	val = strtod(value, &end);
	if (errno == ERANGE)
		return 0;
	if (end == value) {
		errno = EINVAL;
		return 0;
	}
	factor = get_unit_factor(end);
	if (!factor) {
		errno = EINVAL;
		return 0;
	}
	val *= factor;
	*ret = val;
	return 1;
}

int git_parse_maybe_bool_text(const char *value)
{
	if (!value)
		return 1;
	if (!*value)
		return 0;
	if (!strcasecmp(value, "true")
	    || !strcasecmp(value, "yes")
	    || !strcasecmp(value, "on"))
		return 1;
	if (!strcasecmp(value, "false")
	    || !strcasecmp(value, "no")
	    || !strcasecmp(value, "off"))
		return 0;
	return -1;
}

int git_parse_maybe_bool(const char *value)
{
	int v = git_parse_maybe_bool_text(value);
	if (0 <= v)
		return v;
	if (git_parse_int(value, &v))
		return !!v;
	return -1;
}

/*
 * Parse environment variable 'k' as a boolean (in various
 * possible spellings); if missing, use the default value 'def'.
 */
int git_env_bool(const char *k, int def)
{
	const char *v = getenv(k);
	int val;
	if (!v)
		return def;
	val = git_parse_maybe_bool(v);
	if (val < 0)
		die(_("bad boolean environment value '%s' for '%s'"),
		    v, k);
	return val;
}

/*
 * Parse environment variable 'k' as ulong with possibly a unit
 * suffix; if missing, use the default value 'val'.
 */
unsigned long git_env_ulong(const char *k, unsigned long val)
{
	const char *v = getenv(k);
	if (v && !git_parse_ulong(v, &val))
		die(_("failed to parse %s"), k);
	return val;
}
