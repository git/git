/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef PQ_H
#define PQ_H

#include "record.h"

struct pq_entry {
	struct record rec;
	int index;
};

int pq_less(struct pq_entry a, struct pq_entry b);

struct merged_iter_pqueue {
	struct pq_entry *heap;
	int len;
	int cap;
};

struct pq_entry merged_iter_pqueue_top(struct merged_iter_pqueue pq);
bool merged_iter_pqueue_is_empty(struct merged_iter_pqueue pq);
void merged_iter_pqueue_check(struct merged_iter_pqueue pq);
struct pq_entry merged_iter_pqueue_remove(struct merged_iter_pqueue *pq);
void merged_iter_pqueue_add(struct merged_iter_pqueue *pq, struct pq_entry e);
void merged_iter_pqueue_clear(struct merged_iter_pqueue *pq);

#endif
