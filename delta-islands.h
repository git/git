#ifndef DELTA_ISLANDS_H
#define DELTA_ISLANDS_H

struct commit;
struct object_id;
struct packing_data;
struct repository;

int island_delta_cmp(const struct object_id *a, const struct object_id *b);
int in_same_island(const struct object_id *, const struct object_id *);
void resolve_tree_islands(struct repository *r,
			  int progress,
			  struct packing_data *to_pack);
void load_delta_islands(struct repository *r, int progress);
void propagate_island_marks(struct commit *commit);
int compute_pack_layers(struct packing_data *to_pack);
void free_island_marks(void);

#endif /* DELTA_ISLANDS_H */
