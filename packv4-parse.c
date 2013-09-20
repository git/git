/*
 * Code to parse pack v4 object encoding
 *
 * (C) Nicolas Pitre <nico@fluxnic.net>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "cache.h"
#include "packv4-parse.h"
#include "tree-walk.h"
#include "varint.h"

const unsigned char *get_sha1ref(struct packed_git *p,
				 const unsigned char **bufp)
{
	const unsigned char *sha1;

	if (!**bufp) {
		sha1 = *bufp + 1;
		*bufp += 21;
	} else {
		unsigned int index = decode_varint(bufp);
		if (index < 1 || index - 1 > p->num_objects)
			die("bad index in %s", __func__);
		sha1 = p->sha1_table + (index - 1) * 20;
	}

	return sha1;
}

struct packv4_dict *pv4_create_dict(const unsigned char *data, int dict_size)
{
	struct packv4_dict *dict;
	int i;

	/* count number of entries */
	int nb_entries = 0;
	const unsigned char *cp = data;
	while (cp < data + dict_size - 3) {
		cp += 2;  /* prefix bytes */
		cp += strlen((const char *)cp);  /* entry string */
		cp += 1;  /* terminating NUL */
		nb_entries++;
	}
	if (cp - data != dict_size) {
		error("dict size mismatch");
		return NULL;
	}

	dict = xmalloc(sizeof(*dict) +
		       (nb_entries + 1) * sizeof(dict->offsets[0]));
	dict->data = data;
	dict->nb_entries = nb_entries;

	dict->offsets[0] = 0;
	cp = data;
	for (i = 0; i < nb_entries; i++) {
		cp += 2;
		cp += strlen((const char *)cp) + 1;
		dict->offsets[i + 1] = cp - data;
	}

	return dict;
}

void pv4_free_dict(struct packv4_dict *dict)
{
	if (dict) {
		free((void*)dict->data);
		free(dict);
	}
}

static struct packv4_dict *load_dict(struct packed_git *p, off_t *offset)
{
	struct pack_window *w_curs = NULL;
	off_t curpos = *offset;
	unsigned long dict_size, avail;
	unsigned char *src, *data;
	const unsigned char *cp;
	git_zstream stream;
	struct packv4_dict *dict;
	int st;

	/* get uncompressed dictionary data size */
	src = use_pack(p, &w_curs, curpos, &avail);
	cp = src;
	dict_size = decode_varint(&cp);
	curpos += cp - src;

	data = xmallocz(dict_size);
	memset(&stream, 0, sizeof(stream));
	stream.next_out = data;
	stream.avail_out = dict_size + 1;

	git_inflate_init(&stream);
	do {
		src = use_pack(p, &w_curs, curpos, &stream.avail_in);
		stream.next_in = src;
		st = git_inflate(&stream, Z_FINISH);
		curpos += stream.next_in - src;
	} while ((st == Z_OK || st == Z_BUF_ERROR) && stream.avail_out);
	git_inflate_end(&stream);
	unuse_pack(&w_curs);
	if (st != Z_STREAM_END || stream.total_out != dict_size) {
		error("pack dictionary bad");
		free(data);
		return NULL;
	}

	dict = pv4_create_dict(data, dict_size);
	if (!dict) {
		free(data);
		return NULL;
	}

	*offset = curpos;
	return dict;
}

static void load_ident_dict(struct packed_git *p)
{
	off_t offset = 12 + p->num_objects * 20;
	struct packv4_dict *names = load_dict(p, &offset);
	if (!names)
		die("bad pack name dictionary in %s", p->pack_name);
	p->ident_dict = names;
	p->ident_dict_end = offset;
}

const unsigned char *get_identref(struct packed_git *p, const unsigned char **srcp)
{
	unsigned int index;

	if (!p->ident_dict)
		load_ident_dict(p);

	index = decode_varint(srcp);
	if (index >= p->ident_dict->nb_entries) {
		error("%s: index overflow", __func__);
		return NULL;
	}
	return p->ident_dict->data + p->ident_dict->offsets[index];
}

static void load_path_dict(struct packed_git *p)
{
	off_t offset;
	struct packv4_dict *paths;

	/*
	 * For now we need to load the name dictionary to know where
	 * it ends and therefore where the path dictionary starts.
	 */
	if (!p->ident_dict)
		load_ident_dict(p);

	offset = p->ident_dict_end;
	paths = load_dict(p, &offset);
	if (!paths)
		die("bad pack path dictionary in %s", p->pack_name);
	p->path_dict = paths;
}

const unsigned char *get_pathref(struct packed_git *p, unsigned int index,
				 int *len)
{
	if (!p->path_dict)
		load_path_dict(p);

	if (index >= p->path_dict->nb_entries) {
		error("%s: index overflow", __func__);
		return NULL;
	}
	if (len)
		*len = p->path_dict->offsets[index + 1] -
			p->path_dict->offsets[index];
	return p->path_dict->data + p->path_dict->offsets[index];
}

static int tree_line(unsigned char *buf, unsigned long size,
		     const char *label, int label_len,
		     const unsigned char *sha1)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	if (label_len + 1 + 40 + 1 > size)
		return 0;

	memcpy(buf, label, label_len);
	buf += label_len;
	*buf++ = ' ';

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}

	*buf = '\n';

	return label_len + 1 + 40 + 1;
}

static int ident_line(unsigned char *buf, unsigned long size,
		      const char *label, int label_len,
		      const unsigned char *ident, unsigned long time, int tz)
{
	int ident_len = strlen((const char *)ident);
	int len = label_len + 1 + ident_len + 1 + 1 + 5 + 1;
	int time_len = 0;
	unsigned char time_buf[16];

	do {
		time_buf[time_len++] = '0' + time % 10;
		time /= 10;
	} while (time);
	len += time_len;

	if (len > size)
		return 0;

	memcpy(buf, label, label_len);
	buf += label_len;
	*buf++ = ' ';

	memcpy(buf, ident, ident_len);
	buf += ident_len;
	*buf++ = ' ';

	do {
		*buf++ = time_buf[--time_len];
	} while (time_len);
	*buf++ = ' ';

	if (tz < 0) {
		tz = -tz;
		*buf++ = '-';
	} else
		*buf++ = '+';
	*buf++ = '0' + tz / 1000; tz %= 1000;
	*buf++ = '0' + tz / 100;  tz %= 100;
	*buf++ = '0' + tz / 10;   tz %= 10;
	*buf++ = '0' + tz;

	*buf = '\n';

	return len;
}

void *pv4_get_commit(struct packed_git *p, struct pack_window **w_curs,
		     off_t offset, unsigned long size)
{
	unsigned long avail;
	git_zstream stream;
	int len, st;
	unsigned int nb_parents;
	unsigned char *dst, *dcp;
	const unsigned char *src, *scp, *sha1, *ident, *author, *committer;
	unsigned long author_time, commit_time;
	int16_t author_tz, commit_tz;

	dst = xmallocz(size);
	dcp = dst;

	src = use_pack(p, w_curs, offset, &avail);
	scp = src;

	sha1 = get_sha1ref(p, &scp);
	len = tree_line(dcp, size, "tree", strlen("tree"), sha1);
	if (!len)
		die("overflow in %s", __func__);
	dcp += len;
	size -= len;

	nb_parents = decode_varint(&scp);
	while (nb_parents--) {
		sha1 = get_sha1ref(p, &scp);
		len = tree_line(dcp, size, "parent", strlen("parent"), sha1);
		if (!len)
			die("overflow in %s", __func__);
		dcp += len;
		size -= len;
	}

	commit_time = decode_varint(&scp);
	ident = get_identref(p, &scp);
	commit_tz = (ident[0] << 8) | ident[1];
	committer = &ident[2];

	author_time = decode_varint(&scp);
	ident = get_identref(p, &scp);
	author_tz = (ident[0] << 8) | ident[1];
	author = &ident[2];

	if (author_time & 1)
		author_time = commit_time + (author_time >> 1);
	else
		author_time = commit_time - (author_time >> 1);

	len = ident_line(dcp, size, "author", strlen("author"),
			 author, author_time, author_tz);
	if (!len)
		die("overflow in %s", __func__);
	dcp += len;
	size -= len;

	len = ident_line(dcp, size, "committer", strlen("committer"),
			 committer, commit_time, commit_tz);
	if (!len)
		die("overflow in %s", __func__);
	dcp += len;
	size -= len;

	if (scp - src > avail)
		die("overflow in %s", __func__);
	offset += scp - src;

	memset(&stream, 0, sizeof(stream));
	stream.next_out = dcp;
	stream.avail_out = size + 1;
	git_inflate_init(&stream);
	do {
		src = use_pack(p, w_curs, offset, &stream.avail_in);
		stream.next_in = (unsigned char *)src;
		st = git_inflate(&stream, Z_FINISH);
		offset += stream.next_in - src;
	} while ((st == Z_OK || st == Z_BUF_ERROR) && stream.avail_out);
	git_inflate_end(&stream);
	if (st != Z_STREAM_END || stream.total_out != size) {
		free(dst);
		return NULL;
	}

	return dst;
}

static int copy_canonical_tree_entries(struct packed_git *p, off_t offset,
				       unsigned int start, unsigned int count,
				       unsigned char **dstp, unsigned long *sizep)
{
	void *data;
	const unsigned char *from, *end;
	enum object_type type;
	unsigned long size;
	struct tree_desc desc;

	data = unpack_entry(p, offset, &type, &size);
	if (!data)
		return -1;
	if (type != OBJ_TREE) {
		free(data);
		return -1;
	}

	init_tree_desc(&desc, data, size);

	while (start--)
		update_tree_entry(&desc);

	from = desc.buffer;
	while (count--)
		update_tree_entry(&desc);
	end = desc.buffer;

	if (end - from > *sizep) {
		free(data);
		return -1;
	}
	memcpy(*dstp, from, end - from);
	*dstp += end - from;
	*sizep -= end - from;
	free(data);
	return 0;
}

/* ordering is so that member alignment takes the least amount of space */
struct pv4_tree_cache {
	off_t base_offset;
	off_t offset;
	off_t last_copy_base;
	struct packed_git *p;
	unsigned int pos;
	unsigned int nb_entries;
};

#define CACHE_SIZE 1024
static struct pv4_tree_cache pv4_tree_cache[CACHE_SIZE];

static struct pv4_tree_cache *get_tree_offset_cache(struct packed_git *p, off_t base_offset)
{
	struct pv4_tree_cache *c;
	unsigned long hash;

	hash = (unsigned long)p + (unsigned long)base_offset;
	hash += (hash >> 8) + (hash >> 16);
	hash %= CACHE_SIZE;

	c = &pv4_tree_cache[hash];
	if (c->p != p || c->base_offset != base_offset) {
		c->p = p;
		c->base_offset = base_offset;
		c->offset = 0;
		c->last_copy_base = 0;
		c->pos = 0;
		c->nb_entries = 0;
	}
	return c;
}

static int tree_entry_prefix(unsigned char *buf, unsigned long size,
			     const unsigned char *path, int path_len,
			     unsigned mode)
{
	int mode_len = 0;
	int len;
	unsigned char mode_buf[8];

	do {
		mode_buf[mode_len++] = '0' + (mode & 7);
		mode >>= 3;
	} while (mode);

	len = mode_len + 1 + path_len;
	if (len > size)
		return 0;

	do {
		*buf++ = mode_buf[--mode_len];
	} while (mode_len);
	*buf++ = ' ';
	memcpy(buf, path, path_len);

	return len;
}

static int decode_entries(struct packed_git *p, struct pack_window **w_curs,
			  off_t obj_offset, unsigned int start, unsigned int count,
			  unsigned char **dstp, unsigned long *sizep)
{
	unsigned long avail;
	const unsigned char *src, *scp;
	unsigned int curpos;
	off_t offset, copy_objoffset;
	struct pv4_tree_cache *c;

	c = get_tree_offset_cache(p, obj_offset);
	if (count && start < c->nb_entries && start >= c->pos &&
	    count <= c->nb_entries - start) {
		offset = c->offset;
		copy_objoffset = c->last_copy_base;
		curpos = c->pos;
		start -= curpos;
		src = NULL;
		avail = 0;
	} else {
		unsigned int nb_entries;

		src = use_pack(p, w_curs, obj_offset, &avail);
		scp = src;

		/* we need to skip over the object header */
		while (*scp & 128)
			if (++scp - src >= avail - 20)
				return -1;

		/* is this a canonical tree object? */
		if ((*scp & 0xf) == OBJ_TREE) {
			offset = obj_offset + (scp - src);
			return copy_canonical_tree_entries(p, offset,
							   start, count,
							   dstp, sizep);
		}

		/* let's still make sure this is actually a pv4 tree */
		if ((*scp++ & 0xf) != OBJ_PV4_TREE)
			return -1;

		nb_entries = decode_varint(&scp);
		if (!count)
			count = nb_entries;
		if (!nb_entries || start > nb_entries ||
		    count > nb_entries - start)
			return -1;

		curpos = 0;
		copy_objoffset = 0;
		offset = obj_offset + (scp - src);
		avail -= scp - src;
		src = scp;

		/*
		 * If this is a partial copy, let's (re)initialize a cache
		 * entry to speed things up if the remaining of this tree
		 * is needed in the future.
		 */
		if (start + count < nb_entries) {
			c->offset = offset;
			c->pos = 0;
			c->nb_entries = nb_entries;
			c->last_copy_base = 0;
		}
	}

	while (count) {
		unsigned int what;

		if (avail < 20) {
			src = use_pack(p, w_curs, offset, &avail);
			if (avail < 20)
				return -1;
		}
		scp = src;

		what = decode_varint(&scp);
		if (scp == src)
			return -1;

		if (!(what & 1) && start != 0) {
			/*
			 * This is a single entry and we have to skip it.
			 * The path index was parsed and is in 'what'.
			 * Skip over the SHA1 index.
			 */
			if (!*scp)
				scp += 1 + 20;
			else
				while (*scp++ & 128);
			start--;
			curpos++;
		} else if (!(what & 1) && start == 0) {
			/*
			 * This is an actual tree entry to recreate.
			 */
			const unsigned char *path, *sha1;
			unsigned mode;
			int len, pathlen;

			path = get_pathref(p, what >> 1, &pathlen);
			sha1 = get_sha1ref(p, &scp);
			if (!path || !sha1)
				return -1;
			mode = (path[0] << 8) | path[1];
			len = tree_entry_prefix(*dstp, *sizep,
						path + 2, pathlen - 2, mode);
			if (!len || len + 20 > *sizep)
				return -1;
			hashcpy(*dstp + len, sha1);
			*dstp += len + 20;
			*sizep -= len + 20;
			count--;
			curpos++;
		} else if (what & 1) {
			/*
			 * Copy from another tree object.
			 */
			unsigned int copy_start, copy_count;

			copy_start = what >> 1;
			copy_count = decode_varint(&scp);
			if (!copy_count)
				return -1;

			/*
			 * The LSB of copy_count is a flag indicating if
			 * a third value is provided to specify the source
			 * object.  This may be omitted when it doesn't
			 * change, but has to be specified at least for the
			 * first copy sequence.
			 */
			if (copy_count & 1) {
				unsigned index = decode_varint(&scp);
				if (!index) {
					/*
					 * SHA1 follows. We assume the
					 * object is in the same pack.
					 */
					copy_objoffset =
						find_pack_entry_one(scp, p);
					scp += 20;
				} else {
					/*
					 * From the SHA1 index we can get
					 * the object offset directly.
					 */
					copy_objoffset =
						nth_packed_object_offset(p, index - 1);
				}
			}
			copy_count >>= 1;
			if (!copy_count || !copy_objoffset)
				return -1;

			if (start >= copy_count) {
				start -= copy_count;
				curpos += copy_count;
			} else {
				int ret;

				copy_count -= start;
				copy_start += start;
				if (copy_count > count) {
					/*
					 * We won't consume the whole of
					 * this copy sequence and the main
					 * loop will be exited. Let's manage
					 * for offset and curpos to remain
					 * unchanged to update the cache.
					 */
					copy_count = count;
					count = 0;
					scp = src;
				} else {
					count -= copy_count;
					curpos += start + copy_count;
					start = 0;
				}

				ret = decode_entries(p, w_curs, copy_objoffset,
						     copy_start, copy_count,
						     dstp, sizep);
				if (ret)
					return ret;

				/* force pack window readjustment */
				avail = scp - src;
			}
		}

		offset += scp - src;
		avail -= scp - src;
		src = scp;
	}

	/*
	 * Update the cache if we didn't run through the entire tree.
	 * We have to "get" it again as a recursion into decode_entries()
	 * could have invalidated what we obtained initially.
	 */
	c = get_tree_offset_cache(p, obj_offset);
	if (curpos < c->nb_entries) {
		c->pos = curpos;
		c->offset = offset;
		c->last_copy_base = copy_objoffset;
	}
						
	return 0;
}

void *pv4_get_tree(struct packed_git *p, struct pack_window **w_curs,
		   off_t obj_offset, unsigned long size)
{
	unsigned char *dst, *dcp;
	int ret;

	dst = xmallocz(size);
	dcp = dst;
	ret = decode_entries(p, w_curs, obj_offset, 0, 0, &dcp, &size);
	if (ret < 0 || size != 0) {
		free(dst);
		return NULL;
	}
	return dst;
}

unsigned long pv4_unpack_object_header_buffer(const unsigned char *base,
					      unsigned long len,
					      enum object_type *type,
					      unsigned long *sizep)
{
	const unsigned char *cp = base;
	uintmax_t val = decode_varint(&cp);
	*type = val & 0xf;
	*sizep = val >> 4;
	return cp - base;
}
