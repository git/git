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

#include "cache.h"
#include "wildmatch.h"

typedef unsigned char uchar;

/* What character marks an inverted character class? */
#define NEGATE_CLASS	'!'
#define NEGATE_CLASS2	'^'

#define FALSE 0
#define TRUE 1

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

/* Match pattern "p" against "text" */
static int dowild(const uchar *p, const uchar *text, int force_lower_case)
{
	uchar p_ch;

	for ( ; (p_ch = *p) != '\0'; text++, p++) {
		int matched, special;
		uchar t_ch, prev_ch;
		if ((t_ch = *text) == '\0' && p_ch != '*')
			return ABORT_ALL;
		if (force_lower_case && ISUPPER(t_ch))
			t_ch = tolower(t_ch);
		if (force_lower_case && ISUPPER(p_ch))
			p_ch = tolower(p_ch);
		switch (p_ch) {
		case '\\':
			/* Literal match with following character.  Note that the test
			 * in "default" handles the p[1] == '\0' failure case. */
			p_ch = *++p;
			/* FALLTHROUGH */
		default:
			if (t_ch != p_ch)
				return NOMATCH;
			continue;
		case '?':
			/* Match anything but '/'. */
			if (t_ch == '/')
				return NOMATCH;
			continue;
		case '*':
			if (*++p == '*') {
				const uchar *prev_p = p - 2;
				while (*++p == '*') {}
				if ((prev_p == text || *prev_p == '/') ||
				    (*p == '\0' || *p == '/' ||
				     (p[0] == '\\' && p[1] == '/'))) {
					special = TRUE;
				} else
					return ABORT_MALFORMED;
			} else
				special = FALSE;
			if (*p == '\0') {
				/* Trailing "**" matches everything.  Trailing "*" matches
				 * only if there are no more slash characters. */
				if (!special) {
					if (strchr((char*)text, '/') != NULL)
						return NOMATCH;
				}
				return MATCH;
			}
			while (1) {
				if (t_ch == '\0')
					break;
				if ((matched = dowild(p, text,  force_lower_case)) != NOMATCH) {
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
				return NOMATCH;
			continue;
		}
	}

	return *text ? NOMATCH : MATCH;
}

/* Match the "pattern" against the "text" string. */
int wildmatch(const char *pattern, const char *text, int flags)
{
	return dowild((const uchar*)pattern, (const uchar*)text,
		      flags & FNM_CASEFOLD ? 1 :0);
}
