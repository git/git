#ifndef PACKV4_CREATE_H
#define PACKV4_CREATE_H

struct packv4_tables {
	struct pack_idx_entry *all_objs;
	unsigned all_objs_nr;
	struct dict_table *commit_ident_table;
	struct dict_table *tree_path_table;
};

struct dict_table;
struct sha1file;

struct dict_table *create_dict_table(void);
int dict_add_entry(struct dict_table *t, int val, const char *str, int str_len);
void destroy_dict_table(struct dict_table *t);
void dict_dump(struct packv4_tables *v4);

int add_commit_dict_entries(struct dict_table *commit_ident_table,
			    void *buf, unsigned long size);
int add_tree_dict_entries(struct dict_table *tree_path_table,
			  void *buf, unsigned long size);
void sort_dict_entries_by_hits(struct dict_table *t);

int encode_sha1ref(const struct packv4_tables *v4,
		   const unsigned char *sha1, unsigned char *buf);
unsigned long packv4_write_tables(struct sha1file *f,
				  const struct packv4_tables *v4,
				  int pack_compression_level);
void *pv4_encode_commit(const struct packv4_tables *v4,
			void *buffer, unsigned long *sizep,
			int pack_compression_level);
void *pv4_encode_tree(const struct packv4_tables *v4,
		      void *_buffer, unsigned long *sizep,
		      void *delta, unsigned long delta_size,
		      const unsigned char *delta_sha1);

void process_one_pack(struct packv4_tables *v4,
		      char *src_pack, char *dst_pack);

#endif
