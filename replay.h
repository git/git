#ifndef REPLAY_H
#define REPLAY_H

#include "khash.h"
#include "merge-ort.h"
#include "repository.h"

struct commit;
struct tree;

struct commit *replay_create_commit(struct repository *repo,
				    struct tree *tree,
				    struct commit *based_on,
				    struct commit *parent);

struct commit *replay_pick_regular_commit(struct repository *repo,
					  struct commit *pickme,
					  kh_oid_map_t *replayed_commits,
					  struct commit *onto,
					  struct merge_options *merge_opt,
					  struct merge_result *result);

#endif
