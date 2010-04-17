/*
 * packv4-create.c: management of dictionary tables used in pack v4
 *
 * (C) Nicolas Pitre <nico@fluxnic.net>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "cache.h"

struct data_entry {
	unsigned offset;
	unsigned hits;
};

struct dict_table {
	char *data;
	unsigned cur_offset;
	unsigned size;
	struct data_entry *entry;
	unsigned nb_entries;
	unsigned max_entries;
	unsigned *hash;
	unsigned hash_size;
};

struct dict_table *create_dict_table(void)
{
	return xcalloc(sizeof(struct dict_table), 1);
}

void destroy_dict_table(struct dict_table *t)
{
	free(t->data);
	free(t->entry);
	free(t->hash);
	free(t);
}

static int locate_entry(struct dict_table *t, const char *str)
{
	int i = 0;
	const unsigned char *s = (const unsigned char *) str;

	while (*s)
		i = i * 111 + *s++;
	i = (unsigned)i % t->hash_size;

	while (t->hash[i]) {
		unsigned n = t->hash[i] - 1;
		if (!strcmp(str, t->data + t->entry[n].offset))
			return n;
		if (++i >= t->hash_size)
			i = 0;
	}
	return -1 - i;
}

static void rehash_entries(struct dict_table *t)
{
	unsigned n;

	t->hash_size *= 2;
	if (t->hash_size < 1024)
		t->hash_size = 1024;
	t->hash = xrealloc(t->hash, t->hash_size * sizeof(*t->hash));
	memset(t->hash, 0, t->hash_size * sizeof(*t->hash));

	for (n = 0; n < t->nb_entries; n++) {
		int i = locate_entry(t, t->data + t->entry[n].offset);
		if (i < 0)
			t->hash[-1 - i] = n + 1;
	}
}

int dict_add_entry(struct dict_table *t, const char *str)
{
	int i, len = strlen(str) + 1;

	if (t->cur_offset + len >= t->size) {
		t->size = (t->size + len + 1024) * 3 / 2;
		t->data = xrealloc(t->data, t->size);
	}
	memcpy(t->data + t->cur_offset, str, len);

	i = (t->nb_entries) ? locate_entry(t, t->data + t->cur_offset) : -1;
	if (i >= 0) {
		t->entry[i].hits++;
		return i;
	}

	if (t->nb_entries >= t->max_entries) {
		t->max_entries = (t->max_entries + 1024) * 3 / 2;
		t->entry = xrealloc(t->entry, t->max_entries * sizeof(*t->entry));
	}
	t->entry[t->nb_entries].offset = t->cur_offset;
	t->entry[t->nb_entries].hits = 1;
	t->cur_offset += len + 1;
	t->nb_entries++;

	if (t->hash_size * 3 <= t->nb_entries * 4)
		rehash_entries(t);
	else
		t->hash[-1 - i] = t->nb_entries;

	return t->nb_entries - 1;
}

static int cmp_dict_entries(const void *a_, const void *b_)
{
	const struct data_entry *a = a_;
	const struct data_entry *b = b_;
	int diff = b->hits - a->hits;
	if (!diff)
		diff = a->offset - b->offset;
	return diff;
}

static void sort_dict_entries_by_hits(struct dict_table *t)
{
	qsort(t->entry, t->nb_entries, sizeof(*t->entry), cmp_dict_entries);
	t->hash_size = (t->nb_entries * 4 / 3) / 2;
	rehash_entries(t);
}

void dict_dump(struct dict_table *t)
{
	int i;

	sort_dict_entries_by_hits(t);
	for (i = 0; i < t->nb_entries; i++)
		printf("%d\t%s\n",
			t->entry[i].hits,
			t->data + t->entry[i].offset);
}
