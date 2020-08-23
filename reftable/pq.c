/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "pq.h"

#include "system.h"

int pq_less(struct pq_entry a, struct pq_entry b)
{
	struct slice ak = {};
	struct slice bk = {};
	int cmp = 0;
	record_key(a.rec, &ak);
	record_key(b.rec, &bk);

	cmp = slice_compare(ak, bk);

	free(slice_yield(&ak));
	free(slice_yield(&bk));

	if (cmp == 0) {
		return a.index > b.index;
	}

	return cmp < 0;
}

struct pq_entry merged_iter_pqueue_top(struct merged_iter_pqueue pq)
{
	return pq.heap[0];
}

bool merged_iter_pqueue_is_empty(struct merged_iter_pqueue pq)
{
	return pq.len == 0;
}

void merged_iter_pqueue_check(struct merged_iter_pqueue pq)
{
	int i = 0;
	for (i = 1; i < pq.len; i++) {
		int parent = (i - 1) / 2;

		assert(pq_less(pq.heap[parent], pq.heap[i]));
	}
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
		if (j < pq->len && pq_less(pq->heap[j], pq->heap[i])) {
			min = j;
		}
		if (k < pq->len && pq_less(pq->heap[k], pq->heap[min])) {
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

void merged_iter_pqueue_add(struct merged_iter_pqueue *pq, struct pq_entry e)
{
	int i = 0;
	if (pq->len == pq->cap) {
		pq->cap = 2 * pq->cap + 1;
		pq->heap = realloc(pq->heap, pq->cap * sizeof(struct pq_entry));
	}

	pq->heap[pq->len++] = e;
	i = pq->len - 1;
	while (i > 0) {
		int j = (i - 1) / 2;
		if (pq_less(pq->heap[j], pq->heap[i])) {
			break;
		}

		SWAP(pq->heap[j], pq->heap[i]);

		i = j;
	}
}

void merged_iter_pqueue_clear(struct merged_iter_pqueue *pq)
{
	int i = 0;
	for (i = 0; i < pq->len; i++) {
		record_clear(pq->heap[i].rec);
		free(record_yield(&pq->heap[i].rec));
	}
	FREE_AND_NULL(pq->heap);
	pq->len = pq->cap = 0;
}
