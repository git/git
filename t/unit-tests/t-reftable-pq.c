/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "test-lib.h"
#include "reftable/constants.h"
#include "reftable/pq.h"

static void merged_iter_pqueue_check(const struct merged_iter_pqueue *pq)
{
	for (size_t i = 1; i < pq->len; i++) {
		size_t parent = (i - 1) / 2;
		check(pq_less(&pq->heap[parent], &pq->heap[i]));
	}
}

static void t_pq(void)
{
	struct merged_iter_pqueue pq = { 0 };
	struct reftable_record recs[54];
	size_t N = ARRAY_SIZE(recs) - 1, i;
	char *last = NULL;

	for (i = 0; i < N; i++) {
		struct strbuf refname = STRBUF_INIT;
		strbuf_addf(&refname, "%02"PRIuMAX, (uintmax_t)i);

		reftable_record_init(&recs[i], BLOCK_TYPE_REF);
		recs[i].u.ref.refname = strbuf_detach(&refname, NULL);
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
		struct pq_entry e = merged_iter_pqueue_remove(&pq);
		merged_iter_pqueue_check(&pq);

		check(reftable_record_type(e.rec) == BLOCK_TYPE_REF);
		if (last)
			check_int(strcmp(last, e.rec->u.ref.refname), <, 0);
		last = e.rec->u.ref.refname;
	}

	for (i = 0; i < N; i++)
		reftable_record_release(&recs[i]);
	merged_iter_pqueue_release(&pq);
}

int cmd_main(int argc, const char *argv[])
{
	TEST(t_pq(), "pq works");

	return test_done();
}
