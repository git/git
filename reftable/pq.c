/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "pq.h"

#include "reftable-error.h"
#include "reftable-record.h"
#include "system.h"
#include "basics.h"

int pq_less(struct pq_entry *a, struct pq_entry *b)
{
	int cmp = reftable_record_cmp(a->rec, b->rec);
	if (cmp == 0)
		return a->index > b->index;
	return cmp < 0;
}

struct pq_entry merged_iter_pqueue_remove(struct merged_iter_pqueue *pq)
{
	size_t i = 0;
	struct pq_entry e = pq->heap[0];
	pq->heap[0] = pq->heap[pq->len - 1];
	pq->len--;

	while (i < pq->len) {
		size_t min = i;
		size_t j = 2 * i + 1;
		size_t k = 2 * i + 2;
		if (j < pq->len && pq_less(&pq->heap[j], &pq->heap[i]))
			min = j;
		if (k < pq->len && pq_less(&pq->heap[k], &pq->heap[min]))
			min = k;
		if (min == i)
			break;
		SWAP(pq->heap[i], pq->heap[min]);
		i = min;
	}

	return e;
}

int merged_iter_pqueue_add(struct merged_iter_pqueue *pq, const struct pq_entry *e)
{
	size_t i = 0;

	REFTABLE_ALLOC_GROW_OR_NULL(pq->heap, pq->len + 1, pq->cap);
	if (!pq->heap)
		return REFTABLE_OUT_OF_MEMORY_ERROR;
	pq->heap[pq->len++] = *e;

	i = pq->len - 1;
	while (i > 0) {
		size_t j = (i - 1) / 2;
		if (pq_less(&pq->heap[j], &pq->heap[i]))
			break;
		SWAP(pq->heap[j], pq->heap[i]);
		i = j;
	}

	return 0;
}

void merged_iter_pqueue_release(struct merged_iter_pqueue *pq)
{
	REFTABLE_FREE_AND_NULL(pq->heap);
	memset(pq, 0, sizeof(*pq));
}
