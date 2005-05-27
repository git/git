/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"

static int contains(struct diff_filespec *one,
		    const char *needle, unsigned long len)
{
	unsigned long offset, sz;
	const char *data;
	if (diff_populate_filespec(one))
		return 0;
	sz = one->size;
	data = one->data;
	for (offset = 0; offset + len <= sz; offset++)
		     if (!strncmp(needle, data + offset, len))
			     return 1;
	return 0;
}

void diffcore_pickaxe(const char *needle)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	unsigned long len = strlen(needle);
	int i;
	struct diff_queue_struct outq;
	outq.queue = NULL;
	outq.nr = outq.alloc = 0;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		int onum = outq.nr;
		if (!DIFF_FILE_VALID(p->one)) {
			if (!DIFF_FILE_VALID(p->two))
				continue; /* ignore nonsense */
			/* created */
			if (contains(p->two, needle, len))
				diff_q(&outq, p);
		}
		else if (!DIFF_FILE_VALID(p->two)) {
			if (contains(p->one, needle, len))
				diff_q(&outq, p);
		}
		else if (!diff_unmodified_pair(p) &&
			 contains(p->one, needle, len) !=
			 contains(p->two, needle, len))
			diff_q(&outq, p);
		if (onum == outq.nr)
			diff_free_filepair(p);
	}
	free(q->queue);
	*q = outq;
	return;
}
