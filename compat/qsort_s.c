#include "../git-compat-util.h"

/*
 * A merge sort implementation, simplified from the qsort implementation
 * by Mike Haertel, which is a part of the GNU C Library.
 * Added context pointer, safety checks and return value.
 */

static void msort_with_tmp(void *b, size_t n, size_t s,
			   int (*cmp)(const void *, const void *, void *),
			   char *t, void *ctx)
{
	char *tmp;
	char *b1, *b2;
	size_t n1, n2;

	if (n <= 1)
		return;

	n1 = n / 2;
	n2 = n - n1;
	b1 = b;
	b2 = (char *)b + (n1 * s);

	msort_with_tmp(b1, n1, s, cmp, t, ctx);
	msort_with_tmp(b2, n2, s, cmp, t, ctx);

	tmp = t;

	while (n1 > 0 && n2 > 0) {
		if (cmp(b1, b2, ctx) <= 0) {
			memcpy(tmp, b1, s);
			tmp += s;
			b1 += s;
			--n1;
		} else {
			memcpy(tmp, b2, s);
			tmp += s;
			b2 += s;
			--n2;
		}
	}
	if (n1 > 0)
		memcpy(tmp, b1, n1 * s);
	memcpy(b, t, (n - n2) * s);
}

int git_qsort_s(void *b, size_t n, size_t s,
		int (*cmp)(const void *, const void *, void *), void *ctx)
{
	const size_t size = st_mult(n, s);
	char buf[1024];

	if (!n)
		return 0;
	if (!b || !cmp)
		return -1;

	if (size < sizeof(buf)) {
		/* The temporary array fits on the small on-stack buffer. */
		msort_with_tmp(b, n, s, cmp, buf, ctx);
	} else {
		/* It's somewhat large, so malloc it.  */
		char *tmp = xmalloc(size);
		msort_with_tmp(b, n, s, cmp, tmp, ctx);
		free(tmp);
	}
	return 0;
}
