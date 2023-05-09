#include "git-compat-util.h"
#include "config.h"
#include "string-list.h"
#include "versioncmp.h"

/*
 * versioncmp(): copied from string/strverscmp.c in glibc commit
 * ee9247c38a8def24a59eb5cfb7196a98bef8cfdc, reformatted to Git coding
 * style. The implementation is under LGPL-2.1 and Git relicenses it
 * to GPLv2.
 */

/*
 * states: S_N: normal, S_I: comparing integral part, S_F: comparing
 * fractionnal parts, S_Z: idem but with leading Zeroes only
 */
#define  S_N    0x0
#define  S_I    0x3
#define  S_F    0x6
#define  S_Z    0x9

/* result_type: CMP: return diff; LEN: compare using len_diff/diff */
#define  CMP    2
#define  LEN    3

static const struct string_list *prereleases;
static int initialized;

struct suffix_match {
	int conf_pos;
	int start;
	int len;
};

static void find_better_matching_suffix(const char *tagname, const char *suffix,
					int suffix_len, int start, int conf_pos,
					struct suffix_match *match)
{
	/*
	 * A better match either starts earlier or starts at the same offset
	 * but is longer.
	 */
	int end = match->len < suffix_len ? match->start : match->start-1;
	int i;
	for (i = start; i <= end; i++)
		if (starts_with(tagname + i, suffix)) {
			match->conf_pos = conf_pos;
			match->start = i;
			match->len = suffix_len;
			break;
		}
}

/*
 * off is the offset of the first different character in the two strings
 * s1 and s2. If either s1 or s2 contains a prerelease suffix containing
 * that offset or a suffix ends right before that offset, then that
 * string will be forced to be on top.
 *
 * If both s1 and s2 contain a (different) suffix around that position,
 * their order is determined by the order of those two suffixes in the
 * configuration.
 * If any of the strings contains more than one different suffixes around
 * that position, then that string is sorted according to the contained
 * suffix which starts at the earliest offset in that string.
 * If more than one different contained suffixes start at that earliest
 * offset, then that string is sorted according to the longest of those
 * suffixes.
 *
 * Return non-zero if *diff contains the return value for versioncmp()
 */
static int swap_prereleases(const char *s1,
			    const char *s2,
			    int off,
			    int *diff)
{
	int i;
	struct suffix_match match1 = { -1, off, -1 };
	struct suffix_match match2 = { -1, off, -1 };

	for (i = 0; i < prereleases->nr; i++) {
		const char *suffix = prereleases->items[i].string;
		int start, suffix_len = strlen(suffix);
		if (suffix_len < off)
			start = off - suffix_len;
		else
			start = 0;
		find_better_matching_suffix(s1, suffix, suffix_len, start,
					    i, &match1);
		find_better_matching_suffix(s2, suffix, suffix_len, start,
					    i, &match2);
	}
	if (match1.conf_pos == -1 && match2.conf_pos == -1)
		return 0;
	if (match1.conf_pos == match2.conf_pos)
		/* Found the same suffix in both, e.g. "-rc" in "v1.0-rcX"
		 * and "v1.0-rcY": the caller should decide based on "X"
		 * and "Y". */
		return 0;

	if (match1.conf_pos >= 0 && match2.conf_pos >= 0)
		*diff = match1.conf_pos - match2.conf_pos;
	else if (match1.conf_pos >= 0)
		*diff = -1;
	else /* if (match2.conf_pos >= 0) */
		*diff = 1;
	return 1;
}

/*
 * Compare S1 and S2 as strings holding indices/version numbers,
 * returning less than, equal to or greater than zero if S1 is less
 * than, equal to or greater than S2 (for more info, see the texinfo
 * doc).
 */

int versioncmp(const char *s1, const char *s2)
{
	const unsigned char *p1 = (const unsigned char *) s1;
	const unsigned char *p2 = (const unsigned char *) s2;
	unsigned char c1, c2;
	int state, diff;

	/*
	 * Symbol(s)    0       [1-9]   others
	 * Transition   (10) 0  (01) d  (00) x
	 */
	static const uint8_t next_state[] = {
		/* state    x    d    0  */
		/* S_N */  S_N, S_I, S_Z,
		/* S_I */  S_N, S_I, S_I,
		/* S_F */  S_N, S_F, S_F,
		/* S_Z */  S_N, S_F, S_Z
	};

	static const int8_t result_type[] = {
		/* state   x/x  x/d  x/0  d/x  d/d  d/0  0/x  0/d  0/0  */

		/* S_N */  CMP, CMP, CMP, CMP, LEN, CMP, CMP, CMP, CMP,
		/* S_I */  CMP, -1,  -1,  +1,  LEN, LEN, +1,  LEN, LEN,
		/* S_F */  CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP, CMP,
		/* S_Z */  CMP, +1,  +1,  -1,  CMP, CMP, -1,  CMP, CMP
	};

	if (p1 == p2)
		return 0;

	c1 = *p1++;
	c2 = *p2++;
	/* Hint: '0' is a digit too.  */
	state = S_N + ((c1 == '0') + (isdigit (c1) != 0));

	while ((diff = c1 - c2) == 0) {
		if (c1 == '\0')
			return diff;

		state = next_state[state];
		c1 = *p1++;
		c2 = *p2++;
		state += (c1 == '0') + (isdigit (c1) != 0);
	}

	if (!initialized) {
		const char *const newk = "versionsort.suffix";
		const char *const oldk = "versionsort.prereleasesuffix";
		const struct string_list *newl;
		const struct string_list *oldl;
		int new = git_config_get_string_multi(newk, &newl);
		int old = git_config_get_string_multi(oldk, &oldl);

		if (!new && !old)
			warning("ignoring %s because %s is set", oldk, newk);
		if (!new)
			prereleases = newl;
		else if (!old)
			prereleases = oldl;

		initialized = 1;
	}
	if (prereleases && swap_prereleases(s1, s2, (const char *) p1 - s1 - 1,
					    &diff))
		return diff;

	state = result_type[state * 3 + (((c2 == '0') + (isdigit (c2) != 0)))];

	switch (state) {
	case CMP:
		return diff;

	case LEN:
		while (isdigit (*p1++))
			if (!isdigit (*p2++))
				return 1;

		return isdigit (*p2) ? -1 : diff;

	default:
		return state;
	}
}
