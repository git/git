#ifndef DELTA_ISLANDS_H
#define DELTA_ISLANDS_H

int island_delta_cmp(const struct object_id *a, const struct object_id *b);
int in_same_island(const struct object_id *, const struct object_id *);
void resolve_tree_islands(int progress, struct packing_data *to_pack);
void load_delta_islands(void);
void propagate_island_marks(struct commit *commit);
int compute_pack_layers(struct packing_data *to_pack);

#endif /* DELTA_ISLANDS_H */
