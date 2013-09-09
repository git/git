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
#include "pack.h"
#include "pack-revindex.h"
#include "progress.h"
#include "varint.h"


static int pack_compression_seen;
static int pack_compression_level = Z_DEFAULT_COMPRESSION;
static int min_tree_copy = 1;

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
	/* let's assume the smallest possible string to be " <> 0 +0000\n" */
	if (!tz || tz - from < 11)
		return NULL;
	tz -= 4;
	end = tz - 4;
	while (end - from > 3 && *end != ' ')
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

int add_commit_dict_entries(struct dict_table *commit_ident_table,
			    void *buf, unsigned long size)
{
	char *name, *end = NULL;
	int tz_val;

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

static int add_tree_dict_entries(struct dict_table *tree_path_table,
				 void *buf, unsigned long size)
{
	struct tree_desc desc;
	struct name_entry name_entry;

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

/*
 * Encode an object SHA1 reference with either an object index into the
 * pack SHA1 table incremented by 1, or the literal SHA1 value prefixed
 * with a zero byte if the needed SHA1 is not available in the table.
 */
static struct pack_idx_entry *all_objs;
static unsigned all_objs_nr;
static int encode_sha1ref(const unsigned char *sha1, unsigned char *buf)
{
	unsigned lo = 0, hi = all_objs_nr;

	do {
		unsigned mi = (lo + hi) / 2;
		int cmp = hashcmp(all_objs[mi].sha1, sha1);

		if (cmp == 0)
			return encode_varint(mi + 1, buf);
		if (cmp > 0)
			hi = mi;
		else
			lo = mi+1;
	} while (lo < hi);

	*buf++ = 0;
	hashcpy(buf, sha1);
	return 1 + 20;
}

/*
 * This converts a canonical commit object buffer into its
 * tightly packed representation using the already populated
 * and sorted commit_ident_table dictionary.  The parsing is
 * strict so to ensure the canonical version may always be
 * regenerated and produce the same hash.
 */
void *pv4_encode_commit(void *buffer, unsigned long *sizep)
{
	unsigned long size = *sizep;
	char *in, *tail, *end;
	unsigned char *out;
	unsigned char sha1[20];
	int nb_parents, author_index, commit_index, tz_val;
	unsigned long author_time, commit_time;
	z_stream stream;
	int status;

	/*
	 * It is guaranteed that the output is always going to be smaller
	 * than the input.  We could even do this conversion in place.
	 */
	in = buffer;
	tail = in + size;
	buffer = xmalloc(size);
	out = buffer;

	/* parse the "tree" line */
	if (in + 46 >= tail || memcmp(in, "tree ", 5) || in[45] != '\n')
		goto bad_data;
	if (get_sha1_lowhex(in + 5, sha1) < 0)
		goto bad_data;
	in += 46;
	out += encode_sha1ref(sha1, out);

	/* count how many "parent" lines */
	nb_parents = 0;
	while (in + 48 < tail && !memcmp(in, "parent ", 7) && in[47] == '\n') {
		nb_parents++;
		in += 48;
	}
	out += encode_varint(nb_parents, out);

	/* rewind and parse the "parent" lines */
	in -= 48 * nb_parents;
	while (nb_parents--) {
		if (get_sha1_lowhex(in + 7, sha1))
			goto bad_data;
		out += encode_sha1ref(sha1, out);
		in += 48;
	}

	/* parse the "author" line */
	/* it must be at least "author x <x> 0 +0000\n" i.e. 21 chars */
	if (in + 21 >= tail || memcmp(in, "author ", 7))
		goto bad_data;
	in += 7;
	end = get_nameend_and_tz(in, &tz_val);
	if (!end)
		goto bad_data;
	author_index = dict_add_entry(commit_ident_table, tz_val, in, end - in);
	if (author_index < 0)
		goto bad_dict;
	author_time = strtoul(end, &end, 10);
	if (!end || end[0] != ' ' || end[6] != '\n')
		goto bad_data;
	in = end + 7;

	/* parse the "committer" line */
	/* it must be at least "committer x <x> 0 +0000\n" i.e. 24 chars */
	if (in + 24 >= tail || memcmp(in, "committer ", 7))
		goto bad_data;
	in += 10;
	end = get_nameend_and_tz(in, &tz_val);
	if (!end)
		goto bad_data;
	commit_index = dict_add_entry(commit_ident_table, tz_val, in, end - in);
	if (commit_index < 0)
		goto bad_dict;
	commit_time = strtoul(end, &end, 10);
	if (!end || end[0] != ' ' || end[6] != '\n')
		goto bad_data;
	in = end + 7;

	/*
	 * After the tree and parents, we store committer time, committer
	 * index, author time and author index.  This is so that the most
	 * important items for history traversal (parents, committer time,
	 * sometimes the tree) are close together to allow partial decode.
	 */
	out += encode_varint(commit_time, out);
	out += encode_varint(commit_index, out);

	if (author_time <= commit_time)
		author_time = (commit_time - author_time) << 1;
	else
		author_time = ((author_time - commit_time) << 1) | 1;

	out += encode_varint(author_time, out);
	out += encode_varint(author_index, out);

	/* finally, deflate the remaining data */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, pack_compression_level);
	stream.next_in = (unsigned char *)in;
	stream.avail_in = tail - in;
	stream.next_out = (unsigned char *)out;
	stream.avail_out = size - (out - (unsigned char *)buffer);
	status = deflate(&stream, Z_FINISH);
	end = (char *)stream.next_out;
	deflateEnd(&stream);
	if (status != Z_STREAM_END) {
		error("deflate error status %d", status);
		goto bad;
	}

	*sizep = end - (char *)buffer;
	return buffer;

bad_data:
	error("bad commit data");
	goto bad;
bad_dict:
	error("bad dict entry");
bad:
	free(buffer);
	return NULL;
}

static int compare_tree_entries(struct name_entry *e1, struct name_entry *e2)
{
	int len1 = tree_entry_len(e1);
	int len2 = tree_entry_len(e2);
	int len = len1 < len2 ? len1 : len2;
	unsigned char c1, c2;
	int cmp;

	cmp = memcmp(e1->path, e2->path, len);
	if (cmp)
		return cmp;
	c1 = e1->path[len];
	c2 = e2->path[len];
	if (!c1 && S_ISDIR(e1->mode))
		c1 = '/';
	if (!c2 && S_ISDIR(e2->mode))
		c2 = '/';
	return c1 - c2;
}

/*
 * This converts a canonical tree object buffer into its
 * tightly packed representation using the already populated
 * and sorted tree_path_table dictionary.  The parsing is
 * strict so to ensure the canonical version may always be
 * regenerated and produce the same hash.
 *
 * If a delta buffer is provided, we may encode multiple ranges of tree
 * entries against that buffer.
 */
void *pv4_encode_tree(void *_buffer, unsigned long *sizep,
		      void *delta, unsigned long delta_size,
		      const unsigned char *delta_sha1)
{
	unsigned long size = *sizep;
	unsigned char *in, *out, *end, *buffer = _buffer;
	struct tree_desc desc, delta_desc;
	struct name_entry name_entry, delta_entry;
	int nb_entries;
	unsigned int copy_start = 0, copy_count = 0, copy_pos = 0, copy_end = 0;
	unsigned int delta_pos = 0, first_delta = 1;

	if (!size)
		return NULL;

	if (!delta_size || !min_tree_copy)
		delta = NULL;

	/*
	 * We can't make sure the result will always be smaller than the
	 * input. The smallest possible entry is "0 x\0<40 byte SHA1>"
	 * or 44 bytes.  The output entry may have a realistic path index
	 * encoding using up to 3 bytes, and a non indexable SHA1 meaning
	 * 41 bytes.  And the output data already has the nb_entries
	 * headers.  In practice the output size will be significantly
	 * smaller but for now let's make it simple.
	 */
	in = buffer;
	out = xmalloc(size + 48);
	end = out + size + 48;
	buffer = out;

	/* let's count how many entries there are */
	init_tree_desc(&desc, in, size);
	nb_entries = 0;
	while (tree_entry(&desc, &name_entry))
		nb_entries++;
	out += encode_varint(nb_entries, out);

	init_tree_desc(&desc, in, size);
	if (delta) {
		init_tree_desc(&delta_desc, delta, delta_size);
		if (!tree_entry(&delta_desc, &delta_entry))
			delta = NULL;
	}

	while (desc.size) {
		int pathlen, index;

		/*
		 * We don't want any zero-padded mode.  We won't be able
		 * to recreate such an object byte for byte.
		 */
		if (*(const char *)desc.buffer == '0') {
			error("zero-padded mode encountered");
			free(buffer);
			return NULL;
		}

		tree_entry(&desc, &name_entry);

		/*
		 * Try to match entries against our delta object.
		 */
		if (delta) {
			int ret;

			do {
				ret = compare_tree_entries(&name_entry, &delta_entry);
				if (ret <= 0 || copy_count != 0)
					break;
				delta_pos++;
				if (!tree_entry(&delta_desc, &delta_entry))
					delta = NULL;
			} while (delta);

			if (ret == 0 && name_entry.mode == delta_entry.mode &&
			    hashcmp(name_entry.sha1, delta_entry.sha1) == 0) {
				if (!copy_count) {
					copy_start = delta_pos;
					copy_pos = out - buffer;
					copy_end = 0;
				}
				copy_count++;
				delta_pos++;
				if (!tree_entry(&delta_desc, &delta_entry))
					delta = NULL;
			} else
				copy_end = 1;
		} else
			copy_end = 1;

		if (copy_count && copy_end) {
			unsigned char copy_buf[48], *cp = copy_buf;

			/*
			 * Let's write a sequence indicating we're copying
			 * entries from another object:
			 *
			 * entry_start + entry_count + object_ref
			 *
			 * To distinguish between 'entry_start' and an actual
			 * entry index, we use the LSB = 1.
			 *
			 * Furthermore, if object_ref is the same as the
			 * preceding one, we can omit it and save some
			 * more space, especially if that ends up being a
			 * full sha1 reference.  Let's steal the LSB
			 * of entry_count for that purpose.
			 */
			copy_start = (copy_start << 1) | 1;
			copy_count = (copy_count << 1) | first_delta;
			cp += encode_varint(copy_start, cp);
			cp += encode_varint(copy_count, cp);
			if (first_delta)
				cp += encode_sha1ref(delta_sha1, cp);

			/*
			 * Now let's make sure this is going to take less
			 * space than the corresponding direct entries we've
			 * created in parallel.  If so we dump the copy
			 * sequence over those entries in the output buffer.
			 */
			if (copy_count >= min_tree_copy &&
			    cp - copy_buf < out - &buffer[copy_pos]) {
				out = buffer + copy_pos;
				memcpy(out, copy_buf, cp - copy_buf);
				out += cp - copy_buf;
				first_delta = 0;
			}
			copy_count = 0;
		}

		if (end - out < 48) {
			unsigned long sofar = out - buffer;
			buffer = xrealloc(buffer, (sofar + 48)*2);
			end = buffer + (sofar + 48)*2;
			out = buffer + sofar;
		}

		pathlen = tree_entry_len(&name_entry);
		index = dict_add_entry(tree_path_table, name_entry.mode,
				       name_entry.path, pathlen);
		if (index < 0) {
			error("missing tree dict entry");
			free(buffer);
			return NULL;
		}
		out += encode_varint(index << 1, out);
		out += encode_sha1ref(name_entry.sha1, out);
	}

	if (copy_count) {
		/* process the trailing copy */
		unsigned char copy_buf[48], *cp = copy_buf;
		copy_start = (copy_start << 1) | 1;
		copy_count = (copy_count << 1) | first_delta;
		cp += encode_varint(copy_start, cp);
		cp += encode_varint(copy_count, cp);
		if (first_delta)
			cp += encode_sha1ref(delta_sha1, cp);
		if (copy_count >= min_tree_copy &&
		    cp - copy_buf < out - &buffer[copy_pos]) {
			out = buffer + copy_pos;
			memcpy(out, copy_buf, cp - copy_buf);
			out += cp - copy_buf;
		}
	}

	*sizep = out - buffer;
	return buffer;
}

static struct pack_idx_entry *get_packed_object_list(struct packed_git *p)
{
	unsigned i, nr_objects = p->num_objects;
	struct pack_idx_entry *objects;

	objects = xmalloc((nr_objects + 1) * sizeof(*objects));
	objects[nr_objects].offset = p->pack_size - 20;
	for (i = 0; i < nr_objects; i++) {
		hashcpy(objects[i].sha1, nth_packed_object_sha1(p, i));
		objects[i].offset = nth_packed_object_offset(p, i);
	}

	return objects;
}

static int sort_by_offset(const void *e1, const void *e2)
{
	const struct pack_idx_entry * const *entry1 = e1;
	const struct pack_idx_entry * const *entry2 = e2;
	if ((*entry1)->offset < (*entry2)->offset)
		return -1;
	if ((*entry1)->offset > (*entry2)->offset)
		return 1;
	return 0;
}

static struct pack_idx_entry **sort_objs_by_offset(struct pack_idx_entry *list,
						    unsigned nr_objects)
{
	unsigned i;
	struct pack_idx_entry **sorted;

	sorted = xmalloc((nr_objects + 1) * sizeof(*sorted));
	for (i = 0; i < nr_objects + 1; i++)
		sorted[i] = &list[i];
	qsort(sorted, nr_objects + 1, sizeof(*sorted), sort_by_offset);

	return sorted;
}

static int create_pack_dictionaries(struct packed_git *p,
				    struct pack_idx_entry **obj_list)
{
	struct progress *progress_state;
	unsigned int i;

	commit_ident_table = create_dict_table();
	tree_path_table = create_dict_table();

	progress_state = start_progress("Scanning objects", p->num_objects);
	for (i = 0; i < p->num_objects; i++) {
		struct pack_idx_entry *obj = obj_list[i];
		void *data;
		enum object_type type;
		unsigned long size;
		struct object_info oi = {};
		int (*add_dict_entries)(struct dict_table *, void *, unsigned long);
		struct dict_table *dict;

		display_progress(progress_state, i+1);

		oi.typep = &type;
		oi.sizep = &size;
		if (packed_object_info(p, obj->offset, &oi) < 0)
			die("cannot get type of %s from %s",
			    sha1_to_hex(obj->sha1), p->pack_name);

		switch (type) {
		case OBJ_COMMIT:
			add_dict_entries = add_commit_dict_entries;
			dict = commit_ident_table;
			break;
		case OBJ_TREE:
			add_dict_entries = add_tree_dict_entries;
			dict = tree_path_table;
			break;
		default:
			continue;
		}
		data = unpack_entry(p, obj->offset, &type, &size);
		if (!data)
			die("cannot unpack %s from %s",
			    sha1_to_hex(obj->sha1), p->pack_name);
		if (check_sha1_signature(obj->sha1, data, size, typename(type)))
			die("packed %s from %s is corrupt",
			    sha1_to_hex(obj->sha1), p->pack_name);
		if (add_dict_entries(dict, data, size) < 0)
			die("can't process %s object %s",
				typename(type), sha1_to_hex(obj->sha1));
		free(data);
	}

	stop_progress(&progress_state);
	return 0;
}

static unsigned long write_dict_table(struct sha1file *f, struct dict_table *t)
{
	unsigned char buffer[1024];
	unsigned hdrlen;
	unsigned long size, datalen;
	z_stream stream;
	int i, status;

	/*
	 * Stored dict table format: uncompressed data length followed by
	 * compressed content.
	 */

	datalen = t->cur_offset;
	hdrlen = encode_varint(datalen, buffer);
	sha1write(f, buffer, hdrlen);

	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, pack_compression_level);

	for (i = 0; i < t->nb_entries; i++) {
		stream.next_in = t->data + t->entry[i].offset;
		stream.avail_in = 2 + strlen((char *)t->data + t->entry[i].offset + 2) + 1;
		do {
			stream.next_out = buffer;
			stream.avail_out = sizeof(buffer);
			status = deflate(&stream, 0);
			size = stream.next_out - (unsigned char *)buffer;
			sha1write(f, buffer, size);
		} while (status == Z_OK);
	}
	do {
		stream.next_out = buffer;
		stream.avail_out = sizeof(buffer);
		status = deflate(&stream, Z_FINISH);
		size = stream.next_out - (unsigned char *)buffer;
		sha1write(f, buffer, size);
	} while (status == Z_OK);
	if (status != Z_STREAM_END)
		die("unable to deflate dictionary table (%d)", status);
	if (stream.total_in != datalen)
		die("dict data size mismatch (%ld vs %ld)",
		    stream.total_in, datalen);
	datalen = stream.total_out;
	deflateEnd(&stream);

	return hdrlen + datalen;
}

static struct sha1file * packv4_open(char *path)
{
	int fd;

	fd = open(path, O_CREAT|O_EXCL|O_WRONLY, 0600);
	if (fd < 0)
		die_errno("unable to create '%s'", path);
	return sha1fd(fd, path);
}

static unsigned int packv4_write_header(struct sha1file *f, unsigned nr_objects)
{
	struct pack_header hdr;

	hdr.hdr_signature = htonl(PACK_SIGNATURE);
	hdr.hdr_version = htonl(4);
	hdr.hdr_entries = htonl(nr_objects);
	sha1write(f, &hdr, sizeof(hdr));

	return sizeof(hdr);
}

static unsigned long packv4_write_tables(struct sha1file *f, unsigned nr_objects,
					 struct pack_idx_entry *objs)
{
	unsigned i;
	unsigned long written = 0;

	/* The sorted list of object SHA1's is always first */
	for (i = 0; i < nr_objects; i++)
		sha1write(f, objs[i].sha1, 20);
	written = 20 * nr_objects;

	/* Then the commit dictionary table */
	written += write_dict_table(f, commit_ident_table);

	/* Followed by the path component dictionary table */
	written += write_dict_table(f, tree_path_table);

	return written;
}

static int write_object_header(struct sha1file *f, enum object_type type, unsigned long size)
{
	unsigned char buf[16];
	uint64_t val;
	int len;

	/*
	 * We really have only one kind of delta object.
	 */
	if (type == OBJ_OFS_DELTA)
		type = OBJ_REF_DELTA;

	/*
	 * We allocate 4 bits in the LSB for the object type which should
	 * be good for quite a while, given that we effectively encodes
	 * only 5 object types: commit, tree, blob, delta, tag.
	 */
	val = size;
	if (MSB(val, 4))
		die("fixme: the code doesn't currently cope with big sizes");
	val <<= 4;
	val |= type;
	len = encode_varint(val, buf);
	sha1write(f, buf, len);
	return len;
}

static unsigned long copy_object_data(struct sha1file *f, struct packed_git *p,
				      off_t offset)
{
	struct pack_window *w_curs = NULL;
	struct revindex_entry *revidx;
	enum object_type type;
	unsigned long avail, size, datalen, written;
	int hdrlen, reflen, idx_nr;
	unsigned char *src, buf[24];

	revidx = find_pack_revindex(p, offset);
	idx_nr = revidx->nr;
	datalen = revidx[1].offset - offset;

	src = use_pack(p, &w_curs, offset, &avail);
	hdrlen = unpack_object_header_buffer(src, avail, &type, &size);

	written = write_object_header(f, type, size);

	if (type == OBJ_OFS_DELTA) {
		const unsigned char *cp = src + hdrlen;
		off_t base_offset = decode_varint(&cp);
		hdrlen = cp - src;
		base_offset = offset - base_offset;
		if (base_offset <= 0 || base_offset >= offset)
			die("delta offset out of bound");
		revidx = find_pack_revindex(p, base_offset);
		reflen = encode_sha1ref(nth_packed_object_sha1(p, revidx->nr), buf);
		sha1write(f, buf, reflen);
		written += reflen;
	} else if (type == OBJ_REF_DELTA) {
		reflen = encode_sha1ref(src + hdrlen, buf);
		hdrlen += 20;
		sha1write(f, buf, reflen);
		written += reflen;
	}

	if (p->index_version > 1 &&
	    check_pack_crc(p, &w_curs, offset, datalen, idx_nr))
		die("bad CRC for object at offset %"PRIuMAX" in %s",
		    (uintmax_t)offset, p->pack_name);

	offset += hdrlen;
	datalen -= hdrlen;

	while (datalen) {
		src = use_pack(p, &w_curs, offset, &avail);
		if (avail > datalen)
			avail = datalen;
		sha1write(f, src, avail);
		written += avail;
		offset += avail;
		datalen -= avail;
	}
	unuse_pack(&w_curs);

	return written;
}

static unsigned char *get_delta_base(struct packed_git *p, off_t offset,
				     unsigned char *sha1_buf)
{
	struct pack_window *w_curs = NULL;
	enum object_type type;
	unsigned long avail, size;
	int hdrlen;
	unsigned char *src;
	const unsigned char *base_sha1 = NULL; ;

	src = use_pack(p, &w_curs, offset, &avail);
	hdrlen = unpack_object_header_buffer(src, avail, &type, &size);

	if (type == OBJ_OFS_DELTA) {
		const unsigned char *cp = src + hdrlen;
		off_t base_offset = decode_varint(&cp);
		base_offset = offset - base_offset;
		if (base_offset <= 0 || base_offset >= offset) {
			error("delta offset out of bound");
		} else {
			struct revindex_entry *revidx;
			revidx = find_pack_revindex(p, base_offset);
			base_sha1 = nth_packed_object_sha1(p, revidx->nr);
		}
	} else if (type == OBJ_REF_DELTA) {
		base_sha1 = src + hdrlen;
	} else
		error("expected to get a delta but got a %s", typename(type));

	unuse_pack(&w_curs);

	if (!base_sha1)
		return NULL;
	hashcpy(sha1_buf, base_sha1);
	return sha1_buf;
}

static off_t packv4_write_object(struct sha1file *f, struct packed_git *p,
				 struct pack_idx_entry *obj)
{
	void *src, *result;
	struct object_info oi = {};
	enum object_type type, packed_type;
	unsigned long obj_size, buf_size;
	unsigned int hdrlen;

	oi.typep = &type;
	oi.sizep = &obj_size;
	packed_type = packed_object_info(p, obj->offset, &oi);
	if (packed_type < 0)
		die("cannot get type of %s from %s",
		    sha1_to_hex(obj->sha1), p->pack_name);

	/* Some objects are copied without decompression */
	switch (type) {
	case OBJ_COMMIT:
	case OBJ_TREE:
		break;
	default:
		return copy_object_data(f, p, obj->offset);
	}

	/* The rest is converted into their new format */
	src = unpack_entry(p, obj->offset, &type, &buf_size);
	if (!src || obj_size != buf_size)
		die("cannot unpack %s from %s",
		    sha1_to_hex(obj->sha1), p->pack_name);
	if (check_sha1_signature(obj->sha1, src, buf_size, typename(type)))
		die("packed %s from %s is corrupt",
		    sha1_to_hex(obj->sha1), p->pack_name);

	switch (type) {
	case OBJ_COMMIT:
		result = pv4_encode_commit(src, &buf_size);
		break;
	case OBJ_TREE:
		if (packed_type != OBJ_TREE) {
			unsigned char sha1_buf[20], *ref_sha1;
			void *ref;
			enum object_type ref_type;
			unsigned long ref_size;

			ref_sha1 = get_delta_base(p, obj->offset, sha1_buf);
			if (!ref_sha1)
				die("unable to get delta base sha1 for %s",
						sha1_to_hex(obj->sha1));
			ref = read_sha1_file(ref_sha1, &ref_type, &ref_size);
			if (!ref || ref_type != OBJ_TREE)
				die("cannot obtain delta base for %s",
						sha1_to_hex(obj->sha1));
			result = pv4_encode_tree(src, &buf_size,
						 ref, ref_size, ref_sha1);
			free(ref);
		} else {
			result = pv4_encode_tree(src, &buf_size, NULL, 0, NULL);
		}
		break;
	default:
		die("unexpected object type %d", type);
	}
	free(src);
	if (!result) {
		warning("can't convert %s object %s",
			typename(type), sha1_to_hex(obj->sha1));
		/* fall back to copy the object in its original form */
		return copy_object_data(f, p, obj->offset);
	}

	/* Use bit 3 to indicate a special type encoding */
	type += 8;
	hdrlen = write_object_header(f, type, obj_size);
	sha1write(f, result, buf_size);
	free(result);
	return hdrlen + buf_size;
}

static char *normalize_pack_name(const char *path)
{
	char buf[PATH_MAX];
	int len;

	len = strlcpy(buf, path, PATH_MAX);
	if (len >= PATH_MAX - 6)
		die("name too long: %s", path);

	/*
	 * In addition to "foo.idx" we accept "foo.pack" and "foo";
	 * normalize these forms to "foo.pack".
	 */
	if (has_extension(buf, ".idx")) {
		strcpy(buf + len - 4, ".pack");
		len++;
	} else if (!has_extension(buf, ".pack")) {
		strcpy(buf + len, ".pack");
		len += 5;
	}

	return xstrdup(buf);
}

static struct packed_git *open_pack(const char *path)
{
	char *packname = normalize_pack_name(path);
	int len = strlen(packname);
	struct packed_git *p;

	strcpy(packname + len - 5, ".idx");
	p = add_packed_git(packname, len - 1, 1);
	if (!p)
		die("packfile %s not found.", packname);

	install_packed_git(p);
	if (open_pack_index(p))
		die("packfile %s index not opened", p->pack_name);

	free(packname);
	return p;
}

static void process_one_pack(char *src_pack, char *dst_pack)
{
	struct packed_git *p;
	struct sha1file *f;
	struct pack_idx_entry *objs, **p_objs;
	struct pack_idx_option idx_opts;
	unsigned i, nr_objects;
	off_t written = 0;
	char *packname;
	unsigned char pack_sha1[20];
	struct progress *progress_state;

	p = open_pack(src_pack);
	if (!p)
		die("unable to open source pack");

	nr_objects = p->num_objects;
	objs = get_packed_object_list(p);
	p_objs = sort_objs_by_offset(objs, nr_objects);

	create_pack_dictionaries(p, p_objs);
	sort_dict_entries_by_hits(commit_ident_table);
	sort_dict_entries_by_hits(tree_path_table);

	packname = normalize_pack_name(dst_pack);
	f = packv4_open(packname);
	if (!f)
		die("unable to open destination pack");
	written += packv4_write_header(f, nr_objects);
	written += packv4_write_tables(f, nr_objects, objs);

	/* Let's write objects out, updating the object index list in place */
	progress_state = start_progress("Writing objects", nr_objects);
	all_objs = objs;
	all_objs_nr = nr_objects;
	for (i = 0; i < nr_objects; i++) {
		off_t obj_pos = written;
		struct pack_idx_entry *obj = p_objs[i];
		crc32_begin(f);
		written += packv4_write_object(f, p, obj);
		obj->offset = obj_pos;
		obj->crc32 = crc32_end(f);
		display_progress(progress_state, i+1);
	}
	stop_progress(&progress_state);

	sha1close(f, pack_sha1, CSUM_CLOSE | CSUM_FSYNC);

	reset_pack_idx_option(&idx_opts);
	idx_opts.version = 3;
	strcpy(packname + strlen(packname) - 5, ".idx");
	write_idx_file(packname, p_objs, nr_objects, &idx_opts, pack_sha1);

	free(packname);
}

static int git_pack_config(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "pack.compression")) {
		int level = git_config_int(k, v);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die("bad pack compression level %d", level);
		pack_compression_level = level;
		pack_compression_seen = 1;
		return 0;
	}
	return git_default_config(k, v, cb);
}

int main(int argc, char *argv[])
{
	char *src_pack, *dst_pack;

	if (argc == 3) {
		src_pack = argv[1];
		dst_pack = argv[2];
	} else if (argc == 4 && !prefixcmp(argv[1], "--min-tree-copy=")) {
		min_tree_copy = atoi(argv[1] + strlen("--min-tree-copy="));
		src_pack = argv[2];
		dst_pack = argv[3];
	} else {
		fprintf(stderr, "Usage: %s [--min-tree-copy=<n>] <src_packfile> <dst_packfile>\n", argv[0]);
		exit(1);
	}

	git_config(git_pack_config, NULL);
	if (!pack_compression_seen && core_compression_seen)
		pack_compression_level = core_compression_level;
	process_one_pack(src_pack, dst_pack);
	if (0)
		dict_dump();
	return 0;
}
