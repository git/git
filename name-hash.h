#ifndef NAME_HASH_H
#define NAME_HASH_H

struct cache_entry;
struct index_state;

int index_dir_exists(struct index_state *istate, const char *name, int namelen);
void adjust_dirname_case(struct index_state *istate, char *name);
struct cache_entry *index_file_exists(struct index_state *istate, const char *name, int namelen, int igncase);

int test_lazy_init_name_hash(struct index_state *istate, int try_threaded);
void add_name_hash(struct index_state *istate, struct cache_entry *ce);
void remove_name_hash(struct index_state *istate, struct cache_entry *ce);
void free_name_hash(struct index_state *istate);

#endif /* NAME_HASH_H */
