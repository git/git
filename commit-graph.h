#ifndef COMMIT_GRAPH_H
#define COMMIT_GRAPH_H

#include "object-store.h"
#include "oidset.h"

#define GIT_TEST_COMMIT_GRAPH "GIT_TEST_COMMIT_GRAPH"
#define GIT_TEST_COMMIT_GRAPH_DIE_ON_PARSE "GIT_TEST_COMMIT_GRAPH_DIE_ON_PARSE"
#define GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS "GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS"

/*
 * This method is only used to enhance coverage of the commit-graph
 * feature in the test suite with the GIT_TEST_COMMIT_GRAPH and
 * GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS environment variables. Do not
 * call this method oustide of a builtin, and only if you know what
 * you are doing!
 */
void git_test_write_commit_graph_or_die(void);

struct commit;
struct bloom_filter_settings;
struct repository;
struct raw_object_store;
struct string_list;

char *get_commit_graph_filename(struct object_directory *odb);
char *get_commit_graph_chain_filename(struct object_directory *odb);
int open_commit_graph(const char *graph_file, int *fd, struct stat *st);

/*
 * Given a commit struct, try to fill the commit struct info, including:
 *  1. tree object
 *  2. date
 *  3. parents.
 *
 * Returns 1 if and only if the commit was found in the packed graph.
 *
 * See parse_commit_buffer() for the fallback after this call.
 */
int parse_commit_in_graph(struct repository *r, struct commit *item);

/*
 * Fills `*pos` with the graph position of `c`, and returns 1 if `c` is
 * found in the commit-graph belonging to `r`, or 0 otherwise.
 * Initializes the commit-graph belonging to `r` if it hasn't been
 * already.
 *
 * Note: this is a low-level helper that does not alter any slab data
 * associated with `c`. Useful in circumstances where the slab data is
 * already being modified (e.g., writing the commit-graph itself).
 *
 * In most cases, callers should use `parse_commit_in_graph()` instead.
 */
int repo_find_commit_pos_in_graph(struct repository *r, struct commit *c,
				  uint32_t *pos);

/*
 * Look up the given commit ID in the commit-graph. This will only return a
 * commit if the ID exists both in the graph and in the object database such
 * that we don't return commits whose object has been pruned. Otherwise, this
 * function returns `NULL`.
 */
struct commit *lookup_commit_in_graph(struct repository *repo, const struct object_id *id);

/*
 * It is possible that we loaded commit contents from the commit buffer,
 * but we also want to ensure the commit-graph content is correctly
 * checked and filled. Fill the graph_pos and generation members of
 * the given commit.
 */
void load_commit_graph_info(struct repository *r, struct commit *item);

struct tree *get_commit_tree_in_graph(struct repository *r,
				      const struct commit *c);

struct commit_graph {
	const unsigned char *data;
	size_t data_len;

	unsigned char hash_len;
	unsigned char num_chunks;
	uint32_t num_commits;
	struct object_id oid;
	char *filename;
	struct object_directory *odb;

	uint32_t num_commits_in_base;
	unsigned int read_generation_data;
	struct commit_graph *base_graph;

	const uint32_t *chunk_oid_fanout;
	const unsigned char *chunk_oid_lookup;
	const unsigned char *chunk_commit_data;
	const unsigned char *chunk_generation_data;
	const unsigned char *chunk_generation_data_overflow;
	const unsigned char *chunk_extra_edges;
	const unsigned char *chunk_base_graphs;
	const unsigned char *chunk_bloom_indexes;
	const unsigned char *chunk_bloom_data;

	struct topo_level_slab *topo_levels;
	struct bloom_filter_settings *bloom_filter_settings;
};

struct commit_graph *load_commit_graph_one_fd_st(struct repository *r,
						 int fd, struct stat *st,
						 struct object_directory *odb);
struct commit_graph *read_commit_graph_one(struct repository *r,
					   struct object_directory *odb);

/*
 * Callers should initialize the repo_settings with prepare_repo_settings()
 * prior to calling parse_commit_graph().
 */
struct commit_graph *parse_commit_graph(struct repo_settings *s,
					void *graph_map, size_t graph_size);

/*
 * Return 1 if and only if the repository has a commit-graph
 * file and generation numbers are computed in that file.
 */
int generation_numbers_enabled(struct repository *r);

/*
 * Return 1 if and only if the repository has a commit-graph
 * file and generation data chunk has been written for the file.
 */
int corrected_commit_dates_enabled(struct repository *r);

struct bloom_filter_settings *get_bloom_filter_settings(struct repository *r);

enum commit_graph_write_flags {
	COMMIT_GRAPH_WRITE_APPEND     = (1 << 0),
	COMMIT_GRAPH_WRITE_PROGRESS   = (1 << 1),
	COMMIT_GRAPH_WRITE_SPLIT      = (1 << 2),
	COMMIT_GRAPH_WRITE_BLOOM_FILTERS = (1 << 3),
	COMMIT_GRAPH_NO_WRITE_BLOOM_FILTERS = (1 << 4),
};

enum commit_graph_split_flags {
	COMMIT_GRAPH_SPLIT_UNSPECIFIED      = 0,
	COMMIT_GRAPH_SPLIT_MERGE_PROHIBITED = 1,
	COMMIT_GRAPH_SPLIT_REPLACE          = 2
};

struct commit_graph_opts {
	int size_multiple;
	int max_commits;
	timestamp_t expire_time;
	enum commit_graph_split_flags split_flags;
	int max_new_filters;
};

/*
 * The write_commit_graph* methods return zero on success
 * and a negative value on failure. Note that if the repository
 * is not compatible with the commit-graph feature, then the
 * methods will return 0 without writing a commit-graph.
 */
int write_commit_graph_reachable(struct object_directory *odb,
				 enum commit_graph_write_flags flags,
				 const struct commit_graph_opts *opts);
int write_commit_graph(struct object_directory *odb,
		       const struct string_list *pack_indexes,
		       struct oidset *commits,
		       enum commit_graph_write_flags flags,
		       const struct commit_graph_opts *opts);

#define COMMIT_GRAPH_VERIFY_SHALLOW	(1 << 0)

int verify_commit_graph(struct repository *r, struct commit_graph *g, int flags);

void close_commit_graph(struct raw_object_store *);
void free_commit_graph(struct commit_graph *);

/*
 * Disable further use of the commit graph in this process when parsing a
 * "struct commit".
 */
void disable_commit_graph(struct repository *r);

struct commit_graph_data {
	uint32_t graph_pos;
	timestamp_t generation;
};

/*
 * Commits should be parsed before accessing generation, graph positions.
 */
timestamp_t commit_graph_generation(const struct commit *);
uint32_t commit_graph_position(const struct commit *);

/*
 * After this method, all commits reachable from those in the given
 * list will have non-zero, non-infinite generation numbers.
 */
void ensure_generations_valid(struct repository *r,
			      struct commit **commits, size_t nr);

#endif
