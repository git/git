#ifndef NUMPARSE_H
#define NUMPARSE_H

/*
 * Functions for parsing integral numbers.
 *
 * strtol() and strtoul() are very flexible, in fact too flexible for
 * many purposes. These functions wrap them to make them easier to use
 * in a stricter way.
 *
 * There are two classes of function, parse_*() and convert_*(). The
 * former try to read a number from the front of a string and report a
 * pointer to the character following the number. The latter don't
 * report the end of the number, and are meant to be used when the
 * input string should contain only a single number, with no trailing
 * characters.
 *
 * Each class of functions has four variants:
 *
 * - parse_l(), convert_l() -- parse long ints
 * - parse_ul(), convert_ul() -- parse unsigned long ints
 * - parse_i(), convert_i() -- parse ints
 * - parse_ui(), convert_ui() -- parse unsigned ints
 *
 * The style of parsing is controlled by a flags argument which
 * encodes both the base of the number and many other options. The
 * base is encoded by its numerical value (2 <= base <= 36), or zero
 * if it should be determined automatically based on whether the
 * number has a "0x" or "0" prefix.
 *
 * The functions all return zero on success. On error, they return a
 * negative integer indicating the first error that was detected. For
 * example, if no sign characters were allowed but the string
 * contained a '-', the function will return -NUM_MINUS. If there is
 * any kind of error, *result and *endptr are unchanged.
 *
 * Examples:
 *
 * - Convert hexadecimal string s into an unsigned int. Die if there
 *   are any characters in s besides hexadecimal digits, or if the
 *   result exceeds the range of an unsigned int:
 *
 *     if (convert_ui(s, 16, &result))
 *             die("...");
 *
 * - Read a base-ten long number from the front of a string, allowing
 *   sign characters and setting endptr to point at any trailing
 *   characters:
 *
 *     if (parse_l(s, 10 | NUM_SIGN | NUM_TRAILING, &result, &endptr))
 *             die("...");
 *
 * - Convert decimal string s into a signed int, but not allowing the
 *   string to contain a '+' or '-' prefix (and thereby indirectly
 *   ensuring that the result will be non-negative):
 *
 *     if (convert_i(s, 10, &result))
 *             die("...");
 *
 * - Convert s into a signed int, interpreting prefix "0x" to mean
 *   hexadecimal and "0" to mean octal. If the value doesn't fit in an
 *   unsigned int, set result to INT_MIN or INT_MAX.
 *
 *     if (convert_i(s, NUM_SLOPPY, &result))
 *             die("...");
 */


/*
 * Constants for parsing numbers.
 *
 * These can be passed in flags to allow the specified features. Also,
 * if there is an error parsing a number, the parsing functions return
 * the negated value of one of these constants (or NUM_NO_DIGITS or
 * NUM_OTHER_ERROR) to indicate the first error detected.
 */

/*
 * The lowest 6 bits of flags hold the numerical base that should be
 * used to parse the number, 2 <= base <= 36. If base is set to 0,
 * then NUM_BASE_SPECIFIER must be set too; in this case, the base is
 * detected automatically from the string's prefix.
 */
#define NUM_BASE_MASK 0x3f

/* Skip any whitespace before the number. */
#define NUM_LEADING_WHITESPACE (1 << 8)

/* Allow a leading '+'. */
#define NUM_PLUS               (1 << 9)

/* Allow a leading '-'. */
#define NUM_MINUS              (1 << 10)

/*
 * Allow a leading base specifier:
 * - If base is 0: a leading "0x" indicates base 16; a leading "0"
 *   indicates base 8; otherwise, assume base 10.
 * - If base is 16: a leading "0x" is allowed and skipped over.
 */
#define NUM_BASE_SPECIFIER     (1 << 11)

/*
 * If the number is not in the allowed range, return the smallest or
 * largest representable value instead.
 */
#define NUM_SATURATE           (1 << 12)

/*
 * Just parse until the end of the number, ignoring any subsequent
 * characters. If this option is not specified, then it is an error if
 * the whole string cannot be parsed.
 */
#define NUM_TRAILING           (1 << 13)


/* Additional errors that can come from parsing numbers: */

/* There were no valid digits */
#define NUM_NO_DIGITS          (1 << 14)
/* There was some other error reported by strtol()/strtoul(): */
#define NUM_OTHER_ERROR        (1 << 15)

/*
 * Please note that there is also a NUM_NEGATIVE, which is used
 * internally.
 */

/*
 * Now define some useful combinations of parsing options:
 */

/* A bunch of digits with an optional sign. */
#define NUM_SIGN (NUM_PLUS | NUM_MINUS)

/*
 * Be as liberal as possible with the form of the number itself
 * (though if you also want to allow leading whitespace and/or
 * trailing characters, you should combine this with
 * NUM_LEADING_WHITESPACE and/or NUM_TRAILING).
 */
#define NUM_SLOPPY (NUM_SIGN | NUM_SATURATE | NUM_BASE_SPECIFIER)


/*
 * Number parsing functions:
 *
 * The following functions parse a number (long, unsigned long, int,
 * or unsigned int respectively) from the front of s, storing the
 * value to *result and storing a pointer to the first character after
 * the number to *endptr. flags specifies how the number should be
 * parsed, including which base should be used. flags is a combination
 * of the numerical base (2-36) and the NUM_* constants above (see).
 * Return 0 on success or a negative value if there was an error. On
 * failure, *result and *entptr are left unchanged.
 *
 * Please note that if NUM_TRAILING is not set, then it is
 * nevertheless an error if there are any characters between the end
 * of the number and the end of the string.
 */

int parse_l(const char *s, unsigned int flags,
	    long *result, char **endptr);

int parse_ul(const char *s, unsigned int flags,
	     unsigned long *result, char **endptr);

int parse_i(const char *s, unsigned int flags,
	    int *result, char **endptr);

int parse_ui(const char *s, unsigned int flags,
	     unsigned int *result, char **endptr);


/*
 * Number conversion functions:
 *
 * The following functions parse a string into a number. They are
 * identical to the parse_*() functions above, except that the endptr
 * is not returned. These are most useful when parsing a whole string
 * into a number; i.e., when (flags & NUM_TRAILING) is unset.
 */
static inline int convert_l(const char *s, unsigned int flags,
			    long *result)
{
	return parse_l(s, flags, result, NULL);
}

static inline int convert_ul(const char *s, unsigned int flags,
			     unsigned long *result)
{
	return parse_ul(s, flags, result, NULL);
}

static inline int convert_i(const char *s, unsigned int flags,
			    int *result)
{
	return parse_i(s, flags, result, NULL);
}

static inline int convert_ui(const char *s, unsigned int flags,
			     unsigned int *result)
{
	return parse_ui(s, flags, result, NULL);
}

#endif /* NUMPARSE_H */
