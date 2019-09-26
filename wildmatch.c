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
static int dowild(const uchar *p, const uchar *text, unsigned int flags)
{
	uchar p_ch;
	const uchar *pattern = p;

	for ( ; (p_ch = *p) != '\0'; text++, p++) {
		int matched, match_slash, negated;
		uchar t_ch, prev_ch;
		if ((t_ch = *text) == '\0' && p_ch != '*')
			return WM_ABORT_ALL;
		if ((flags & WM_CASEFOLD) && ISUPPER(t_ch))
			t_ch = tolower(t_ch);
		if ((flags & WM_CASEFOLD) && ISUPPER(p_ch))
			p_ch = tolower(p_ch);
		switch (p_ch) {
		case '\\':
			/* Literal match with following character.  Note that the test
			 * in "default" handles the p[1] == '\0' failure case. */
			p_ch = *++p;
			/* FALLTHROUGH */
		default:
			if (t_ch != p_ch)
				return WM_NOMATCH;
			continue;
		case '?':
			/* Match anything but '/'. */
			if ((flags & WM_PATHNAME) && t_ch == '/')
				return WM_NOMATCH;
			continue;
		case '*':
			if (*++p == '*') {
				const uchar *prev_p = p - 2;
				while (*++p == '*') {}
				if (!(flags & WM_PATHNAME))
					/* without WM_PATHNAME, '*' == '**' */
					match_slash = 1;
				else if ((prev_p < pattern || *prev_p == '/') &&
				    (*p == '\0' || *p == '/' ||
				     (p[0] == '\\' && p[1] == '/'))) {
					/*
					 * Assuming we already match 'foo/' and are at
					 * <star star slash>, just assume it matches
					 * nothing and go ahead match the rest of the
					 * pattern with the remaining string. This
					 * helps make foo/<*><*>/bar (<> because
					 * otherwise it breaks C comment syntax) match
					 * both foo/bar and foo/a/bar.
					 */
					if (p[0] == '/' &&
					    dowild(p + 1, text, flags) == WM_MATCH)
						return WM_MATCH;
					match_slash = 1;
				} else /* WM_PATHNAME is set */
					match_slash = 0;
			} else
				/* without WM_PATHNAME, '*' == '**' */
				match_slash = flags & WM_PATHNAME ? 0 : 1;
			if (*p == '\0') {
				/* Trailing "**" matches everything.  Trailing "*" matches
				 * only if there are no more slash characters. */
				if (!match_slash) {
					if (strchr((char*)text, '/') != NULL)
						return WM_NOMATCH;
				}
				return WM_MATCH;
			} else if (!match_slash && *p == '/') {
				/*
				 * _one_ asterisk followed by a slash
				 * with WM_PATHNAME matches the next
				 * directory
				 */
				const char *slash = strchr((char*)text, '/');
				if (!slash)
					return WM_NOMATCH;
				text = (const uchar*)slash;
				/* the slash is consumed by the top-level for loop */
				break;
			}
			while (1) {
				if (t_ch == '\0')
					break;
				/*
				 * Try to advance faster when an asterisk is
				 * followed by a literal. We know in this case
				 * that the string before the literal
				 * must belong to "*".
				 * If match_slash is false, do not look past
				 * the first slash as it cannot belong to '*'.
				 */
				if (!is_glob_special(*p)) {
					p_ch = *p;
					if ((flags & WM_CASEFOLD) && ISUPPER(p_ch))
						p_ch = tolower(p_ch);
					while ((t_ch = *text) != '\0' &&
					       (match_slash || t_ch != '/')) {
						if ((flags & WM_CASEFOLD) && ISUPPER(t_ch))
							t_ch = tolower(t_ch);
						if (t_ch == p_ch)
							break;
						text++;
					}
					if (t_ch != p_ch)
						return WM_NOMATCH;
				}
				if ((matched = dowild(p, text, flags)) != WM_NOMATCH) {
					if (!match_slash || matched != WM_ABORT_TO_STARSTAR)
						return matched;
				} else if (!match_slash && t_ch == '/')
					return WM_ABORT_TO_STARSTAR;
				t_ch = *++text;
			}
			return WM_ABORT_ALL;
		case '[':
			p_ch = *++p;
#ifdef NEGATE_CLASS2
			if (p_ch == NEGATE_CLASS2)
				p_ch = NEGATE_CLASS;
#endif
			/* Assign literal 1/0 because of "matched" comparison. */
			negated = p_ch == NEGATE_CLASS ? 1 : 0;
			if (negated) {
				/* Inverted character class. */
				p_ch = *++p;
			}
			prev_ch = 0;
			matched = 0;
			do {
				if (!p_ch)
					return WM_ABORT_ALL;
				if (p_ch == '\\') {
					p_ch = *++p;
					if (!p_ch)
						return WM_ABORT_ALL;
					if (t_ch == p_ch)
						matched = 1;
				} else if (p_ch == '-' && prev_ch && p[1] && p[1] != ']') {
					p_ch = *++p;
					if (p_ch == '\\') {
						p_ch = *++p;
						if (!p_ch)
							return WM_ABORT_ALL;
					}
					if (t_ch <= p_ch && t_ch >= prev_ch)
						matched = 1;
					else if ((flags & WM_CASEFOLD) && ISLOWER(t_ch)) {
						uchar t_ch_upper = toupper(t_ch);
						if (t_ch_upper <= p_ch && t_ch_upper >= prev_ch)
							matched = 1;
					}
					p_ch = 0; /* This makes "prev_ch" get set to 0. */
				} else if (p_ch == '[' && p[1] == ':') {
					const uchar *s;
					int i;
					for (s = p += 2; (p_ch = *p) && p_ch != ']'; p++) {} /*SHARED ITERATOR*/
					if (!p_ch)
						return WM_ABORT_ALL;
					i = p - s - 1;
					if (i < 0 || p[-1] != ':') {
						/* Didn't find ":]", so treat like a normal set. */
						p = s - 2;
						p_ch = '[';
						if (t_ch == p_ch)
							matched = 1;
						continue;
					}
					if (CC_EQ(s,i, "alnum")) {
						if (ISALNUM(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "alpha")) {
						if (ISALPHA(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "blank")) {
						if (ISBLANK(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "cntrl")) {
						if (ISCNTRL(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "digit")) {
						if (ISDIGIT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "graph")) {
						if (ISGRAPH(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "lower")) {
						if (ISLOWER(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "print")) {
						if (ISPRINT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "punct")) {
						if (ISPUNCT(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "space")) {
						if (ISSPACE(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "upper")) {
						if (ISUPPER(t_ch))
							matched = 1;
						else if ((flags & WM_CASEFOLD) && ISLOWER(t_ch))
							matched = 1;
					} else if (CC_EQ(s,i, "xdigit")) {
						if (ISXDIGIT(t_ch))
							matched = 1;
					} else /* malformed [:class:] string */
						return WM_ABORT_ALL;
					p_ch = 0; /* This makes "prev_ch" get set to 0. */
				} else if (t_ch == p_ch)
					matched = 1;
			} while (prev_ch = p_ch, (p_ch = *++p) != ']');
			if (matched == negated ||
			    ((flags & WM_PATHNAME) && t_ch == '/'))
				return WM_NOMATCH;
			continue;
		}
	}

	return *text ? WM_NOMATCH : WM_MATCH;
}

/* Match the "pattern" against the "text" string. */
int wildmatch(const char *pattern, const char *text, unsigned int flags)
{
	return dowild((const uchar*)pattern, (const uchar*)text, flags);
}
