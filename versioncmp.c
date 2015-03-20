#include "cache.h"
#include "string-list.h"

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

/*
 * p1 and p2 point to the first different character in two strings. If
 * either p1 or p2 starts with a prerelease suffix, it will be forced
 * to be on top.
 *
 * If both p1 and p2 start with (different) suffix, the order is
 * determined by config file.
 *
 * Note that we don't have to deal with the situation when both p1 and
 * p2 start with the same suffix because the common part is already
 * consumed by the caller.
 *
 * Return non-zero if *diff contains the return value for versioncmp()
 */
static int swap_prereleases(const void *p1_,
			    const void *p2_,
			    int *diff)
{
	const char *p1 = p1_;
	const char *p2 = p2_;
	int i, i1 = -1, i2 = -1;

	for (i = 0; i < prereleases->nr; i++) {
		const char *suffix = prereleases->items[i].string;
		if (i1 == -1 && starts_with(p1, suffix))
			i1 = i;
		if (i2 == -1 && starts_with(p2, suffix))
			i2 = i;
	}
	if (i1 == -1 && i2 == -1)
		return 0;
	if (i1 >= 0 && i2 >= 0)
		*diff = i1 - i2;
	else if (i1 >= 0)
		*diff = -1;
	else /* if (i2 >= 0) */
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
		initialized = 1;
		prereleases = git_config_get_value_multi("versionsort.prereleasesuffix");
	}
	if (prereleases && swap_prereleases(p1 - 1, p2 - 1, &diff))
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
