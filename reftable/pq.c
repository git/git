/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "pq.h"

#include "reftable-record.h"
#include "system.h"
#include "basics.h"

int pq_less(struct pq_entry *a, struct pq_entry *b)
{
	int cmp = reftable_record_cmp(&a->rec, &b->rec);
	if (cmp == 0)
		return a->index > b->index;
	return cmp < 0;
}

struct pq_entry merged_iter_pqueue_top(struct merged_iter_pqueue pq)
{
	return pq.heap[0];
}

int merged_iter_pqueue_is_empty(struct merged_iter_pqueue pq)
{
	return pq.len == 0;
}

struct pq_entry merged_iter_pqueue_remove(struct merged_iter_pqueue *pq)
{
	int i = 0;
	struct pq_entry e = pq->heap[0];
	pq->heap[0] = pq->heap[pq->len - 1];
	pq->len--;

	i = 0;
	while (i < pq->len) {
		int min = i;
		int j = 2 * i + 1;
		int k = 2 * i + 2;
		if (j < pq->len && pq_less(&pq->heap[j], &pq->heap[i])) {
			min = j;
		}
		if (k < pq->len && pq_less(&pq->heap[k], &pq->heap[min])) {
			min = k;
		}

		if (min == i) {
			break;
		}

		SWAP(pq->heap[i], pq->heap[min]);
		i = min;
	}

	return e;
}

void merged_iter_pqueue_add(struct merged_iter_pqueue *pq, const struct pq_entry *e)
{
	int i = 0;

	REFTABLE_ALLOC_GROW(pq->heap, pq->len + 1, pq->cap);
	pq->heap[pq->len++] = *e;

	i = pq->len - 1;
	while (i > 0) {
		int j = (i - 1) / 2;
		if (pq_less(&pq->heap[j], &pq->heap[i])) {
			break;
		}

		SWAP(pq->heap[j], pq->heap[i]);

		i = j;
	}
}

void merged_iter_pqueue_release(struct merged_iter_pqueue *pq)
{
	int i = 0;
	for (i = 0; i < pq->len; i++) {
		reftable_record_release(&pq->heap[i].rec);
	}
	FREE_AND_NULL(pq->heap);
	pq->len = pq->cap = 0;
}
