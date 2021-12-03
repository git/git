/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"

#include "basics.h"
#include "constants.h"
#include "pq.h"
#include "record.h"
#include "reftable-tests.h"
#include "test_framework.h"

void merged_iter_pqueue_check(struct merged_iter_pqueue pq)
{
	int i;
	for (i = 1; i < pq.len; i++) {
		int parent = (i - 1) / 2;

		EXPECT(pq_less(&pq.heap[parent], &pq.heap[i]));
	}
}

static void test_pq(void)
{
	char *names[54] = { NULL };
	int N = ARRAY_SIZE(names) - 1;

	struct merged_iter_pqueue pq = { NULL };
	const char *last = NULL;

	int i = 0;
	for (i = 0; i < N; i++) {
		char name[100];
		snprintf(name, sizeof(name), "%02d", i);
		names[i] = xstrdup(name);
	}

	i = 1;
	do {
		struct reftable_record rec =
			reftable_new_record(BLOCK_TYPE_REF);
		struct pq_entry e = { 0 };

		reftable_record_as_ref(&rec)->refname = names[i];
		e.rec = rec;
		merged_iter_pqueue_add(&pq, e);
		merged_iter_pqueue_check(pq);
		i = (i * 7) % N;
	} while (i != 1);

	while (!merged_iter_pqueue_is_empty(pq)) {
		struct pq_entry e = merged_iter_pqueue_remove(&pq);
		struct reftable_ref_record *ref =
			reftable_record_as_ref(&e.rec);

		merged_iter_pqueue_check(pq);

		if (last) {
			EXPECT(strcmp(last, ref->refname) < 0);
		}
		last = ref->refname;
		ref->refname = NULL;
		reftable_free(ref);
	}

	for (i = 0; i < N; i++) {
		reftable_free(names[i]);
	}

	merged_iter_pqueue_release(&pq);
}

int pq_test_main(int argc, const char *argv[])
{
	RUN_TEST(test_pq);
	return 0;
}
