#ifndef PACKV4_PARSE_H
#define PACKV4_PARSE_H

struct packv4_dict {
	const unsigned char *data;
	unsigned int nb_entries;
	unsigned int offsets[FLEX_ARRAY];
};

struct packv4_dict *pv4_create_dict(const unsigned char *data, int dict_size);
void pv4_free_dict(struct packv4_dict *dict);

unsigned long pv4_unpack_object_header_buffer(const unsigned char *base,
					      unsigned long len,
					      enum object_type *type,
					      unsigned long *sizep);
const unsigned char *get_sha1ref(struct packed_git *p,
				 const unsigned char **bufp);

void *pv4_get_commit(struct packed_git *p, struct pack_window **w_curs,
		     off_t offset, unsigned long size);
void *pv4_get_tree(struct packed_git *p, struct pack_window **w_curs,
		   off_t obj_offset, unsigned long size);

#endif
