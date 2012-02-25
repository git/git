/*
 * packv4-create.c: creation of dictionary tables and objects used in pack v4
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

int dict_add_entry(struct dict_table *t, int val, const char *str, int str_len)
{
	int i, val_len = 2;

	if (t->cur_offset + val_len + str_len + 1 > t->size) {
		t->size = (t->size + val_len + str_len + 1024) * 3 / 2;
		t->data = xrealloc(t->data, t->size);
	}

	t->data[t->cur_offset] = val >> 8;
	t->data[t->cur_offset + 1] = val;
	memcpy(t->data + t->cur_offset + val_len, str, str_len);
	t->data[t->cur_offset + val_len + str_len] = 0;

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
	t->cur_offset += val_len + str_len + 1;
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

static struct dict_table *commit_ident_table;
static struct dict_table *tree_path_table;

/*
 * Parse the author/committer line from a canonical commit object.
 * The 'from' argument points right after the "author " or "committer "
 * string.  The time zone is parsed and stored in *tz_val.  The returned
 * pointer is right after the end of the email address which is also just
 * before the time value, or NULL if a parsing error is encountered.
 */
static char *get_nameend_and_tz(char *from, int *tz_val)
{
	char *end, *tz;

	tz = strchr(from, '\n');
	/* let's assume the smallest possible string to be "x <x> 0 +0000\n" */
	if (!tz || tz - from < 13)
		return NULL;
	tz -= 4;
	end = tz - 4;
	while (end - from > 5 && *end != ' ')
		end--;
	if (end[-1] != '>' || end[0] != ' ' || tz[-2] != ' ')
		return NULL;
	*tz_val = (tz[0] - '0') * 1000 +
		  (tz[1] - '0') * 100 +
		  (tz[2] - '0') * 10 +
		  (tz[3] - '0');
	switch (tz[-1]) {
	default:	return NULL;
	case '+':	break;
	case '-':	*tz_val = -*tz_val;
	}
	return end;
}

static int add_commit_dict_entries(void *buf, unsigned long size)
{
	char *name, *end = NULL;
	int tz_val;

	if (!commit_ident_table)
		commit_ident_table = create_dict_table();

	/* parse and add author info */
	name = strstr(buf, "\nauthor ");
	if (name) {
		name += 8;
		end = get_nameend_and_tz(name, &tz_val);
	}
	if (!name || !end)
		return -1;
	dict_add_entry(commit_ident_table, tz_val, name, end - name);

	/* parse and add committer info */
	name = strstr(end, "\ncommitter ");
	if (name) {
	       name += 11;
	       end = get_nameend_and_tz(name, &tz_val);
	}
	if (!name || !end)
		return -1;
	dict_add_entry(commit_ident_table, tz_val, name, end - name);

	return 0;
}

static int add_tree_dict_entries(void *buf, unsigned long size)
{
	struct tree_desc desc;
	struct name_entry name_entry;

	if (!tree_path_table)
		tree_path_table = create_dict_table();

	init_tree_desc(&desc, buf, size);
	while (tree_entry(&desc, &name_entry)) {
		int pathlen = tree_entry_len(&name_entry);
		dict_add_entry(tree_path_table, name_entry.mode,
				name_entry.path, pathlen);
	}

	return 0;
}

void dump_dict_table(struct dict_table *t)
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

static void dict_dump(void)
{
	dump_dict_table(commit_ident_table);
	dump_dict_table(tree_path_table);
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

static struct idx_entry *get_packed_object_list(struct packed_git *p)
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

	return objects;
}

static int create_pack_dictionaries(struct packed_git *p,
				    struct idx_entry *objects)
{
	unsigned int i;

	for (i = 0; i < p->num_objects; i++) {
		void *data;
		enum object_type type;
		unsigned long size;
		struct object_info oi = {};
		int (*add_dict_entries)(void *, unsigned long);

		oi.typep = &type;
		oi.sizep = &size;
		if (packed_object_info(p, objects[i].offset, &oi) < 0)
			die("cannot get type of %s from %s",
			    sha1_to_hex(objects[i].sha1), p->pack_name);

		switch (type) {
		case OBJ_COMMIT:
			add_dict_entries = add_commit_dict_entries;
			break;
		case OBJ_TREE:
			add_dict_entries = add_tree_dict_entries;
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
		if (add_dict_entries(data, size) < 0)
			die("can't process %s object %s",
				typename(type), sha1_to_hex(objects[i].sha1));
		free(data);
	}

	return 0;
}

static struct packed_git *open_pack(const char *path)
{
	char arg[PATH_MAX];
	int len;
	struct packed_git *p;

	len = strlcpy(arg, path, PATH_MAX);
	if (len >= PATH_MAX) {
		error("name too long: %s", path);
		return NULL;
	}

	/*
	 * In addition to "foo.idx" we accept "foo.pack" and "foo";
	 * normalize these forms to "foo.idx" for add_packed_git().
	 */
	if (has_extension(arg, ".pack")) {
		strcpy(arg + len - 5, ".idx");
		len--;
	} else if (!has_extension(arg, ".idx")) {
		if (len + 4 >= PATH_MAX) {
			error("name too long: %s.idx", arg);
			return NULL;
		}
		strcpy(arg + len, ".idx");
		len += 4;
	}

	/*
	 * add_packed_git() uses our buffer (containing "foo.idx") to
	 * build the pack filename ("foo.pack").  Make sure it fits.
	 */
	if (len + 1 >= PATH_MAX) {
		arg[len - 4] = '\0';
		error("name too long: %s.pack", arg);
		return NULL;
	}

	p = add_packed_git(arg, len, 1);
	if (!p) {
		error("packfile %s not found.", arg);
		return NULL;
	}

	install_packed_git(p);
	if (open_pack_index(p)) {
		error("packfile %s index not opened", p->pack_name);
		return NULL;
	}

	return p;
}

static void process_one_pack(char *src_pack)
{
	struct packed_git *p;
	struct idx_entry *objs;

	p = open_pack(src_pack);
	if (!p)
		die("unable to open source pack");

	objs = get_packed_object_list(p);
	create_pack_dictionaries(p, objs);
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <packfile>\n", argv[0]);
		exit(1);
	}
	process_one_pack(argv[1]);
	dict_dump();
	return 0;
}
