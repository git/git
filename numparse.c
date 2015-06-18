#include "git-compat-util.h"
#include "numparse.h"

#define NUM_NEGATIVE (1 << 16)


static int parse_precheck(const char *s, unsigned int *flags)
{
	const char *number;

	if (isspace(*s)) {
		if (!(*flags & NUM_LEADING_WHITESPACE))
			return -NUM_LEADING_WHITESPACE;
		do {
			s++;
		} while (isspace(*s));
	}

	if (*s == '+') {
		if (!(*flags & NUM_PLUS))
			return -NUM_PLUS;
		number = s + 1;
		*flags &= ~NUM_NEGATIVE;
	} else if (*s == '-') {
		if (!(*flags & NUM_MINUS))
			return -NUM_MINUS;
		number = s + 1;
		*flags |= NUM_NEGATIVE;
	} else {
		number = s;
		*flags &= ~NUM_NEGATIVE;
	}

	if (!(*flags & NUM_BASE_SPECIFIER)) {
		int base = *flags & NUM_BASE_MASK;
		if (base == 0) {
			/* This is a pointless combination of options. */
			die("BUG: base=0 specified without NUM_BASE_SPECIFIER");
		} else if (base == 16 && starts_with(number, "0x")) {
			/*
			 * We want to treat this as zero terminated by
			 * an 'x', whereas strtol()/strtoul() would
			 * silently eat the "0x". We accomplish this
			 * by treating it as a base 10 number:
			 */
			*flags = (*flags & ~NUM_BASE_MASK) | 10;
		}
	}
	return 0;
}

int parse_l(const char *s, unsigned int flags, long *result, char **endptr)
{
	long l;
	const char *end;
	int err = 0;

	err = parse_precheck(s, &flags);
	if (err)
		return err;

	/*
	 * Now let strtol() do the heavy lifting:
	 */
	errno = 0;
	l = strtol(s, (char **)&end, flags & NUM_BASE_MASK);
	if (errno) {
		if (errno == ERANGE) {
			if (!(flags & NUM_SATURATE))
				return -NUM_SATURATE;
		} else {
			return -NUM_OTHER_ERROR;
		}
	}
	if (end == s)
		return -NUM_NO_DIGITS;

	if (*end && !(flags & NUM_TRAILING))
		return -NUM_TRAILING;

	/* Everything was OK */
	*result = l;
	if (endptr)
		*endptr = (char *)end;
	return 0;
}

int parse_ul(const char *s, unsigned int flags,
	     unsigned long *result, char **endptr)
{
	unsigned long ul;
	const char *end;
	int err = 0;

	err = parse_precheck(s, &flags);
	if (err)
		return err;

	/*
	 * Now let strtoul() do the heavy lifting:
	 */
	errno = 0;
	ul = strtoul(s, (char **)&end, flags & NUM_BASE_MASK);
	if (errno) {
		if (errno == ERANGE) {
			if (!(flags & NUM_SATURATE))
				return -NUM_SATURATE;
		} else {
			return -NUM_OTHER_ERROR;
		}
	}
	if (end == s)
		return -NUM_NO_DIGITS;

	/*
	 * strtoul(), perversely, accepts negative numbers, converting
	 * them to the positive number with the same bit pattern. We
	 * don't ever want that.
	 */
	if ((flags & NUM_NEGATIVE) && ul) {
		if (!(flags & NUM_SATURATE))
			return -NUM_SATURATE;
		ul = 0;
	}

	if (*end && !(flags & NUM_TRAILING))
		return -NUM_TRAILING;

	/* Everything was OK */
	*result = ul;
	if (endptr)
		*endptr = (char *)end;
	return 0;
}

int parse_i(const char *s, unsigned int flags, int *result, char **endptr)
{
	long l;
	int err;
	char *end;

	err = parse_l(s, flags, &l, &end);
	if (err)
		return err;

	if ((int)l == l)
		*result = l;
	else if (!(flags & NUM_SATURATE))
		return -NUM_SATURATE;
	else
		*result = (l <= 0) ? INT_MIN : INT_MAX;

	if (endptr)
		*endptr = end;

	return 0;
}

int parse_ui(const char *s, unsigned int flags, unsigned int *result, char **endptr)
{
	unsigned long ul;
	int err;
	char *end;

	err = parse_ul(s, flags, &ul, &end);
	if (err)
		return err;

	if ((unsigned int)ul == ul)
		*result = ul;
	else if (!(flags & NUM_SATURATE))
		return -NUM_SATURATE;
	else
		*result = UINT_MAX;

	if (endptr)
		*endptr = end;

	return 0;
}
