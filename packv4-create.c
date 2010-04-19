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
#include "object.h"
#include "tree-walk.h"

struct data_entry {
	unsigned offset;
	unsigned size;
	unsigned hits;
};

struct dict_table {
	unsigned char *data;
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

static int locate_entry(struct dict_table *t, const void *data, int size)
{
	int i = 0, len = size;
	const unsigned char *p = data;

	while (len--)
		i = i * 111 + *p++;
	i = (unsigned)i % t->hash_size;

	while (t->hash[i]) {
		unsigned n = t->hash[i] - 1;
		if (t->entry[n].size == size &&
		    memcmp(t->data + t->entry[n].offset, data, size) == 0)
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
		int i = locate_entry(t, t->data + t->entry[n].offset,
					t->entry[n].size);
		if (i < 0)
			t->hash[-1 - i] = n + 1;
	}
}

int dict_add_entry(struct dict_table *t, int val, const char *str)
{
	int i, val_len = 2, str_len = strlen(str) + 1;

	if (t->cur_offset + val_len + str_len > t->size) {
		t->size = (t->size + val_len + str_len + 1024) * 3 / 2;
		t->data = xrealloc(t->data, t->size);
	}

	t->data[t->cur_offset] = val >> 8;
	t->data[t->cur_offset + 1] = val;
	memcpy(t->data + t->cur_offset + val_len, str, str_len);

	i = (t->nb_entries) ?
		locate_entry(t, t->data + t->cur_offset, val_len + str_len) : -1;
	if (i >= 0) {
		t->entry[i].hits++;
		return i;
	}

	if (t->nb_entries >= t->max_entries) {
		t->max_entries = (t->max_entries + 1024) * 3 / 2;
		t->entry = xrealloc(t->entry, t->max_entries * sizeof(*t->entry));
	}
	t->entry[t->nb_entries].offset = t->cur_offset;
	t->entry[t->nb_entries].size = val_len + str_len;
	t->entry[t->nb_entries].hits = 1;
	t->cur_offset += val_len + str_len;
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

static struct dict_table *tree_path_table;

static int add_tree_dict_entries(void *buf, unsigned long size)
{
	struct tree_desc desc;
	struct name_entry name_entry;

	if (!tree_path_table)
		tree_path_table = create_dict_table();

	init_tree_desc(&desc, buf, size);
	while (tree_entry(&desc, &name_entry))
		dict_add_entry(tree_path_table, name_entry.mode,
			       name_entry.path);
	return 0;
}

void dict_dump(struct dict_table *t)
{
	int i;

	sort_dict_entries_by_hits(t);
	for (i = 0; i < t->nb_entries; i++) {
		int16_t val;
		uint16_t uval;
		val = t->data[t->entry[i].offset] << 8;
		val |= t->data[t->entry[i].offset + 1];
		uval = val;
		printf("%d\t%d\t%o\t%s\n",
			t->entry[i].hits, val, uval,
			t->data + t->entry[i].offset + 2);
	}
}

struct idx_entry
{
	off_t                offset;
	const unsigned char *sha1;
};

static int sort_by_offset(const void *e1, const void *e2)
{
	const struct idx_entry *entry1 = e1;
	const struct idx_entry *entry2 = e2;
	if (entry1->offset < entry2->offset)
		return -1;
	if (entry1->offset > entry2->offset)
		return 1;
	return 0;
}

static int create_pack_dictionaries(struct packed_git *p)
{
	uint32_t nr_objects, i;
	struct idx_entry *objects;

	nr_objects = p->num_objects;
	objects = xmalloc((nr_objects + 1) * sizeof(*objects));
	objects[nr_objects].offset = p->index_size - 40;
	for (i = 0; i < nr_objects; i++) {
		objects[i].sha1 = nth_packed_object_sha1(p, i);
		objects[i].offset = nth_packed_object_offset(p, i);
	}
	qsort(objects, nr_objects, sizeof(*objects), sort_by_offset);

	for (i = 0; i < nr_objects; i++) {
		void *data;
		enum object_type type;
		unsigned long size;
		struct object_info oi = {};

		oi.typep = &type;
		oi.sizep = &size;
		if (packed_object_info(p, objects[i].offset, &oi) < 0)
			die("cannot get type of %s from %s",
			    sha1_to_hex(objects[i].sha1), p->pack_name);

		switch (type) {
		case OBJ_TREE:
			break;
		default:
			continue;
		}
		data = unpack_entry(p, objects[i].offset, &type, &size);
		if (!data)
			die("cannot unpack %s from %s",
			    sha1_to_hex(objects[i].sha1), p->pack_name);
		if (check_sha1_signature(objects[i].sha1, data, size, typename(type)))
			die("packed %s from %s is corrupt",
			    sha1_to_hex(objects[i].sha1), p->pack_name);
		if (add_tree_dict_entries(data, size) < 0)
			die("can't process %s object %s",
				typename(type), sha1_to_hex(objects[i].sha1));
		free(data);
	}
	free(objects);

	return 0;
}

static int process_one_pack(const char *path)
{
	char arg[PATH_MAX];
	int len;
	struct packed_git *p;

	len = strlcpy(arg, path, PATH_MAX);
	if (len >= PATH_MAX)
		return error("name too long: %s", path);

	/*
	 * In addition to "foo.idx" we accept "foo.pack" and "foo";
	 * normalize these forms to "foo.idx" for add_packed_git().
	 */
	if (has_extension(arg, ".pack")) {
		strcpy(arg + len - 5, ".idx");
		len--;
	} else if (!has_extension(arg, ".idx")) {
		if (len + 4 >= PATH_MAX)
			return error("name too long: %s.idx", arg);
		strcpy(arg + len, ".idx");
		len += 4;
	}

	/*
	 * add_packed_git() uses our buffer (containing "foo.idx") to
	 * build the pack filename ("foo.pack").  Make sure it fits.
	 */
	if (len + 1 >= PATH_MAX) {
		arg[len - 4] = '\0';
		return error("name too long: %s.pack", arg);
	}

	p = add_packed_git(arg, len, 1);
	if (!p)
		return error("packfile %s not found.", arg);

	install_packed_git(p);
	if (open_pack_index(p))
		return error("packfile %s index not opened", p->pack_name);
	return create_pack_dictionaries(p);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <packfile>\n", argv[0]);
		exit(1);
	}
	process_one_pack(argv[1]);
	dict_dump(tree_path_table);
	return 0;
}
