/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "unit-test.h"
#include "lib-reftable.h"
#include "reftable/constants.h"
#include "reftable/pq.h"
#include "strbuf.h"

static void merged_iter_pqueue_check(const struct merged_iter_pqueue *pq)
{
	for (size_t i = 1; i < pq->len; i++) {
		size_t parent = (i - 1) / 2;
		cl_assert(pq_less(&pq->heap[parent], &pq->heap[i]) != 0);
	}
}

static int pq_entry_equal(struct pq_entry *a, struct pq_entry *b)
{
	int cmp;
	cl_assert_equal_i(reftable_record_cmp(a->rec, b->rec, &cmp), 0);
	return !cmp && (a->index == b->index);
}

void test_reftable_pq__record(void)
{
	struct merged_iter_pqueue pq = { 0 };
	struct reftable_record recs[54];
	size_t N = ARRAY_SIZE(recs) - 1, i;
	char *last = NULL;

	for (i = 0; i < N; i++) {
		cl_assert(!reftable_record_init(&recs[i],
						REFTABLE_BLOCK_TYPE_REF));
		recs[i].u.ref.refname = xstrfmt("%02"PRIuMAX, (uintmax_t)i);
	}

	i = 1;
	do {
		struct pq_entry e = {
			.rec = &recs[i],
		};

		merged_iter_pqueue_add(&pq, &e);
		merged_iter_pqueue_check(&pq);
		i = (i * 7) % N;
	} while (i != 1);

	while (!merged_iter_pqueue_is_empty(pq)) {
		struct pq_entry top = merged_iter_pqueue_top(pq);
		struct pq_entry e;

		cl_assert_equal_i(merged_iter_pqueue_remove(&pq, &e), 0);
		merged_iter_pqueue_check(&pq);

		cl_assert(pq_entry_equal(&top, &e));
		cl_assert(reftable_record_type(e.rec) == REFTABLE_BLOCK_TYPE_REF);
		if (last)
			cl_assert(strcmp(last, e.rec->u.ref.refname) < 0);
		last = e.rec->u.ref.refname;
	}

	for (i = 0; i < N; i++)
		reftable_record_release(&recs[i]);
	merged_iter_pqueue_release(&pq);
}

void test_reftable_pq__index(void)
{
	struct merged_iter_pqueue pq = { 0 };
	struct reftable_record recs[13];
	char *last = NULL;
	size_t N = ARRAY_SIZE(recs), i;

	for (i = 0; i < N; i++) {
		cl_assert(!reftable_record_init(&recs[i],
						REFTABLE_BLOCK_TYPE_REF));
		recs[i].u.ref.refname = (char *) "refs/heads/master";
	}

	i = 1;
	do {
		struct pq_entry e = {
			.rec = &recs[i],
			.index = i,
		};

		merged_iter_pqueue_add(&pq, &e);
		merged_iter_pqueue_check(&pq);
		i = (i * 7) % N;
	} while (i != 1);

	for (i = N - 1; i > 0; i--) {
		struct pq_entry top = merged_iter_pqueue_top(pq);
		struct pq_entry e;

		cl_assert_equal_i(merged_iter_pqueue_remove(&pq, &e), 0);
		merged_iter_pqueue_check(&pq);

		cl_assert(pq_entry_equal(&top, &e));
		cl_assert(reftable_record_type(e.rec) == REFTABLE_BLOCK_TYPE_REF);
		cl_assert_equal_i(e.index, i);
		if (last)
			cl_assert_equal_s(last, e.rec->u.ref.refname);
		last = e.rec->u.ref.refname;
	}

	merged_iter_pqueue_release(&pq);
}

void test_reftable_pq__merged_iter_pqueue_top(void)
{
	struct merged_iter_pqueue pq = { 0 };
	struct reftable_record recs[13];
	size_t N = ARRAY_SIZE(recs), i;

	for (i = 0; i < N; i++) {
		cl_assert(!reftable_record_init(&recs[i],
						REFTABLE_BLOCK_TYPE_REF));
		recs[i].u.ref.refname = (char *) "refs/heads/master";
	}

	i = 1;
	do {
		struct pq_entry e = {
			.rec = &recs[i],
			.index = i,
		};

		merged_iter_pqueue_add(&pq, &e);
		merged_iter_pqueue_check(&pq);
		i = (i * 7) % N;
	} while (i != 1);

	for (i = N - 1; i > 0; i--) {
		struct pq_entry top = merged_iter_pqueue_top(pq);
		struct pq_entry e;

		cl_assert_equal_i(merged_iter_pqueue_remove(&pq, &e), 0);

		merged_iter_pqueue_check(&pq);
		cl_assert(pq_entry_equal(&top, &e) != 0);
		cl_assert(reftable_record_equal(top.rec, &recs[i], REFTABLE_HASH_SIZE_SHA1) != 0);
		for (size_t j = 0; i < pq.len; j++) {
			cl_assert(pq_less(&top, &pq.heap[j]) != 0);
			cl_assert(top.index > j);
		}
	}

	merged_iter_pqueue_release(&pq);
}
