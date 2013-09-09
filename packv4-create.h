#ifndef PACKV4_CREATE_H
#define PACKV4_CREATE_H

struct packv4_tables {
	struct pack_idx_entry *all_objs;
	unsigned all_objs_nr;
	struct dict_table *commit_ident_table;
	struct dict_table *tree_path_table;
};

#endif
