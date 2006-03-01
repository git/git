#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"
#include "count-delta.h"

static int diffcore_count_changes_1(void *src, unsigned long src_size,
				    void *dst, unsigned long dst_size,
				    unsigned long delta_limit,
				    unsigned long *src_copied,
				    unsigned long *literal_added)
{
	void *delta;
	unsigned long delta_size;

	delta = diff_delta(src, src_size,
			   dst, dst_size,
			   &delta_size, delta_limit);
	if (!delta)
		/* If delta_limit is exceeded, we have too much differences */
		return -1;

	/* Estimate the edit size by interpreting delta. */
	if (count_delta(delta, delta_size, src_copied, literal_added)) {
		free(delta);
		return -1;
	}
	free(delta);
	return 0;
}

int diffcore_count_changes(void *src, unsigned long src_size,
			   void *dst, unsigned long dst_size,
			   unsigned long delta_limit,
			   unsigned long *src_copied,
			   unsigned long *literal_added)
{
	return diffcore_count_changes_1(src, src_size,
					dst, dst_size,
					delta_limit,
					src_copied,
					literal_added);
}
