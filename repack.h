#ifndef REPACK_H
#define REPACK_H

#include "list-objects-filter-options.h"

struct pack_objects_args {
	char *window;
	char *window_memory;
	char *depth;
	char *threads;
	unsigned long max_pack_size;
	int no_reuse_delta;
	int no_reuse_object;
	int quiet;
	int local;
	int name_hash_version;
	int path_walk;
	struct list_objects_filter_options filter_options;
};

void pack_objects_args_release(struct pack_objects_args *args);

#endif /* REPACK_H */
