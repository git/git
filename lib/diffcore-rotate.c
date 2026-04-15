/*
 * Copyright (C) 2021, Google LLC.
 * Based on diffcore-order.c, which is Copyright (C) 2005, Junio C Hamano
 */

#include "git-compat-util.h"
#include "gettext.h"
#include "diff.h"
#include "diffcore.h"

void diffcore_rotate(struct diff_options *opt)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq = DIFF_QUEUE_INIT;
	int rotate_to, i;

	if (!q->nr)
		return;

	for (i = 0; i < q->nr; i++) {
		int cmp = strcmp(opt->rotate_to, q->queue[i]->two->path);
		if (!cmp)
			break; /* exact match */
		if (!opt->rotate_to_strict && cmp < 0)
			break; /* q->queue[i] is now past the target pathname */
	}

	if (q->nr <= i) {
		/* we did not find the specified path */
		if (opt->rotate_to_strict)
			die(_("No such path '%s' in the diff"), opt->rotate_to);
		return;
	}

	rotate_to = i;

	for (i = rotate_to; i < q->nr; i++)
		diff_q(&outq, q->queue[i]);
	for (i = 0; i < rotate_to; i++) {
		if (opt->skip_instead_of_rotate)
			diff_free_filepair(q->queue[i]);
		else
			diff_q(&outq, q->queue[i]);
	}
	free(q->queue);
	*q = outq;
}
