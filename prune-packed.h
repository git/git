#ifndef PRUNE_PACKED_H
#define PRUNE_PACKED_H

struct repository;

#define PRUNE_PACKED_DRY_RUN 01
#define PRUNE_PACKED_VERBOSE 02

void prune_packed_objects(struct repository *r, int opts);

#endif
