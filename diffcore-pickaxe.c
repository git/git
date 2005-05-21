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

void diff_pickaxe(struct diff_queue_struct *q, const char *needle)
{
	unsigned long len = strlen(needle);
	int i;
	struct diff_queue_struct outq;
	outq.queue = NULL;
	outq.nr = outq.alloc = 0;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!p->one->file_valid) {
			if (!p->two->file_valid)
				continue; /* ignore nonsense */
			/* created */
			if (contains(p->two, needle, len))
				diff_queue(&outq, p->one, p->two);
		}
		else if (!p->two->file_valid) {
			if (contains(p->one, needle, len))
				diff_queue(&outq, p->one, p->two);
		}
		else if (contains(p->one, needle, len) !=
			 contains(p->two, needle, len))
			diff_queue(&outq, p->one, p->two);
	}
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		free(p);
	}
	free(q->queue);
	*q = outq;
	return;
}
