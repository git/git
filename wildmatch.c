/*
**  Do shell-style pattern matching for ?, \, [], and * characters.
**  It is 8bit clean.
**
**  Written by Rich $alz, mirror!rs, Wed Nov 26 19:03:17 EST 1986.
**  Rich $alz is now <rsalz@bbn.com>.
**
**  Modified by Wayne Davison to special-case '/' matching, to make '**'
**  work differently than '*', and to fix the character-class code.
*/

#include "rsync.h"

/* What character marks an inverted character class? */
#define NEGATE_CLASS	'!'
#define NEGATE_CLASS2	'^'

#define FALSE 0
#define TRUE 1
#define ABORT_ALL -1
#define ABORT_TO_STARSTAR -2

#define CC_EQ(class, len, litmatch) ((len) == sizeof (litmatch)-1 \
				    && *(class) == *(litmatch) \
				    && strncmp((char*)class, litmatch, len) == 0)

#if defined STDC_HEADERS || !defined isascii
# define ISASCII(c) 1
#else
# define ISASCII(c) isascii(c)
#endif

#ifdef isblank
# define ISBLANK(c) (ISASCII(c) && isblank(c))
#else
# define ISBLANK(c) ((c) == ' ' || (c) == '\t')
#endif

#ifdef isgraph
# define ISGRAPH(c) (ISASCII(c) && isgraph(c))
#else
# define ISGRAPH(c) (ISASCII(c) && isprint(c) && !isspace(c))
#endif

#define ISPRINT(c) (ISASCII(c) && isprint(c))
#define ISDIGIT(c) (ISASCII(c) && isdigit(c))
#define ISALNUM(c) (ISASCII(c) && isalnum(c))
#define ISALPHA(c) (ISASCII(c) && isalpha(c))
#define ISCNTRL(c) (ISASCII(c) && iscntrl(c))
#define ISLOWER(c) (ISASCII(c) && islower(c))
#define ISPUNCT(c) (ISASCII(c) && ispunct(c))
#define ISSPACE(c) (ISASCII(c) && isspace(c))
#define ISUPPER(c) (ISASCII(c) && isupper(c))
#define ISXDIGIT(c) (ISASCII(c) && isxdigit(c))

#ifdef WILD_TEST_ITERATIONS
int wildmatch_iteration_count;
#endif

static int force_lower_case = 0;

/* Match pattern "p" against the a virtually-joined string consisting
 * of "text" and any strings in array "a". */
static int dowild(const uchar *p, const uchar *text, const uchar*const *a)
{
    uchar p_ch;

#ifdef WILD_TEST_ITERATIONS
    wildmatch_iteration_count++;
#endif

    for ( ; (p_ch = *p) != '\0'; text++, p++) {
	int matched, special;
	uchar t_ch, prev_ch;
	while ((t_ch = *text) == '\0') {
	    if (*a == NULL) {
		if (p_ch != '*')
		    return ABORT_ALL;
		break;
	    }
	    text = *a++;
	}
	if (force_lower_case && ISUPPER(t_ch))
	    t_ch = tolower(t_ch);
	switch (p_ch) {
	  case '\\':
	    /* Literal match with following character.  Note that the test
	     * in "default" handles the p[1] == '\0' failure case. */
	    p_ch = *++p;
	    /* FALLTHROUGH */
	  default:
	    if (t_ch != p_ch)
		return FALSE;
	    continue;
	  case '?':
	    /* Match anything but '/'. */
	    if (t_ch == '/')
		return FALSE;
	    continue;
	  case '*':
	    if (*++p == '*') {
		while (*++p == '*') {}
		special = TRUE;
	    } else
		special = FALSE;
	    if (*p == '\0') {
		/* Trailing "**" matches everything.  Trailing "*" matches
		 * only if there are no more slash characters. */
		if (!special) {
		    do {
			if (strchr((char*)text, '/') != NULL)
			    return FALSE;
		    } while ((text = *a++) != NULL);
		}
		return TRUE;
	    }
	    while (1) {
		if (t_ch == '\0') {
		    if ((text = *a++) == NULL)
			break;
		    t_ch = *text;
		    continue;
		}
		if ((matched = dowild(p, text, a)) != FALSE) {
		    if (!special || matched != ABORT_TO_STARSTAR)
			return matched;
		} else if (!special && t_ch == '/')
		    return ABORT_TO_STARSTAR;
		t_ch = *++text;
	    }
	    return ABORT_ALL;
	  case '[':
	    p_ch = *++p;
#ifdef NEGATE_CLASS2
	    if (p_ch == NEGATE_CLASS2)
		p_ch = NEGATE_CLASS;
#endif
	    /* Assign literal TRUE/FALSE because of "matched" comparison. */
	    special = p_ch == NEGATE_CLASS? TRUE : FALSE;
	    if (special) {
		/* Inverted character class. */
		p_ch = *++p;
	    }
	    prev_ch = 0;
	    matched = FALSE;
	    do {
		if (!p_ch)
		    return ABORT_ALL;
		if (p_ch == '\\') {
		    p_ch = *++p;
		    if (!p_ch)
			return ABORT_ALL;
		    if (t_ch == p_ch)
			matched = TRUE;
		} else if (p_ch == '-' && prev_ch && p[1] && p[1] != ']') {
		    p_ch = *++p;
		    if (p_ch == '\\') {
			p_ch = *++p;
			if (!p_ch)
			    return ABORT_ALL;
		    }
		    if (t_ch <= p_ch && t_ch >= prev_ch)
			matched = TRUE;
		    p_ch = 0; /* This makes "prev_ch" get set to 0. */
		} else if (p_ch == '[' && p[1] == ':') {
		    const uchar *s;
		    int i;
		    for (s = p += 2; (p_ch = *p) && p_ch != ']'; p++) {} /*SHARED ITERATOR*/
		    if (!p_ch)
			return ABORT_ALL;
		    i = p - s - 1;
		    if (i < 0 || p[-1] != ':') {
			/* Didn't find ":]", so treat like a normal set. */
			p = s - 2;
			p_ch = '[';
			if (t_ch == p_ch)
			    matched = TRUE;
			continue;
		    }
		    if (CC_EQ(s,i, "alnum")) {
			if (ISALNUM(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "alpha")) {
			if (ISALPHA(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "blank")) {
			if (ISBLANK(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "cntrl")) {
			if (ISCNTRL(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "digit")) {
			if (ISDIGIT(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "graph")) {
			if (ISGRAPH(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "lower")) {
			if (ISLOWER(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "print")) {
			if (ISPRINT(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "punct")) {
			if (ISPUNCT(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "space")) {
			if (ISSPACE(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "upper")) {
			if (ISUPPER(t_ch))
			    matched = TRUE;
		    } else if (CC_EQ(s,i, "xdigit")) {
			if (ISXDIGIT(t_ch))
			    matched = TRUE;
		    } else /* malformed [:class:] string */
			return ABORT_ALL;
		    p_ch = 0; /* This makes "prev_ch" get set to 0. */
		} else if (t_ch == p_ch)
		    matched = TRUE;
	    } while (prev_ch = p_ch, (p_ch = *++p) != ']');
	    if (matched == special || t_ch == '/')
		return FALSE;
	    continue;
	}
    }

    do {
	if (*text)
	    return FALSE;
    } while ((text = *a++) != NULL);

    return TRUE;
}

/* Match literal string "s" against the a virtually-joined string consisting
 * of "text" and any strings in array "a". */
static int doliteral(const uchar *s, const uchar *text, const uchar*const *a)
{
    for ( ; *s != '\0'; text++, s++) {
	while (*text == '\0') {
	    if ((text = *a++) == NULL)
		return FALSE;
	}
	if (*text != *s)
	    return FALSE;
    }

    do {
	if (*text)
	    return FALSE;
    } while ((text = *a++) != NULL);

    return TRUE;
}

/* Return the last "count" path elements from the concatenated string.
 * We return a string pointer to the start of the string, and update the
 * array pointer-pointer to point to any remaining string elements. */
static const uchar *trailing_N_elements(const uchar*const **a_ptr, int count)
{
    const uchar*const *a = *a_ptr;
    const uchar*const *first_a = a;

    while (*a)
	    a++;

    while (a != first_a) {
	const uchar *s = *--a;
	s += strlen((char*)s);
	while (--s >= *a) {
	    if (*s == '/' && !--count) {
		*a_ptr = a+1;
		return s+1;
	    }
	}
    }

    if (count == 1) {
	*a_ptr = a+1;
	return *a;
    }

    return NULL;
}

/* Match the "pattern" against the "text" string. */
int wildmatch(const char *pattern, const char *text)
{
    static const uchar *nomore[1]; /* A NULL pointer. */
#ifdef WILD_TEST_ITERATIONS
    wildmatch_iteration_count = 0;
#endif
    return dowild((const uchar*)pattern, (const uchar*)text, nomore) == TRUE;
}

/* Match the "pattern" against the forced-to-lower-case "text" string. */
int iwildmatch(const char *pattern, const char *text)
{
    static const uchar *nomore[1]; /* A NULL pointer. */
    int ret;
#ifdef WILD_TEST_ITERATIONS
    wildmatch_iteration_count = 0;
#endif
    force_lower_case = 1;
    ret = dowild((const uchar*)pattern, (const uchar*)text, nomore) == TRUE;
    force_lower_case = 0;
    return ret;
}

/* Match pattern "p" against the a virtually-joined string consisting
 * of all the pointers in array "texts" (which has a NULL pointer at the
 * end).  The int "where" can be 0 (normal matching), > 0 (match only
 * the trailing N slash-separated filename components of "texts"), or < 0
 * (match the "pattern" at the start or after any slash in "texts"). */
int wildmatch_array(const char *pattern, const char*const *texts, int where)
{
    const uchar *p = (const uchar*)pattern;
    const uchar*const *a = (const uchar*const*)texts;
    const uchar *text;
    int matched;

#ifdef WILD_TEST_ITERATIONS
    wildmatch_iteration_count = 0;
#endif

    if (where > 0)
	text = trailing_N_elements(&a, where);
    else
	text = *a++;
    if (!text)
	return FALSE;

    if ((matched = dowild(p, text, a)) != TRUE && where < 0
     && matched != ABORT_ALL) {
	while (1) {
	    if (*text == '\0') {
		if ((text = (uchar*)*a++) == NULL)
		    return FALSE;
		continue;
	    }
	    if (*text++ == '/' && (matched = dowild(p, text, a)) != FALSE
	     && matched != ABORT_TO_STARSTAR)
		break;
	}
    }
    return matched == TRUE;
}

/* Match literal string "s" against the a virtually-joined string consisting
 * of all the pointers in array "texts" (which has a NULL pointer at the
 * end).  The int "where" can be 0 (normal matching), or > 0 (match
 * only the trailing N slash-separated filename components of "texts"). */
int litmatch_array(const char *string, const char*const *texts, int where)
{
    const uchar *s = (const uchar*)string;
    const uchar*const *a = (const uchar* const*)texts;
    const uchar *text;

    if (where > 0)
	text = trailing_N_elements(&a, where);
    else
	text = *a++;
    if (!text)
	return FALSE;

    return doliteral(s, text, a) == TRUE;
}
