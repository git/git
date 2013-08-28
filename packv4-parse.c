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

struct packv4_dict {
	const unsigned char *data;
	unsigned int nb_entries;
	unsigned int offsets[FLEX_ARRAY];
};

static struct packv4_dict *load_dict(struct packed_git *p, off_t *offset)
{
	struct pack_window *w_curs = NULL;
	off_t curpos = *offset;
	unsigned long dict_size, avail;
	unsigned char *src, *data;
	const unsigned char *cp;
	git_zstream stream;
	struct packv4_dict *dict;
	int nb_entries, i, st;

	/* get uncompressed dictionary data size */
	src = use_pack(p, &w_curs, curpos, &avail);
	cp = src;
	dict_size = decode_varint(&cp);
	if (dict_size < 3) {
		error("bad dict size");
		return NULL;
	}
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

	/* count number of entries */
	nb_entries = 0;
	cp = data;
	while (cp < data + dict_size - 3) {
		cp += 2;  /* prefix bytes */
		cp += strlen((const char *)cp);  /* entry string */
		cp += 1;  /* terminating NUL */
		nb_entries++;
	}
	if (cp - data != dict_size) {
		error("dict size mismatch");
		free(data);
		return NULL;
	}

	dict = xmalloc(sizeof(*dict) + nb_entries * sizeof(dict->offsets[0]));
	dict->data = data;
	dict->nb_entries = nb_entries;

	cp = data;
	for (i = 0; i < nb_entries; i++) {
		dict->offsets[i] = cp - data;
		cp += 2;
		cp += strlen((const char *)cp) + 1;
	}

	*offset = curpos;
	return dict;
}
