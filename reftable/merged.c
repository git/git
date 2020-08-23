/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "merged.h"

#include "system.h"

#include "constants.h"
#include "iter.h"
#include "pq.h"
#include "reader.h"

static int merged_iter_init(struct merged_iter *mi)
{
	int i = 0;
	for (i = 0; i < mi->stack_len; i++) {
		struct record rec = new_record(mi->typ);
		int err = iterator_next(mi->stack[i], rec);
		if (err < 0) {
			return err;
		}

		if (err > 0) {
			iterator_destroy(&mi->stack[i]);
			record_clear(rec);
			free(record_yield(&rec));
		} else {
			struct pq_entry e = {
				.rec = rec,
				.index = i,
			};
			merged_iter_pqueue_add(&mi->pq, e);
		}
	}

	return 0;
}

static void merged_iter_close(void *p)
{
	struct merged_iter *mi = (struct merged_iter *)p;
	int i = 0;
	merged_iter_pqueue_clear(&mi->pq);
	for (i = 0; i < mi->stack_len; i++) {
		iterator_destroy(&mi->stack[i]);
	}
	free(mi->stack);
}

static int merged_iter_advance_subiter(struct merged_iter *mi, int idx)
{
	if (iterator_is_null(mi->stack[idx])) {
		return 0;
	}

	{
		struct record rec = new_record(mi->typ);
		struct pq_entry e = {
			.rec = rec,
			.index = idx,
		};
		int err = iterator_next(mi->stack[idx], rec);
		if (err < 0) {
			return err;
		}

		if (err > 0) {
			iterator_destroy(&mi->stack[idx]);
			record_clear(rec);
			free(record_yield(&rec));
			return 0;
		}

		merged_iter_pqueue_add(&mi->pq, e);
	}
	return 0;
}

static int merged_iter_next(struct merged_iter *mi, struct record rec)
{
	struct slice entry_key = {};
	struct pq_entry entry = merged_iter_pqueue_remove(&mi->pq);
	int err = merged_iter_advance_subiter(mi, entry.index);
	if (err < 0) {
		return err;
	}

	record_key(entry.rec, &entry_key);
	while (!merged_iter_pqueue_is_empty(mi->pq)) {
		struct pq_entry top = merged_iter_pqueue_top(mi->pq);
		struct slice k = {};
		int err = 0, cmp = 0;

		record_key(top.rec, &k);

		cmp = slice_compare(k, entry_key);
		free(slice_yield(&k));

		if (cmp > 0) {
			break;
		}

		merged_iter_pqueue_remove(&mi->pq);
		err = merged_iter_advance_subiter(mi, top.index);
		if (err < 0) {
			return err;
		}
		record_clear(top.rec);
		free(record_yield(&top.rec));
	}

	record_copy_from(rec, entry.rec, mi->hash_size);
	record_clear(entry.rec);
	free(record_yield(&entry.rec));
	free(slice_yield(&entry_key));
	return 0;
}

static int merged_iter_next_void(void *p, struct record rec)
{
	struct merged_iter *mi = (struct merged_iter *)p;
	if (merged_iter_pqueue_is_empty(mi->pq)) {
		return 1;
	}

	return merged_iter_next(mi, rec);
}

struct iterator_vtable merged_iter_vtable = {
	.next = &merged_iter_next_void,
	.close = &merged_iter_close,
};

static void iterator_from_merged_iter(struct iterator *it,
				      struct merged_iter *mi)
{
	it->iter_arg = mi;
	it->ops = &merged_iter_vtable;
}

int new_merged_table(struct merged_table **dest, struct reader **stack, int n)
{
	uint64_t last_max = 0;
	uint64_t first_min = 0;
	int i = 0;
	for (i = 0; i < n; i++) {
		struct reader *r = stack[i];
		if (i > 0 && last_max >= reader_min_update_index(r)) {
			return FORMAT_ERROR;
		}
		if (i == 0) {
			first_min = reader_min_update_index(r);
		}

		last_max = reader_max_update_index(r);
	}

	{
		struct merged_table m = {
			.stack = stack,
			.stack_len = n,
			.min = first_min,
			.max = last_max,
			.hash_size = SHA1_SIZE,
		};

		*dest = calloc(sizeof(struct merged_table), 1);
		**dest = m;
	}
	return 0;
}

void merged_table_close(struct merged_table *mt)
{
	int i = 0;
	for (i = 0; i < mt->stack_len; i++) {
		reader_free(mt->stack[i]);
	}
	FREE_AND_NULL(mt->stack);
	mt->stack_len = 0;
}

/* clears the list of subtable, without affecting the readers themselves. */
void merged_table_clear(struct merged_table *mt)
{
	FREE_AND_NULL(mt->stack);
	mt->stack_len = 0;
}

void merged_table_free(struct merged_table *mt)
{
	if (mt == NULL) {
		return;
	}
	merged_table_clear(mt);
	free(mt);
}

uint64_t merged_max_update_index(struct merged_table *mt)
{
	return mt->max;
}

uint64_t merged_min_update_index(struct merged_table *mt)
{
	return mt->min;
}

static int merged_table_seek_record(struct merged_table *mt,
				    struct iterator *it, struct record rec)
{
	struct iterator *iters = calloc(sizeof(struct iterator), mt->stack_len);
	struct merged_iter merged = {
		.stack = iters,
		.typ = record_type(rec),
		.hash_size = mt->hash_size,
	};
	int n = 0;
	int err = 0;
	int i = 0;
	for (i = 0; i < mt->stack_len && err == 0; i++) {
		int e = reader_seek(mt->stack[i], &iters[n], rec);
		if (e < 0) {
			err = e;
		}
		if (e == 0) {
			n++;
		}
	}
	if (err < 0) {
		int i = 0;
		for (i = 0; i < n; i++) {
			iterator_destroy(&iters[i]);
		}
		free(iters);
		return err;
	}

	merged.stack_len = n, err = merged_iter_init(&merged);
	if (err < 0) {
		merged_iter_close(&merged);
		return err;
	}

	{
		struct merged_iter *p = malloc(sizeof(struct merged_iter));
		*p = merged;
		iterator_from_merged_iter(it, p);
	}
	return 0;
}

int merged_table_seek_ref(struct merged_table *mt, struct iterator *it,
			  const char *name)
{
	struct ref_record ref = {
		.ref_name = (char *)name,
	};
	struct record rec = {};
	record_from_ref(&rec, &ref);
	return merged_table_seek_record(mt, it, rec);
}

int merged_table_seek_log_at(struct merged_table *mt, struct iterator *it,
			     const char *name, uint64_t update_index)
{
	struct log_record log = {
		.ref_name = (char *)name,
		.update_index = update_index,
	};
	struct record rec = {};
	record_from_log(&rec, &log);
	return merged_table_seek_record(mt, it, rec);
}

int merged_table_seek_log(struct merged_table *mt, struct iterator *it,
			  const char *name)
{
	uint64_t max = ~((uint64_t)0);
	return merged_table_seek_log_at(mt, it, name, max);
}
