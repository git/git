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

bool git_parse_signed(const char *value, intmax_t *ret, intmax_t max)
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
			return false;
		if (end == value) {
			errno = EINVAL;
			return false;
		}
		factor = get_unit_factor(end);
		if (!factor) {
			errno = EINVAL;
			return false;
		}
		if ((val < 0 && (-max - 1) / factor > val) ||
		    (val > 0 && max / factor < val)) {
			errno = ERANGE;
			return false;
		}
		val *= factor;
		*ret = val;
		return true;
	}
	errno = EINVAL;
	return false;
}

bool git_parse_unsigned(const char *value, uintmax_t *ret, uintmax_t max)
{
	if (value && *value) {
		char *end;
		uintmax_t val;
		uintmax_t factor;

		/* negative values would be accepted by strtoumax */
		if (strchr(value, '-')) {
			errno = EINVAL;
			return false;
		}
		errno = 0;
		val = strtoumax(value, &end, 0);
		if (errno == ERANGE)
			return false;
		if (end == value) {
			errno = EINVAL;
			return false;
		}
		factor = get_unit_factor(end);
		if (!factor) {
			errno = EINVAL;
			return false;
		}
		if (unsigned_mult_overflows(factor, val) ||
		    factor * val > max) {
			errno = ERANGE;
			return false;
		}
		val *= factor;
		*ret = val;
		return true;
	}
	errno = EINVAL;
	return false;
}

bool git_parse_int(const char *value, int *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(int)))
		return false;
	*ret = tmp;
	return true;
}

bool git_parse_int64(const char *value, int64_t *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(int64_t)))
		return false;
	*ret = tmp;
	return true;
}

bool git_parse_ulong(const char *value, unsigned long *ret)
{
	uintmax_t tmp;
	if (!git_parse_unsigned(value, &tmp, maximum_unsigned_value_of_type(long)))
		return false;
	*ret = tmp;
	return true;
}

bool git_parse_ssize_t(const char *value, ssize_t *ret)
{
	intmax_t tmp;
	if (!git_parse_signed(value, &tmp, maximum_signed_value_of_type(ssize_t)))
		return false;
	*ret = tmp;
	return true;
}

bool git_parse_double(const char *value, double *ret)
{
	char *end;
	double val;
	uintmax_t factor;

	if (!value || !*value) {
		errno = EINVAL;
		return false;
	}

	errno = 0;
	val = strtod(value, &end);
	if (errno == ERANGE)
		return false;
	if (end == value) {
		errno = EINVAL;
		return false;
	}
	factor = get_unit_factor(end);
	if (!factor) {
		errno = EINVAL;
		return false;
	}
	val *= factor;
	*ret = val;
	return true;
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

/*
 * Helper that handles both signed/unsigned cases. If "negate" is NULL,
 * negative values are disallowed. If not NULL and the input is negative,
 * the value is range-checked but the caller is responsible for actually doing
 * the negatiion. You probably don't want to use this! Use one of
 * parse_signed_from_buf() or parse_unsigned_from_buf() below.
 */
static bool parse_from_buf_internal(const char *buf, size_t len,
				    const char **ep, bool *negate,
				    uintmax_t *ret, uintmax_t max)
{
	const char *end = buf + len;
	uintmax_t val = 0;

	while (buf < end && isspace(*buf))
		buf++;

	if (negate)
		*negate = false;
	if (buf < end && *buf == '-') {
		if (!negate) {
			errno = EINVAL;
			return false;
		}
		buf++;
		*negate = true;
		/* Assume negative range is always one larger than positive. */
		max = max + 1;
	} else if (buf < end && *buf == '+') {
		buf++;
	}

	if (buf == end || !isdigit(*buf)) {
		errno = EINVAL;
		return false;
	}

	while (buf < end && isdigit(*buf)) {
		int digit = *buf - '0';

		if (val > max / 10) {
			errno = ERANGE;
			return false;
		}
		val *= 10;
		if (val > max - digit) {
			errno = ERANGE;
			return false;
		}
		val += digit;

		buf++;
	}

	*ep = buf;
	*ret = val;
	return true;
}

bool parse_unsigned_from_buf(const char *buf, size_t len, const char **ep,
			     uintmax_t *ret, uintmax_t max)
{
	return parse_from_buf_internal(buf, len, ep, NULL, ret, max);
}

bool parse_signed_from_buf(const char *buf, size_t len, const char **ep,
			   intmax_t *ret, intmax_t max)
{
	uintmax_t u_ret;
	bool negate;

	if (!parse_from_buf_internal(buf, len, ep, &negate, &u_ret, max))
		return false;
	/*
	 * Range already checked internally, but we must apply negation
	 * ourselves since only we have the signed integer type.
	 */
	if (negate) {
		*ret = u_ret;
		*ret = -*ret;
	} else {
		*ret = u_ret;
	}
	return true;
}

bool parse_int_from_buf(const char *buf, size_t len, const char **ep, int *ret)
{
	intmax_t tmp;
	if (!parse_signed_from_buf(buf, len, ep, &tmp,
				   maximum_signed_value_of_type(int)))
		return false;
	*ret = tmp;
	return true;
}
