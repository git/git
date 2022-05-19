#ifndef CUMMIT_GRAPH_H
#define CUMMIT_GRAPH_H

#include "but-compat-util.h"
#include "object-store.h"
#include "oidset.h"

#define GIT_TEST_CUMMIT_GRAPH "GIT_TEST_CUMMIT_GRAPH"
#define GIT_TEST_CUMMIT_GRAPH_DIE_ON_PARSE "GIT_TEST_CUMMIT_GRAPH_DIE_ON_PARSE"
#define GIT_TEST_CUMMIT_GRAPH_CHANGED_PATHS "GIT_TEST_CUMMIT_GRAPH_CHANGED_PATHS"

/*
 * This method is only used to enhance coverage of the cummit-graph
 * feature in the test suite with the GIT_TEST_CUMMIT_GRAPH and
 * GIT_TEST_CUMMIT_GRAPH_CHANGED_PATHS environment variables. Do not
 * call this method oustide of a builtin, and only if you know what
 * you are doing!
 */
void but_test_write_cummit_graph_or_die(void);

struct cummit;
struct bloom_filter_settings;
struct repository;
struct raw_object_store;
struct string_list;

char *get_cummit_graph_filename(struct object_directory *odb);
char *get_cummit_graph_chain_filename(struct object_directory *odb);
int open_cummit_graph(const char *graph_file, int *fd, struct stat *st);

/*
 * Given a cummit struct, try to fill the cummit struct info, including:
 *  1. tree object
 *  2. date
 *  3. parents.
 *
 * Returns 1 if and only if the cummit was found in the packed graph.
 *
 * See parse_cummit_buffer() for the fallback after this call.
 */
int parse_cummit_in_graph(struct repository *r, struct cummit *item);

/*
 * Look up the given cummit ID in the cummit-graph. This will only return a
 * cummit if the ID exists both in the graph and in the object database such
 * that we don't return cummits whose object has been pruned. Otherwise, this
 * function returns `NULL`.
 */
struct cummit *lookup_cummit_in_graph(struct repository *repo, const struct object_id *id);

/*
 * It is possible that we loaded cummit contents from the cummit buffer,
 * but we also want to ensure the cummit-graph content is correctly
 * checked and filled. Fill the graph_pos and generation members of
 * the given cummit.
 */
void load_cummit_graph_info(struct repository *r, struct cummit *item);

struct tree *get_cummit_tree_in_graph(struct repository *r,
				      const struct cummit *c);

struct cummit_graph {
	const unsigned char *data;
	size_t data_len;

	unsigned char hash_len;
	unsigned char num_chunks;
	uint32_t num_cummits;
	struct object_id oid;
	char *filename;
	struct object_directory *odb;

	uint32_t num_cummits_in_base;
	unsigned int read_generation_data;
	struct cummit_graph *base_graph;

	const uint32_t *chunk_oid_fanout;
	const unsigned char *chunk_oid_lookup;
	const unsigned char *chunk_cummit_data;
	const unsigned char *chunk_generation_data;
	const unsigned char *chunk_generation_data_overflow;
	const unsigned char *chunk_extra_edges;
	const unsigned char *chunk_base_graphs;
	const unsigned char *chunk_bloom_indexes;
	const unsigned char *chunk_bloom_data;

	struct topo_level_slab *topo_levels;
	struct bloom_filter_settings *bloom_filter_settings;
};

struct cummit_graph *load_cummit_graph_one_fd_st(struct repository *r,
						 int fd, struct stat *st,
						 struct object_directory *odb);
struct cummit_graph *read_cummit_graph_one(struct repository *r,
					   struct object_directory *odb);
struct cummit_graph *parse_cummit_graph(struct repository *r,
					void *graph_map, size_t graph_size);

/*
 * Return 1 if and only if the repository has a cummit-graph
 * file and generation numbers are computed in that file.
 */
int generation_numbers_enabled(struct repository *r);

/*
 * Return 1 if and only if the repository has a cummit-graph
 * file and generation data chunk has been written for the file.
 */
int corrected_cummit_dates_enabled(struct repository *r);

struct bloom_filter_settings *get_bloom_filter_settings(struct repository *r);

enum cummit_graph_write_flags {
	CUMMIT_GRAPH_WRITE_APPEND     = (1 << 0),
	CUMMIT_GRAPH_WRITE_PROGRESS   = (1 << 1),
	CUMMIT_GRAPH_WRITE_SPLIT      = (1 << 2),
	CUMMIT_GRAPH_WRITE_BLOOM_FILTERS = (1 << 3),
	CUMMIT_GRAPH_NO_WRITE_BLOOM_FILTERS = (1 << 4),
};

enum cummit_graph_split_flags {
	CUMMIT_GRAPH_SPLIT_UNSPECIFIED      = 0,
	CUMMIT_GRAPH_SPLIT_MERGE_PROHIBITED = 1,
	CUMMIT_GRAPH_SPLIT_REPLACE          = 2
};

struct cummit_graph_opts {
	int size_multiple;
	int max_cummits;
	timestamp_t expire_time;
	enum cummit_graph_split_flags split_flags;
	int max_new_filters;
};

/*
 * The write_cummit_graph* methods return zero on success
 * and a negative value on failure. Note that if the repository
 * is not compatible with the cummit-graph feature, then the
 * methods will return 0 without writing a cummit-graph.
 */
int write_cummit_graph_reachable(struct object_directory *odb,
				 enum cummit_graph_write_flags flags,
				 const struct cummit_graph_opts *opts);
int write_cummit_graph(struct object_directory *odb,
		       const struct string_list *pack_indexes,
		       struct oidset *cummits,
		       enum cummit_graph_write_flags flags,
		       const struct cummit_graph_opts *opts);

#define CUMMIT_GRAPH_VERIFY_SHALLOW	(1 << 0)

int verify_cummit_graph(struct repository *r, struct cummit_graph *g, int flags);

void close_cummit_graph(struct raw_object_store *);
void free_cummit_graph(struct cummit_graph *);

/*
 * Disable further use of the cummit graph in this process when parsing a
 * "struct cummit".
 */
void disable_cummit_graph(struct repository *r);

struct cummit_graph_data {
	uint32_t graph_pos;
	timestamp_t generation;
};

/*
 * cummits should be parsed before accessing generation, graph positions.
 */
timestamp_t cummit_graph_generation(const struct cummit *);
uint32_t cummit_graph_position(const struct cummit *);
#endif
