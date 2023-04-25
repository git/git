#include "git-compat-util.h"
#include "config.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "pack.h"
#include "packfile.h"
#include "commit.h"
#include "object.h"
#include "refs.h"
#include "revision.h"
#include "hash-lookup.h"
#include "commit-graph.h"
#include "object-file.h"
#include "object-store.h"
#include "oid-array.h"
#include "alloc.h"
#include "hashmap.h"
#include "replace-object.h"
#include "progress.h"
#include "bloom.h"
#include "commit-slab.h"
#include "shallow.h"
#include "json-writer.h"
#include "trace2.h"
#include "chunk-format.h"
#include "wrapper.h"

void git_test_write_commit_graph_or_die(void)
{
	int flags = 0;
	if (!git_env_bool(GIT_TEST_COMMIT_GRAPH, 0))
		return;

	if (git_env_bool(GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS, 0))
		flags = COMMIT_GRAPH_WRITE_BLOOM_FILTERS;

	if (write_commit_graph_reachable(the_repository->objects->odb,
					 flags, NULL))
		die("failed to write commit-graph under GIT_TEST_COMMIT_GRAPH");
}

#define GRAPH_SIGNATURE 0x43475048 /* "CGPH" */
#define GRAPH_CHUNKID_OIDFANOUT 0x4f494446 /* "OIDF" */
#define GRAPH_CHUNKID_OIDLOOKUP 0x4f49444c /* "OIDL" */
#define GRAPH_CHUNKID_DATA 0x43444154 /* "CDAT" */
#define GRAPH_CHUNKID_GENERATION_DATA 0x47444132 /* "GDA2" */
#define GRAPH_CHUNKID_GENERATION_DATA_OVERFLOW 0x47444f32 /* "GDO2" */
#define GRAPH_CHUNKID_EXTRAEDGES 0x45444745 /* "EDGE" */
#define GRAPH_CHUNKID_BLOOMINDEXES 0x42494458 /* "BIDX" */
#define GRAPH_CHUNKID_BLOOMDATA 0x42444154 /* "BDAT" */
#define GRAPH_CHUNKID_BASE 0x42415345 /* "BASE" */

#define GRAPH_DATA_WIDTH (the_hash_algo->rawsz + 16)

#define GRAPH_VERSION_1 0x1
#define GRAPH_VERSION GRAPH_VERSION_1

#define GRAPH_EXTRA_EDGES_NEEDED 0x80000000
#define GRAPH_EDGE_LAST_MASK 0x7fffffff
#define GRAPH_PARENT_NONE 0x70000000

#define GRAPH_LAST_EDGE 0x80000000

#define GRAPH_HEADER_SIZE 8
#define GRAPH_FANOUT_SIZE (4 * 256)
#define GRAPH_MIN_SIZE (GRAPH_HEADER_SIZE + 4 * CHUNK_TOC_ENTRY_SIZE \
			+ GRAPH_FANOUT_SIZE + the_hash_algo->rawsz)

#define CORRECTED_COMMIT_DATE_OFFSET_OVERFLOW (1ULL << 31)

/* Remember to update object flag allocation in object.h */
#define REACHABLE       (1u<<15)

define_commit_slab(topo_level_slab, uint32_t);

/* Keep track of the order in which commits are added to our list. */
define_commit_slab(commit_pos, int);
static struct commit_pos commit_pos = COMMIT_SLAB_INIT(1, commit_pos);

static void set_commit_pos(struct repository *r, const struct object_id *oid)
{
	static int32_t max_pos;
	struct commit *commit = lookup_commit(r, oid);

	if (!commit)
		return; /* should never happen, but be lenient */

	*commit_pos_at(&commit_pos, commit) = max_pos++;
}

static int commit_pos_cmp(const void *va, const void *vb)
{
	const struct commit *a = *(const struct commit **)va;
	const struct commit *b = *(const struct commit **)vb;
	return commit_pos_at(&commit_pos, a) -
	       commit_pos_at(&commit_pos, b);
}

define_commit_slab(commit_graph_data_slab, struct commit_graph_data);
static struct commit_graph_data_slab commit_graph_data_slab =
	COMMIT_SLAB_INIT(1, commit_graph_data_slab);

static int get_configured_generation_version(struct repository *r)
{
	int version = 2;
	repo_config_get_int(r, "commitgraph.generationversion", &version);
	return version;
}

uint32_t commit_graph_position(const struct commit *c)
{
	struct commit_graph_data *data =
		commit_graph_data_slab_peek(&commit_graph_data_slab, c);

	return data ? data->graph_pos : COMMIT_NOT_FROM_GRAPH;
}

timestamp_t commit_graph_generation(const struct commit *c)
{
	struct commit_graph_data *data =
		commit_graph_data_slab_peek(&commit_graph_data_slab, c);

	if (data && data->generation)
		return data->generation;

	return GENERATION_NUMBER_INFINITY;
}

static struct commit_graph_data *commit_graph_data_at(const struct commit *c)
{
	unsigned int i, nth_slab;
	struct commit_graph_data *data =
		commit_graph_data_slab_peek(&commit_graph_data_slab, c);

	if (data)
		return data;

	nth_slab = c->index / commit_graph_data_slab.slab_size;
	data = commit_graph_data_slab_at(&commit_graph_data_slab, c);

	/*
	 * commit-slab initializes elements with zero, overwrite this with
	 * COMMIT_NOT_FROM_GRAPH for graph_pos.
	 *
	 * We avoid initializing generation with checking if graph position
	 * is not COMMIT_NOT_FROM_GRAPH.
	 */
	for (i = 0; i < commit_graph_data_slab.slab_size; i++) {
		commit_graph_data_slab.slab[nth_slab][i].graph_pos =
			COMMIT_NOT_FROM_GRAPH;
	}

	return data;
}

/*
 * Should be used only while writing commit-graph as it compares
 * generation value of commits by directly accessing commit-slab.
 */
static int commit_gen_cmp(const void *va, const void *vb)
{
	const struct commit *a = *(const struct commit **)va;
	const struct commit *b = *(const struct commit **)vb;

	const timestamp_t generation_a = commit_graph_data_at(a)->generation;
	const timestamp_t generation_b = commit_graph_data_at(b)->generation;
	/* lower generation commits first */
	if (generation_a < generation_b)
		return -1;
	else if (generation_a > generation_b)
		return 1;

	/* use date as a heuristic when generations are equal */
	if (a->date < b->date)
		return -1;
	else if (a->date > b->date)
		return 1;
	return 0;
}

char *get_commit_graph_filename(struct object_directory *obj_dir)
{
	return xstrfmt("%s/info/commit-graph", obj_dir->path);
}

static char *get_split_graph_filename(struct object_directory *odb,
				      const char *oid_hex)
{
	return xstrfmt("%s/info/commit-graphs/graph-%s.graph", odb->path,
		       oid_hex);
}

char *get_commit_graph_chain_filename(struct object_directory *odb)
{
	return xstrfmt("%s/info/commit-graphs/commit-graph-chain", odb->path);
}

static struct commit_graph *alloc_commit_graph(void)
{
	struct commit_graph *g = xcalloc(1, sizeof(*g));

	return g;
}

extern int read_replace_refs;

static int commit_graph_compatible(struct repository *r)
{
	if (!r->gitdir)
		return 0;

	if (read_replace_refs) {
		prepare_replace_object(r);
		if (hashmap_get_size(&r->objects->replace_map->map))
			return 0;
	}

	prepare_commit_graft(r);
	if (r->parsed_objects &&
	    (r->parsed_objects->grafts_nr || r->parsed_objects->substituted_parent))
		return 0;
	if (is_repository_shallow(r))
		return 0;

	return 1;
}

int open_commit_graph(const char *graph_file, int *fd, struct stat *st)
{
	*fd = git_open(graph_file);
	if (*fd < 0)
		return 0;
	if (fstat(*fd, st)) {
		close(*fd);
		return 0;
	}
	return 1;
}

struct commit_graph *load_commit_graph_one_fd_st(struct repository *r,
						 int fd, struct stat *st,
						 struct object_directory *odb)
{
	void *graph_map;
	size_t graph_size;
	struct commit_graph *ret;

	graph_size = xsize_t(st->st_size);

	if (graph_size < GRAPH_MIN_SIZE) {
		close(fd);
		error(_("commit-graph file is too small"));
		return NULL;
	}
	graph_map = xmmap(NULL, graph_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	prepare_repo_settings(r);
	ret = parse_commit_graph(&r->settings, graph_map, graph_size);

	if (ret)
		ret->odb = odb;
	else
		munmap(graph_map, graph_size);

	return ret;
}

static int verify_commit_graph_lite(struct commit_graph *g)
{
	/*
	 * Basic validation shared between parse_commit_graph()
	 * which'll be called every time the graph is used, and the
	 * much more expensive verify_commit_graph() used by
	 * "commit-graph verify".
	 *
	 * There should only be very basic checks here to ensure that
	 * we don't e.g. segfault in fill_commit_in_graph(), but
	 * because this is a very hot codepath nothing that e.g. loops
	 * over g->num_commits, or runs a checksum on the commit-graph
	 * itself.
	 */
	if (!g->chunk_oid_fanout) {
		error("commit-graph is missing the OID Fanout chunk");
		return 1;
	}
	if (!g->chunk_oid_lookup) {
		error("commit-graph is missing the OID Lookup chunk");
		return 1;
	}
	if (!g->chunk_commit_data) {
		error("commit-graph is missing the Commit Data chunk");
		return 1;
	}

	return 0;
}

static int graph_read_oid_lookup(const unsigned char *chunk_start,
				 size_t chunk_size, void *data)
{
	struct commit_graph *g = data;
	g->chunk_oid_lookup = chunk_start;
	g->num_commits = chunk_size / g->hash_len;
	return 0;
}

static int graph_read_bloom_data(const unsigned char *chunk_start,
				  size_t chunk_size, void *data)
{
	struct commit_graph *g = data;
	uint32_t hash_version;
	g->chunk_bloom_data = chunk_start;
	hash_version = get_be32(chunk_start);

	if (hash_version != 1)
		return 0;

	g->bloom_filter_settings = xmalloc(sizeof(struct bloom_filter_settings));
	g->bloom_filter_settings->hash_version = hash_version;
	g->bloom_filter_settings->num_hashes = get_be32(chunk_start + 4);
	g->bloom_filter_settings->bits_per_entry = get_be32(chunk_start + 8);
	g->bloom_filter_settings->max_changed_paths = DEFAULT_BLOOM_MAX_CHANGES;

	return 0;
}

struct commit_graph *parse_commit_graph(struct repo_settings *s,
					void *graph_map, size_t graph_size)
{
	const unsigned char *data;
	struct commit_graph *graph;
	uint32_t graph_signature;
	unsigned char graph_version, hash_version;
	struct chunkfile *cf = NULL;

	if (!graph_map)
		return NULL;

	if (graph_size < GRAPH_MIN_SIZE)
		return NULL;

	data = (const unsigned char *)graph_map;

	graph_signature = get_be32(data);
	if (graph_signature != GRAPH_SIGNATURE) {
		error(_("commit-graph signature %X does not match signature %X"),
		      graph_signature, GRAPH_SIGNATURE);
		return NULL;
	}

	graph_version = *(unsigned char*)(data + 4);
	if (graph_version != GRAPH_VERSION) {
		error(_("commit-graph version %X does not match version %X"),
		      graph_version, GRAPH_VERSION);
		return NULL;
	}

	hash_version = *(unsigned char*)(data + 5);
	if (hash_version != oid_version(the_hash_algo)) {
		error(_("commit-graph hash version %X does not match version %X"),
		      hash_version, oid_version(the_hash_algo));
		return NULL;
	}

	graph = alloc_commit_graph();

	graph->hash_len = the_hash_algo->rawsz;
	graph->num_chunks = *(unsigned char*)(data + 6);
	graph->data = graph_map;
	graph->data_len = graph_size;

	if (graph_size < GRAPH_HEADER_SIZE +
			 (graph->num_chunks + 1) * CHUNK_TOC_ENTRY_SIZE +
			 GRAPH_FANOUT_SIZE + the_hash_algo->rawsz) {
		error(_("commit-graph file is too small to hold %u chunks"),
		      graph->num_chunks);
		free(graph);
		return NULL;
	}

	cf = init_chunkfile(NULL);

	if (read_table_of_contents(cf, graph->data, graph_size,
				   GRAPH_HEADER_SIZE, graph->num_chunks))
		goto free_and_return;

	pair_chunk(cf, GRAPH_CHUNKID_OIDFANOUT,
		   (const unsigned char **)&graph->chunk_oid_fanout);
	read_chunk(cf, GRAPH_CHUNKID_OIDLOOKUP, graph_read_oid_lookup, graph);
	pair_chunk(cf, GRAPH_CHUNKID_DATA, &graph->chunk_commit_data);
	pair_chunk(cf, GRAPH_CHUNKID_EXTRAEDGES, &graph->chunk_extra_edges);
	pair_chunk(cf, GRAPH_CHUNKID_BASE, &graph->chunk_base_graphs);

	if (s->commit_graph_generation_version >= 2) {
		pair_chunk(cf, GRAPH_CHUNKID_GENERATION_DATA,
			&graph->chunk_generation_data);
		pair_chunk(cf, GRAPH_CHUNKID_GENERATION_DATA_OVERFLOW,
			&graph->chunk_generation_data_overflow);

		if (graph->chunk_generation_data)
			graph->read_generation_data = 1;
	}

	if (s->commit_graph_read_changed_paths) {
		pair_chunk(cf, GRAPH_CHUNKID_BLOOMINDEXES,
			   &graph->chunk_bloom_indexes);
		read_chunk(cf, GRAPH_CHUNKID_BLOOMDATA,
			   graph_read_bloom_data, graph);
	}

	if (graph->chunk_bloom_indexes && graph->chunk_bloom_data) {
		init_bloom_filters();
	} else {
		/* We need both the bloom chunks to exist together. Else ignore the data */
		graph->chunk_bloom_indexes = NULL;
		graph->chunk_bloom_data = NULL;
		FREE_AND_NULL(graph->bloom_filter_settings);
	}

	oidread(&graph->oid, graph->data + graph->data_len - graph->hash_len);

	if (verify_commit_graph_lite(graph))
		goto free_and_return;

	free_chunkfile(cf);
	return graph;

free_and_return:
	free_chunkfile(cf);
	free(graph->bloom_filter_settings);
	free(graph);
	return NULL;
}

static struct commit_graph *load_commit_graph_one(struct repository *r,
						  const char *graph_file,
						  struct object_directory *odb)
{

	struct stat st;
	int fd;
	struct commit_graph *g;
	int open_ok = open_commit_graph(graph_file, &fd, &st);

	if (!open_ok)
		return NULL;

	g = load_commit_graph_one_fd_st(r, fd, &st, odb);

	if (g)
		g->filename = xstrdup(graph_file);

	return g;
}

static struct commit_graph *load_commit_graph_v1(struct repository *r,
						 struct object_directory *odb)
{
	char *graph_name = get_commit_graph_filename(odb);
	struct commit_graph *g = load_commit_graph_one(r, graph_name, odb);
	free(graph_name);

	return g;
}

static int add_graph_to_chain(struct commit_graph *g,
			      struct commit_graph *chain,
			      struct object_id *oids,
			      int n)
{
	struct commit_graph *cur_g = chain;

	if (n && !g->chunk_base_graphs) {
		warning(_("commit-graph has no base graphs chunk"));
		return 0;
	}

	while (n) {
		n--;

		if (!cur_g ||
		    !oideq(&oids[n], &cur_g->oid) ||
		    !hasheq(oids[n].hash, g->chunk_base_graphs + g->hash_len * n)) {
			warning(_("commit-graph chain does not match"));
			return 0;
		}

		cur_g = cur_g->base_graph;
	}

	g->base_graph = chain;

	if (chain)
		g->num_commits_in_base = chain->num_commits + chain->num_commits_in_base;

	return 1;
}

static struct commit_graph *load_commit_graph_chain(struct repository *r,
						    struct object_directory *odb)
{
	struct commit_graph *graph_chain = NULL;
	struct strbuf line = STRBUF_INIT;
	struct stat st;
	struct object_id *oids;
	int i = 0, valid = 1, count;
	char *chain_name = get_commit_graph_chain_filename(odb);
	FILE *fp;
	int stat_res;

	fp = fopen(chain_name, "r");
	stat_res = stat(chain_name, &st);
	free(chain_name);

	if (!fp)
		return NULL;
	if (stat_res ||
	    st.st_size <= the_hash_algo->hexsz) {
		fclose(fp);
		return NULL;
	}

	count = st.st_size / (the_hash_algo->hexsz + 1);
	CALLOC_ARRAY(oids, count);

	prepare_alt_odb(r);

	for (i = 0; i < count; i++) {
		struct object_directory *odb;

		if (strbuf_getline_lf(&line, fp) == EOF)
			break;

		if (get_oid_hex(line.buf, &oids[i])) {
			warning(_("invalid commit-graph chain: line '%s' not a hash"),
				line.buf);
			valid = 0;
			break;
		}

		valid = 0;
		for (odb = r->objects->odb; odb; odb = odb->next) {
			char *graph_name = get_split_graph_filename(odb, line.buf);
			struct commit_graph *g = load_commit_graph_one(r, graph_name, odb);

			free(graph_name);

			if (g) {
				if (add_graph_to_chain(g, graph_chain, oids, i)) {
					graph_chain = g;
					valid = 1;
				}

				break;
			}
		}

		if (!valid) {
			warning(_("unable to find all commit-graph files"));
			break;
		}
	}

	free(oids);
	fclose(fp);
	strbuf_release(&line);

	return graph_chain;
}

/*
 * returns 1 if and only if all graphs in the chain have
 * corrected commit dates stored in the generation_data chunk.
 */
static int validate_mixed_generation_chain(struct commit_graph *g)
{
	int read_generation_data = 1;
	struct commit_graph *p = g;

	while (read_generation_data && p) {
		read_generation_data = p->read_generation_data;
		p = p->base_graph;
	}

	if (read_generation_data)
		return 1;

	while (g) {
		g->read_generation_data = 0;
		g = g->base_graph;
	}

	return 0;
}

struct commit_graph *read_commit_graph_one(struct repository *r,
					   struct object_directory *odb)
{
	struct commit_graph *g = load_commit_graph_v1(r, odb);

	if (!g)
		g = load_commit_graph_chain(r, odb);

	validate_mixed_generation_chain(g);

	return g;
}

static void prepare_commit_graph_one(struct repository *r,
				     struct object_directory *odb)
{

	if (r->objects->commit_graph)
		return;

	r->objects->commit_graph = read_commit_graph_one(r, odb);
}

/*
 * Return 1 if commit_graph is non-NULL, and 0 otherwise.
 *
 * On the first invocation, this function attempts to load the commit
 * graph if the_repository is configured to have one.
 */
static int prepare_commit_graph(struct repository *r)
{
	struct object_directory *odb;

	/*
	 * Early return if there is no git dir or if the commit graph is
	 * disabled.
	 *
	 * This must come before the "already attempted?" check below, because
	 * we want to disable even an already-loaded graph file.
	 */
	if (!r->gitdir || r->commit_graph_disabled)
		return 0;

	if (r->objects->commit_graph_attempted)
		return !!r->objects->commit_graph;
	r->objects->commit_graph_attempted = 1;

	prepare_repo_settings(r);

	if (!git_env_bool(GIT_TEST_COMMIT_GRAPH, 0) &&
	    r->settings.core_commit_graph != 1)
		/*
		 * This repository is not configured to use commit graphs, so
		 * do not load one. (But report commit_graph_attempted anyway
		 * so that commit graph loading is not attempted again for this
		 * repository.)
		 */
		return 0;

	if (!commit_graph_compatible(r))
		return 0;

	prepare_alt_odb(r);
	for (odb = r->objects->odb;
	     !r->objects->commit_graph && odb;
	     odb = odb->next)
		prepare_commit_graph_one(r, odb);
	return !!r->objects->commit_graph;
}

int generation_numbers_enabled(struct repository *r)
{
	uint32_t first_generation;
	struct commit_graph *g;
	if (!prepare_commit_graph(r))
	       return 0;

	g = r->objects->commit_graph;

	if (!g->num_commits)
		return 0;

	first_generation = get_be32(g->chunk_commit_data +
				    g->hash_len + 8) >> 2;

	return !!first_generation;
}

int corrected_commit_dates_enabled(struct repository *r)
{
	struct commit_graph *g;
	if (!prepare_commit_graph(r))
		return 0;

	g = r->objects->commit_graph;

	if (!g->num_commits)
		return 0;

	return g->read_generation_data;
}

struct bloom_filter_settings *get_bloom_filter_settings(struct repository *r)
{
	struct commit_graph *g = r->objects->commit_graph;
	while (g) {
		if (g->bloom_filter_settings)
			return g->bloom_filter_settings;
		g = g->base_graph;
	}
	return NULL;
}

static void close_commit_graph_one(struct commit_graph *g)
{
	if (!g)
		return;

	clear_commit_graph_data_slab(&commit_graph_data_slab);
	close_commit_graph_one(g->base_graph);
	free_commit_graph(g);
}

void close_commit_graph(struct raw_object_store *o)
{
	close_commit_graph_one(o->commit_graph);
	o->commit_graph = NULL;
}

static int bsearch_graph(struct commit_graph *g, const struct object_id *oid, uint32_t *pos)
{
	return bsearch_hash(oid->hash, g->chunk_oid_fanout,
			    g->chunk_oid_lookup, g->hash_len, pos);
}

static void load_oid_from_graph(struct commit_graph *g,
				uint32_t pos,
				struct object_id *oid)
{
	uint32_t lex_index;

	while (g && pos < g->num_commits_in_base)
		g = g->base_graph;

	if (!g)
		BUG("NULL commit-graph");

	if (pos >= g->num_commits + g->num_commits_in_base)
		die(_("invalid commit position. commit-graph is likely corrupt"));

	lex_index = pos - g->num_commits_in_base;

	oidread(oid, g->chunk_oid_lookup + g->hash_len * lex_index);
}

static struct commit_list **insert_parent_or_die(struct repository *r,
						 struct commit_graph *g,
						 uint32_t pos,
						 struct commit_list **pptr)
{
	struct commit *c;
	struct object_id oid;

	if (pos >= g->num_commits + g->num_commits_in_base)
		die("invalid parent position %"PRIu32, pos);

	load_oid_from_graph(g, pos, &oid);
	c = lookup_commit(r, &oid);
	if (!c)
		die(_("could not find commit %s"), oid_to_hex(&oid));
	commit_graph_data_at(c)->graph_pos = pos;
	return &commit_list_insert(c, pptr)->next;
}

static void fill_commit_graph_info(struct commit *item, struct commit_graph *g, uint32_t pos)
{
	const unsigned char *commit_data;
	struct commit_graph_data *graph_data;
	uint32_t lex_index, offset_pos;
	uint64_t date_high, date_low, offset;

	while (pos < g->num_commits_in_base)
		g = g->base_graph;

	if (pos >= g->num_commits + g->num_commits_in_base)
		die(_("invalid commit position. commit-graph is likely corrupt"));

	lex_index = pos - g->num_commits_in_base;
	commit_data = g->chunk_commit_data + GRAPH_DATA_WIDTH * lex_index;

	graph_data = commit_graph_data_at(item);
	graph_data->graph_pos = pos;

	date_high = get_be32(commit_data + g->hash_len + 8) & 0x3;
	date_low = get_be32(commit_data + g->hash_len + 12);
	item->date = (timestamp_t)((date_high << 32) | date_low);

	if (g->read_generation_data) {
		offset = (timestamp_t)get_be32(g->chunk_generation_data + sizeof(uint32_t) * lex_index);

		if (offset & CORRECTED_COMMIT_DATE_OFFSET_OVERFLOW) {
			if (!g->chunk_generation_data_overflow)
				die(_("commit-graph requires overflow generation data but has none"));

			offset_pos = offset ^ CORRECTED_COMMIT_DATE_OFFSET_OVERFLOW;
			graph_data->generation = item->date + get_be64(g->chunk_generation_data_overflow + 8 * offset_pos);
		} else
			graph_data->generation = item->date + offset;
	} else
		graph_data->generation = get_be32(commit_data + g->hash_len + 8) >> 2;

	if (g->topo_levels)
		*topo_level_slab_at(g->topo_levels, item) = get_be32(commit_data + g->hash_len + 8) >> 2;
}

static inline void set_commit_tree(struct commit *c, struct tree *t)
{
	c->maybe_tree = t;
}

static int fill_commit_in_graph(struct repository *r,
				struct commit *item,
				struct commit_graph *g, uint32_t pos)
{
	uint32_t edge_value;
	uint32_t *parent_data_ptr;
	struct commit_list **pptr;
	const unsigned char *commit_data;
	uint32_t lex_index;

	while (pos < g->num_commits_in_base)
		g = g->base_graph;

	fill_commit_graph_info(item, g, pos);

	lex_index = pos - g->num_commits_in_base;
	commit_data = g->chunk_commit_data + (g->hash_len + 16) * lex_index;

	item->object.parsed = 1;

	set_commit_tree(item, NULL);

	pptr = &item->parents;

	edge_value = get_be32(commit_data + g->hash_len);
	if (edge_value == GRAPH_PARENT_NONE)
		return 1;
	pptr = insert_parent_or_die(r, g, edge_value, pptr);

	edge_value = get_be32(commit_data + g->hash_len + 4);
	if (edge_value == GRAPH_PARENT_NONE)
		return 1;
	if (!(edge_value & GRAPH_EXTRA_EDGES_NEEDED)) {
		pptr = insert_parent_or_die(r, g, edge_value, pptr);
		return 1;
	}

	parent_data_ptr = (uint32_t*)(g->chunk_extra_edges +
			  4 * (uint64_t)(edge_value & GRAPH_EDGE_LAST_MASK));
	do {
		edge_value = get_be32(parent_data_ptr);
		pptr = insert_parent_or_die(r, g,
					    edge_value & GRAPH_EDGE_LAST_MASK,
					    pptr);
		parent_data_ptr++;
	} while (!(edge_value & GRAPH_LAST_EDGE));

	return 1;
}

static int search_commit_pos_in_graph(const struct object_id *id, struct commit_graph *g, uint32_t *pos)
{
	struct commit_graph *cur_g = g;
	uint32_t lex_index;

	while (cur_g && !bsearch_graph(cur_g, id, &lex_index))
		cur_g = cur_g->base_graph;

	if (cur_g) {
		*pos = lex_index + cur_g->num_commits_in_base;
		return 1;
	}

	return 0;
}

static int find_commit_pos_in_graph(struct commit *item, struct commit_graph *g, uint32_t *pos)
{
	uint32_t graph_pos = commit_graph_position(item);
	if (graph_pos != COMMIT_NOT_FROM_GRAPH) {
		*pos = graph_pos;
		return 1;
	} else {
		return search_commit_pos_in_graph(&item->object.oid, g, pos);
	}
}

int repo_find_commit_pos_in_graph(struct repository *r, struct commit *c,
				  uint32_t *pos)
{
	if (!prepare_commit_graph(r))
		return 0;
	return find_commit_pos_in_graph(c, r->objects->commit_graph, pos);
}

struct commit *lookup_commit_in_graph(struct repository *repo, const struct object_id *id)
{
	struct commit *commit;
	uint32_t pos;

	if (!prepare_commit_graph(repo))
		return NULL;
	if (!search_commit_pos_in_graph(id, repo->objects->commit_graph, &pos))
		return NULL;
	if (!has_object(repo, id, 0))
		return NULL;

	commit = lookup_commit(repo, id);
	if (!commit)
		return NULL;
	if (commit->object.parsed)
		return commit;

	if (!fill_commit_in_graph(repo, commit, repo->objects->commit_graph, pos))
		return NULL;

	return commit;
}

static int parse_commit_in_graph_one(struct repository *r,
				     struct commit_graph *g,
				     struct commit *item)
{
	uint32_t pos;

	if (item->object.parsed)
		return 1;

	if (find_commit_pos_in_graph(item, g, &pos))
		return fill_commit_in_graph(r, item, g, pos);

	return 0;
}

int parse_commit_in_graph(struct repository *r, struct commit *item)
{
	static int checked_env = 0;

	if (!checked_env &&
	    git_env_bool(GIT_TEST_COMMIT_GRAPH_DIE_ON_PARSE, 0))
		die("dying as requested by the '%s' variable on commit-graph parse!",
		    GIT_TEST_COMMIT_GRAPH_DIE_ON_PARSE);
	checked_env = 1;

	if (!prepare_commit_graph(r))
		return 0;
	return parse_commit_in_graph_one(r, r->objects->commit_graph, item);
}

void load_commit_graph_info(struct repository *r, struct commit *item)
{
	uint32_t pos;
	if (repo_find_commit_pos_in_graph(r, item, &pos))
		fill_commit_graph_info(item, r->objects->commit_graph, pos);
}

static struct tree *load_tree_for_commit(struct repository *r,
					 struct commit_graph *g,
					 struct commit *c)
{
	struct object_id oid;
	const unsigned char *commit_data;
	uint32_t graph_pos = commit_graph_position(c);

	while (graph_pos < g->num_commits_in_base)
		g = g->base_graph;

	commit_data = g->chunk_commit_data +
			GRAPH_DATA_WIDTH * (graph_pos - g->num_commits_in_base);

	oidread(&oid, commit_data);
	set_commit_tree(c, lookup_tree(r, &oid));

	return c->maybe_tree;
}

static struct tree *get_commit_tree_in_graph_one(struct repository *r,
						 struct commit_graph *g,
						 const struct commit *c)
{
	if (c->maybe_tree)
		return c->maybe_tree;
	if (commit_graph_position(c) == COMMIT_NOT_FROM_GRAPH)
		BUG("get_commit_tree_in_graph_one called from non-commit-graph commit");

	return load_tree_for_commit(r, g, (struct commit *)c);
}

struct tree *get_commit_tree_in_graph(struct repository *r, const struct commit *c)
{
	return get_commit_tree_in_graph_one(r, r->objects->commit_graph, c);
}

struct packed_commit_list {
	struct commit **list;
	size_t nr;
	size_t alloc;
};

struct write_commit_graph_context {
	struct repository *r;
	struct object_directory *odb;
	char *graph_name;
	struct oid_array oids;
	struct packed_commit_list commits;
	int num_extra_edges;
	int num_generation_data_overflows;
	unsigned long approx_nr_objects;
	struct progress *progress;
	int progress_done;
	uint64_t progress_cnt;

	char *base_graph_name;
	int num_commit_graphs_before;
	int num_commit_graphs_after;
	char **commit_graph_filenames_before;
	char **commit_graph_filenames_after;
	char **commit_graph_hash_after;
	uint32_t new_num_commits_in_base;
	struct commit_graph *new_base_graph;

	unsigned append:1,
		 report_progress:1,
		 split:1,
		 changed_paths:1,
		 order_by_pack:1,
		 write_generation_data:1,
		 trust_generation_numbers:1;

	struct topo_level_slab *topo_levels;
	const struct commit_graph_opts *opts;
	size_t total_bloom_filter_data_size;
	const struct bloom_filter_settings *bloom_settings;

	int count_bloom_filter_computed;
	int count_bloom_filter_not_computed;
	int count_bloom_filter_trunc_empty;
	int count_bloom_filter_trunc_large;
};

static int write_graph_chunk_fanout(struct hashfile *f,
				    void *data)
{
	struct write_commit_graph_context *ctx = data;
	int i, count = 0;
	struct commit **list = ctx->commits.list;

	/*
	 * Write the first-level table (the list is sorted,
	 * but we use a 256-entry lookup to be able to avoid
	 * having to do eight extra binary search iterations).
	 */
	for (i = 0; i < 256; i++) {
		while (count < ctx->commits.nr) {
			if ((*list)->object.oid.hash[0] != i)
				break;
			display_progress(ctx->progress, ++ctx->progress_cnt);
			count++;
			list++;
		}

		hashwrite_be32(f, count);
	}

	return 0;
}

static int write_graph_chunk_oids(struct hashfile *f,
				  void *data)
{
	struct write_commit_graph_context *ctx = data;
	struct commit **list = ctx->commits.list;
	int count;
	for (count = 0; count < ctx->commits.nr; count++, list++) {
		display_progress(ctx->progress, ++ctx->progress_cnt);
		hashwrite(f, (*list)->object.oid.hash, the_hash_algo->rawsz);
	}

	return 0;
}

static const struct object_id *commit_to_oid(size_t index, const void *table)
{
	const struct commit * const *commits = table;
	return &commits[index]->object.oid;
}

static int write_graph_chunk_data(struct hashfile *f,
				  void *data)
{
	struct write_commit_graph_context *ctx = data;
	struct commit **list = ctx->commits.list;
	struct commit **last = ctx->commits.list + ctx->commits.nr;
	uint32_t num_extra_edges = 0;

	while (list < last) {
		struct commit_list *parent;
		struct object_id *tree;
		int edge_value;
		uint32_t packedDate[2];
		display_progress(ctx->progress, ++ctx->progress_cnt);

		if (repo_parse_commit_no_graph(ctx->r, *list))
			die(_("unable to parse commit %s"),
				oid_to_hex(&(*list)->object.oid));
		tree = get_commit_tree_oid(*list);
		hashwrite(f, tree->hash, the_hash_algo->rawsz);

		parent = (*list)->parents;

		if (!parent)
			edge_value = GRAPH_PARENT_NONE;
		else {
			edge_value = oid_pos(&parent->item->object.oid,
					     ctx->commits.list,
					     ctx->commits.nr,
					     commit_to_oid);

			if (edge_value >= 0)
				edge_value += ctx->new_num_commits_in_base;
			else if (ctx->new_base_graph) {
				uint32_t pos;
				if (find_commit_pos_in_graph(parent->item,
							     ctx->new_base_graph,
							     &pos))
					edge_value = pos;
			}

			if (edge_value < 0)
				BUG("missing parent %s for commit %s",
				    oid_to_hex(&parent->item->object.oid),
				    oid_to_hex(&(*list)->object.oid));
		}

		hashwrite_be32(f, edge_value);

		if (parent)
			parent = parent->next;

		if (!parent)
			edge_value = GRAPH_PARENT_NONE;
		else if (parent->next)
			edge_value = GRAPH_EXTRA_EDGES_NEEDED | num_extra_edges;
		else {
			edge_value = oid_pos(&parent->item->object.oid,
					     ctx->commits.list,
					     ctx->commits.nr,
					     commit_to_oid);

			if (edge_value >= 0)
				edge_value += ctx->new_num_commits_in_base;
			else if (ctx->new_base_graph) {
				uint32_t pos;
				if (find_commit_pos_in_graph(parent->item,
							     ctx->new_base_graph,
							     &pos))
					edge_value = pos;
			}

			if (edge_value < 0)
				BUG("missing parent %s for commit %s",
				    oid_to_hex(&parent->item->object.oid),
				    oid_to_hex(&(*list)->object.oid));
		}

		hashwrite_be32(f, edge_value);

		if (edge_value & GRAPH_EXTRA_EDGES_NEEDED) {
			do {
				num_extra_edges++;
				parent = parent->next;
			} while (parent);
		}

		if (sizeof((*list)->date) > 4)
			packedDate[0] = htonl(((*list)->date >> 32) & 0x3);
		else
			packedDate[0] = 0;

		packedDate[0] |= htonl(*topo_level_slab_at(ctx->topo_levels, *list) << 2);

		packedDate[1] = htonl((*list)->date);
		hashwrite(f, packedDate, 8);

		list++;
	}

	return 0;
}

static int write_graph_chunk_generation_data(struct hashfile *f,
					     void *data)
{
	struct write_commit_graph_context *ctx = data;
	int i, num_generation_data_overflows = 0;

	for (i = 0; i < ctx->commits.nr; i++) {
		struct commit *c = ctx->commits.list[i];
		timestamp_t offset;
		repo_parse_commit(ctx->r, c);
		offset = commit_graph_data_at(c)->generation - c->date;
		display_progress(ctx->progress, ++ctx->progress_cnt);

		if (offset > GENERATION_NUMBER_V2_OFFSET_MAX) {
			offset = CORRECTED_COMMIT_DATE_OFFSET_OVERFLOW | num_generation_data_overflows;
			num_generation_data_overflows++;
		}

		hashwrite_be32(f, offset);
	}

	return 0;
}

static int write_graph_chunk_generation_data_overflow(struct hashfile *f,
						      void *data)
{
	struct write_commit_graph_context *ctx = data;
	int i;
	for (i = 0; i < ctx->commits.nr; i++) {
		struct commit *c = ctx->commits.list[i];
		timestamp_t offset = commit_graph_data_at(c)->generation - c->date;
		display_progress(ctx->progress, ++ctx->progress_cnt);

		if (offset > GENERATION_NUMBER_V2_OFFSET_MAX) {
			hashwrite_be32(f, offset >> 32);
			hashwrite_be32(f, (uint32_t) offset);
		}
	}

	return 0;
}

static int write_graph_chunk_extra_edges(struct hashfile *f,
					 void *data)
{
	struct write_commit_graph_context *ctx = data;
	struct commit **list = ctx->commits.list;
	struct commit **last = ctx->commits.list + ctx->commits.nr;
	struct commit_list *parent;

	while (list < last) {
		int num_parents = 0;

		display_progress(ctx->progress, ++ctx->progress_cnt);

		for (parent = (*list)->parents; num_parents < 3 && parent;
		     parent = parent->next)
			num_parents++;

		if (num_parents <= 2) {
			list++;
			continue;
		}

		/* Since num_parents > 2, this initializer is safe. */
		for (parent = (*list)->parents->next; parent; parent = parent->next) {
			int edge_value = oid_pos(&parent->item->object.oid,
						 ctx->commits.list,
						 ctx->commits.nr,
						 commit_to_oid);

			if (edge_value >= 0)
				edge_value += ctx->new_num_commits_in_base;
			else if (ctx->new_base_graph) {
				uint32_t pos;
				if (find_commit_pos_in_graph(parent->item,
							     ctx->new_base_graph,
							     &pos))
					edge_value = pos;
			}

			if (edge_value < 0)
				BUG("missing parent %s for commit %s",
				    oid_to_hex(&parent->item->object.oid),
				    oid_to_hex(&(*list)->object.oid));
			else if (!parent->next)
				edge_value |= GRAPH_LAST_EDGE;

			hashwrite_be32(f, edge_value);
		}

		list++;
	}

	return 0;
}

static int write_graph_chunk_bloom_indexes(struct hashfile *f,
					   void *data)
{
	struct write_commit_graph_context *ctx = data;
	struct commit **list = ctx->commits.list;
	struct commit **last = ctx->commits.list + ctx->commits.nr;
	uint32_t cur_pos = 0;

	while (list < last) {
		struct bloom_filter *filter = get_bloom_filter(ctx->r, *list);
		size_t len = filter ? filter->len : 0;
		cur_pos += len;
		display_progress(ctx->progress, ++ctx->progress_cnt);
		hashwrite_be32(f, cur_pos);
		list++;
	}

	return 0;
}

static void trace2_bloom_filter_settings(struct write_commit_graph_context *ctx)
{
	struct json_writer jw = JSON_WRITER_INIT;

	jw_object_begin(&jw, 0);
	jw_object_intmax(&jw, "hash_version", ctx->bloom_settings->hash_version);
	jw_object_intmax(&jw, "num_hashes", ctx->bloom_settings->num_hashes);
	jw_object_intmax(&jw, "bits_per_entry", ctx->bloom_settings->bits_per_entry);
	jw_object_intmax(&jw, "max_changed_paths", ctx->bloom_settings->max_changed_paths);
	jw_end(&jw);

	trace2_data_json("bloom", ctx->r, "settings", &jw);

	jw_release(&jw);
}

static int write_graph_chunk_bloom_data(struct hashfile *f,
					void *data)
{
	struct write_commit_graph_context *ctx = data;
	struct commit **list = ctx->commits.list;
	struct commit **last = ctx->commits.list + ctx->commits.nr;

	trace2_bloom_filter_settings(ctx);

	hashwrite_be32(f, ctx->bloom_settings->hash_version);
	hashwrite_be32(f, ctx->bloom_settings->num_hashes);
	hashwrite_be32(f, ctx->bloom_settings->bits_per_entry);

	while (list < last) {
		struct bloom_filter *filter = get_bloom_filter(ctx->r, *list);
		size_t len = filter ? filter->len : 0;

		display_progress(ctx->progress, ++ctx->progress_cnt);
		if (len)
			hashwrite(f, filter->data, len * sizeof(unsigned char));
		list++;
	}

	return 0;
}

static int add_packed_commits(const struct object_id *oid,
			      struct packed_git *pack,
			      uint32_t pos,
			      void *data)
{
	struct write_commit_graph_context *ctx = (struct write_commit_graph_context*)data;
	enum object_type type;
	off_t offset = nth_packed_object_offset(pack, pos);
	struct object_info oi = OBJECT_INFO_INIT;

	if (ctx->progress)
		display_progress(ctx->progress, ++ctx->progress_done);

	oi.typep = &type;
	if (packed_object_info(ctx->r, pack, offset, &oi) < 0)
		die(_("unable to get type of object %s"), oid_to_hex(oid));

	if (type != OBJ_COMMIT)
		return 0;

	oid_array_append(&ctx->oids, oid);
	set_commit_pos(ctx->r, oid);

	return 0;
}

static void add_missing_parents(struct write_commit_graph_context *ctx, struct commit *commit)
{
	struct commit_list *parent;
	for (parent = commit->parents; parent; parent = parent->next) {
		if (!(parent->item->object.flags & REACHABLE)) {
			oid_array_append(&ctx->oids, &parent->item->object.oid);
			parent->item->object.flags |= REACHABLE;
		}
	}
}

static void close_reachable(struct write_commit_graph_context *ctx)
{
	int i;
	struct commit *commit;
	enum commit_graph_split_flags flags = ctx->opts ?
		ctx->opts->split_flags : COMMIT_GRAPH_SPLIT_UNSPECIFIED;

	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Loading known commits in commit graph"),
					ctx->oids.nr);
	for (i = 0; i < ctx->oids.nr; i++) {
		display_progress(ctx->progress, i + 1);
		commit = lookup_commit(ctx->r, &ctx->oids.oid[i]);
		if (commit)
			commit->object.flags |= REACHABLE;
	}
	stop_progress(&ctx->progress);

	/*
	 * As this loop runs, ctx->oids.nr may grow, but not more
	 * than the number of missing commits in the reachable
	 * closure.
	 */
	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Expanding reachable commits in commit graph"),
					0);
	for (i = 0; i < ctx->oids.nr; i++) {
		display_progress(ctx->progress, i + 1);
		commit = lookup_commit(ctx->r, &ctx->oids.oid[i]);

		if (!commit)
			continue;
		if (ctx->split) {
			if ((!repo_parse_commit(ctx->r, commit) &&
			     commit_graph_position(commit) == COMMIT_NOT_FROM_GRAPH) ||
			    flags == COMMIT_GRAPH_SPLIT_REPLACE)
				add_missing_parents(ctx, commit);
		} else if (!repo_parse_commit_no_graph(ctx->r, commit))
			add_missing_parents(ctx, commit);
	}
	stop_progress(&ctx->progress);

	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Clearing commit marks in commit graph"),
					ctx->oids.nr);
	for (i = 0; i < ctx->oids.nr; i++) {
		display_progress(ctx->progress, i + 1);
		commit = lookup_commit(ctx->r, &ctx->oids.oid[i]);

		if (commit)
			commit->object.flags &= ~REACHABLE;
	}
	stop_progress(&ctx->progress);
}

struct compute_generation_info {
	struct repository *r;
	struct packed_commit_list *commits;
	struct progress *progress;
	int progress_cnt;

	timestamp_t (*get_generation)(struct commit *c, void *data);
	void (*set_generation)(struct commit *c, timestamp_t gen, void *data);
	void *data;
};

static timestamp_t compute_generation_from_max(struct commit *c,
					       timestamp_t max_gen,
					       int generation_version)
{
	switch (generation_version) {
	case 1: /* topological levels */
		if (max_gen > GENERATION_NUMBER_V1_MAX - 1)
			max_gen = GENERATION_NUMBER_V1_MAX - 1;
		return max_gen + 1;

	case 2: /* corrected commit date */
		if (c->date && c->date > max_gen)
			max_gen = c->date - 1;
		return max_gen + 1;

	default:
		BUG("attempting unimplemented version");
	}
}

static void compute_reachable_generation_numbers(
			struct compute_generation_info *info,
			int generation_version)
{
	int i;
	struct commit_list *list = NULL;

	for (i = 0; i < info->commits->nr; i++) {
		struct commit *c = info->commits->list[i];
		timestamp_t gen;
		repo_parse_commit(info->r, c);
		gen = info->get_generation(c, info->data);
		display_progress(info->progress, info->progress_cnt + 1);

		if (gen != GENERATION_NUMBER_ZERO && gen != GENERATION_NUMBER_INFINITY)
			continue;

		commit_list_insert(c, &list);
		while (list) {
			struct commit *current = list->item;
			struct commit_list *parent;
			int all_parents_computed = 1;
			uint32_t max_gen = 0;

			for (parent = current->parents; parent; parent = parent->next) {
				repo_parse_commit(info->r, parent->item);
				gen = info->get_generation(parent->item, info->data);

				if (gen == GENERATION_NUMBER_ZERO) {
					all_parents_computed = 0;
					commit_list_insert(parent->item, &list);
					break;
				}

				if (gen > max_gen)
					max_gen = gen;
			}

			if (all_parents_computed) {
				pop_commit(&list);
				gen = compute_generation_from_max(
						current, max_gen,
						generation_version);
				info->set_generation(current, gen, info->data);
			}
		}
	}
}

static timestamp_t get_topo_level(struct commit *c, void *data)
{
	struct write_commit_graph_context *ctx = data;
	return *topo_level_slab_at(ctx->topo_levels, c);
}

static void set_topo_level(struct commit *c, timestamp_t t, void *data)
{
	struct write_commit_graph_context *ctx = data;
	*topo_level_slab_at(ctx->topo_levels, c) = (uint32_t)t;
}

static void compute_topological_levels(struct write_commit_graph_context *ctx)
{
	struct compute_generation_info info = {
		.r = ctx->r,
		.commits = &ctx->commits,
		.get_generation = get_topo_level,
		.set_generation = set_topo_level,
		.data = ctx,
	};

	if (ctx->report_progress)
		info.progress = ctx->progress
			      = start_delayed_progress(
					_("Computing commit graph topological levels"),
					ctx->commits.nr);

	compute_reachable_generation_numbers(&info, 1);

	stop_progress(&ctx->progress);
}

static timestamp_t get_generation_from_graph_data(struct commit *c, void *data)
{
	return commit_graph_data_at(c)->generation;
}

static void set_generation_v2(struct commit *c, timestamp_t t, void *data)
{
	struct commit_graph_data *g = commit_graph_data_at(c);
	g->generation = t;
}

static void compute_generation_numbers(struct write_commit_graph_context *ctx)
{
	int i;
	struct compute_generation_info info = {
		.r = ctx->r,
		.commits = &ctx->commits,
		.get_generation = get_generation_from_graph_data,
		.set_generation = set_generation_v2,
		.data = ctx,
	};

	if (ctx->report_progress)
		info.progress = ctx->progress
			      = start_delayed_progress(
					_("Computing commit graph generation numbers"),
					ctx->commits.nr);

	if (!ctx->trust_generation_numbers) {
		for (i = 0; i < ctx->commits.nr; i++) {
			struct commit *c = ctx->commits.list[i];
			repo_parse_commit(ctx->r, c);
			commit_graph_data_at(c)->generation = GENERATION_NUMBER_ZERO;
		}
	}

	compute_reachable_generation_numbers(&info, 2);

	for (i = 0; i < ctx->commits.nr; i++) {
		struct commit *c = ctx->commits.list[i];
		timestamp_t offset = commit_graph_data_at(c)->generation - c->date;
		if (offset > GENERATION_NUMBER_V2_OFFSET_MAX)
			ctx->num_generation_data_overflows++;
	}
	stop_progress(&ctx->progress);
}

static void set_generation_in_graph_data(struct commit *c, timestamp_t t,
					 void *data)
{
	commit_graph_data_at(c)->generation = t;
}

/*
 * After this method, all commits reachable from those in the given
 * list will have non-zero, non-infinite generation numbers.
 */
void ensure_generations_valid(struct repository *r,
			      struct commit **commits, size_t nr)
{
	int generation_version = get_configured_generation_version(r);
	struct packed_commit_list list = {
		.list = commits,
		.alloc = nr,
		.nr = nr,
	};
	struct compute_generation_info info = {
		.r = r,
		.commits = &list,
		.get_generation = get_generation_from_graph_data,
		.set_generation = set_generation_in_graph_data,
	};

	compute_reachable_generation_numbers(&info, generation_version);
}

static void trace2_bloom_filter_write_statistics(struct write_commit_graph_context *ctx)
{
	trace2_data_intmax("commit-graph", ctx->r, "filter-computed",
			   ctx->count_bloom_filter_computed);
	trace2_data_intmax("commit-graph", ctx->r, "filter-not-computed",
			   ctx->count_bloom_filter_not_computed);
	trace2_data_intmax("commit-graph", ctx->r, "filter-trunc-empty",
			   ctx->count_bloom_filter_trunc_empty);
	trace2_data_intmax("commit-graph", ctx->r, "filter-trunc-large",
			   ctx->count_bloom_filter_trunc_large);
}

static void compute_bloom_filters(struct write_commit_graph_context *ctx)
{
	int i;
	struct progress *progress = NULL;
	struct commit **sorted_commits;
	int max_new_filters;

	init_bloom_filters();

	if (ctx->report_progress)
		progress = start_delayed_progress(
			_("Computing commit changed paths Bloom filters"),
			ctx->commits.nr);

	DUP_ARRAY(sorted_commits, ctx->commits.list, ctx->commits.nr);

	if (ctx->order_by_pack)
		QSORT(sorted_commits, ctx->commits.nr, commit_pos_cmp);
	else
		QSORT(sorted_commits, ctx->commits.nr, commit_gen_cmp);

	max_new_filters = ctx->opts && ctx->opts->max_new_filters >= 0 ?
		ctx->opts->max_new_filters : ctx->commits.nr;

	for (i = 0; i < ctx->commits.nr; i++) {
		enum bloom_filter_computed computed = 0;
		struct commit *c = sorted_commits[i];
		struct bloom_filter *filter = get_or_compute_bloom_filter(
			ctx->r,
			c,
			ctx->count_bloom_filter_computed < max_new_filters,
			ctx->bloom_settings,
			&computed);
		if (computed & BLOOM_COMPUTED) {
			ctx->count_bloom_filter_computed++;
			if (computed & BLOOM_TRUNC_EMPTY)
				ctx->count_bloom_filter_trunc_empty++;
			if (computed & BLOOM_TRUNC_LARGE)
				ctx->count_bloom_filter_trunc_large++;
		} else if (computed & BLOOM_NOT_COMPUTED)
			ctx->count_bloom_filter_not_computed++;
		ctx->total_bloom_filter_data_size += filter
			? sizeof(unsigned char) * filter->len : 0;
		display_progress(progress, i + 1);
	}

	if (trace2_is_enabled())
		trace2_bloom_filter_write_statistics(ctx);

	free(sorted_commits);
	stop_progress(&progress);
}

struct refs_cb_data {
	struct oidset *commits;
	struct progress *progress;
};

static int add_ref_to_set(const char *refname UNUSED,
			  const struct object_id *oid,
			  int flags UNUSED, void *cb_data)
{
	struct object_id peeled;
	struct refs_cb_data *data = (struct refs_cb_data *)cb_data;

	if (!peel_iterated_oid(oid, &peeled))
		oid = &peeled;
	if (oid_object_info(the_repository, oid, NULL) == OBJ_COMMIT)
		oidset_insert(data->commits, oid);

	display_progress(data->progress, oidset_size(data->commits));

	return 0;
}

int write_commit_graph_reachable(struct object_directory *odb,
				 enum commit_graph_write_flags flags,
				 const struct commit_graph_opts *opts)
{
	struct oidset commits = OIDSET_INIT;
	struct refs_cb_data data;
	int result;

	memset(&data, 0, sizeof(data));
	data.commits = &commits;
	if (flags & COMMIT_GRAPH_WRITE_PROGRESS)
		data.progress = start_delayed_progress(
			_("Collecting referenced commits"), 0);

	for_each_ref(add_ref_to_set, &data);

	stop_progress(&data.progress);

	result = write_commit_graph(odb, NULL, &commits,
				    flags, opts);

	oidset_clear(&commits);
	return result;
}

static int fill_oids_from_packs(struct write_commit_graph_context *ctx,
				const struct string_list *pack_indexes)
{
	uint32_t i;
	struct strbuf progress_title = STRBUF_INIT;
	struct strbuf packname = STRBUF_INIT;
	int dirlen;
	int ret = 0;

	strbuf_addf(&packname, "%s/pack/", ctx->odb->path);
	dirlen = packname.len;
	if (ctx->report_progress) {
		strbuf_addf(&progress_title,
			    Q_("Finding commits for commit graph in %"PRIuMAX" pack",
			       "Finding commits for commit graph in %"PRIuMAX" packs",
			       pack_indexes->nr),
			    (uintmax_t)pack_indexes->nr);
		ctx->progress = start_delayed_progress(progress_title.buf, 0);
		ctx->progress_done = 0;
	}
	for (i = 0; i < pack_indexes->nr; i++) {
		struct packed_git *p;
		strbuf_setlen(&packname, dirlen);
		strbuf_addstr(&packname, pack_indexes->items[i].string);
		p = add_packed_git(packname.buf, packname.len, 1);
		if (!p) {
			ret = error(_("error adding pack %s"), packname.buf);
			goto cleanup;
		}
		if (open_pack_index(p)) {
			ret = error(_("error opening index for %s"), packname.buf);
			goto cleanup;
		}
		for_each_object_in_pack(p, add_packed_commits, ctx,
					FOR_EACH_OBJECT_PACK_ORDER);
		close_pack(p);
		free(p);
	}

cleanup:
	stop_progress(&ctx->progress);
	strbuf_release(&progress_title);
	strbuf_release(&packname);

	return ret;
}

static int fill_oids_from_commits(struct write_commit_graph_context *ctx,
				  struct oidset *commits)
{
	struct oidset_iter iter;
	struct object_id *oid;

	if (!oidset_size(commits))
		return 0;

	oidset_iter_init(commits, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		oid_array_append(&ctx->oids, oid);
	}

	return 0;
}

static void fill_oids_from_all_packs(struct write_commit_graph_context *ctx)
{
	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
			_("Finding commits for commit graph among packed objects"),
			ctx->approx_nr_objects);
	for_each_packed_object(add_packed_commits, ctx,
			       FOR_EACH_OBJECT_PACK_ORDER);
	if (ctx->progress_done < ctx->approx_nr_objects)
		display_progress(ctx->progress, ctx->approx_nr_objects);
	stop_progress(&ctx->progress);
}

static void copy_oids_to_commits(struct write_commit_graph_context *ctx)
{
	uint32_t i;
	enum commit_graph_split_flags flags = ctx->opts ?
		ctx->opts->split_flags : COMMIT_GRAPH_SPLIT_UNSPECIFIED;

	ctx->num_extra_edges = 0;
	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
			_("Finding extra edges in commit graph"),
			ctx->oids.nr);
	oid_array_sort(&ctx->oids);
	for (i = 0; i < ctx->oids.nr; i = oid_array_next_unique(&ctx->oids, i)) {
		unsigned int num_parents;

		display_progress(ctx->progress, i + 1);

		ALLOC_GROW(ctx->commits.list, ctx->commits.nr + 1, ctx->commits.alloc);
		ctx->commits.list[ctx->commits.nr] = lookup_commit(ctx->r, &ctx->oids.oid[i]);

		if (ctx->split && flags != COMMIT_GRAPH_SPLIT_REPLACE &&
		    commit_graph_position(ctx->commits.list[ctx->commits.nr]) != COMMIT_NOT_FROM_GRAPH)
			continue;

		if (ctx->split && flags == COMMIT_GRAPH_SPLIT_REPLACE)
			repo_parse_commit(ctx->r, ctx->commits.list[ctx->commits.nr]);
		else
			repo_parse_commit_no_graph(ctx->r, ctx->commits.list[ctx->commits.nr]);

		num_parents = commit_list_count(ctx->commits.list[ctx->commits.nr]->parents);
		if (num_parents > 2)
			ctx->num_extra_edges += num_parents - 1;

		ctx->commits.nr++;
	}
	stop_progress(&ctx->progress);
}

static int write_graph_chunk_base_1(struct hashfile *f,
				    struct commit_graph *g)
{
	int num = 0;

	if (!g)
		return 0;

	num = write_graph_chunk_base_1(f, g->base_graph);
	hashwrite(f, g->oid.hash, the_hash_algo->rawsz);
	return num + 1;
}

static int write_graph_chunk_base(struct hashfile *f,
				    void *data)
{
	struct write_commit_graph_context *ctx = data;
	int num = write_graph_chunk_base_1(f, ctx->new_base_graph);

	if (num != ctx->num_commit_graphs_after - 1) {
		error(_("failed to write correct number of base graph ids"));
		return -1;
	}

	return 0;
}

static int write_commit_graph_file(struct write_commit_graph_context *ctx)
{
	uint32_t i;
	int fd;
	struct hashfile *f;
	struct lock_file lk = LOCK_INIT;
	const unsigned hashsz = the_hash_algo->rawsz;
	struct strbuf progress_title = STRBUF_INIT;
	struct chunkfile *cf;
	unsigned char file_hash[GIT_MAX_RAWSZ];

	if (ctx->split) {
		struct strbuf tmp_file = STRBUF_INIT;

		strbuf_addf(&tmp_file,
			    "%s/info/commit-graphs/tmp_graph_XXXXXX",
			    ctx->odb->path);
		ctx->graph_name = strbuf_detach(&tmp_file, NULL);
	} else {
		ctx->graph_name = get_commit_graph_filename(ctx->odb);
	}

	if (safe_create_leading_directories(ctx->graph_name)) {
		UNLEAK(ctx->graph_name);
		error(_("unable to create leading directories of %s"),
			ctx->graph_name);
		return -1;
	}

	if (ctx->split) {
		char *lock_name = get_commit_graph_chain_filename(ctx->odb);

		hold_lock_file_for_update_mode(&lk, lock_name,
					       LOCK_DIE_ON_ERROR, 0444);
		free(lock_name);

		fd = git_mkstemp_mode(ctx->graph_name, 0444);
		if (fd < 0) {
			error(_("unable to create temporary graph layer"));
			return -1;
		}

		if (adjust_shared_perm(ctx->graph_name)) {
			error(_("unable to adjust shared permissions for '%s'"),
			      ctx->graph_name);
			return -1;
		}

		f = hashfd(fd, ctx->graph_name);
	} else {
		hold_lock_file_for_update_mode(&lk, ctx->graph_name,
					       LOCK_DIE_ON_ERROR, 0444);
		fd = get_lock_file_fd(&lk);
		f = hashfd(fd, get_lock_file_path(&lk));
	}

	cf = init_chunkfile(f);

	add_chunk(cf, GRAPH_CHUNKID_OIDFANOUT, GRAPH_FANOUT_SIZE,
		  write_graph_chunk_fanout);
	add_chunk(cf, GRAPH_CHUNKID_OIDLOOKUP, hashsz * ctx->commits.nr,
		  write_graph_chunk_oids);
	add_chunk(cf, GRAPH_CHUNKID_DATA, (hashsz + 16) * ctx->commits.nr,
		  write_graph_chunk_data);

	if (ctx->write_generation_data)
		add_chunk(cf, GRAPH_CHUNKID_GENERATION_DATA,
			  sizeof(uint32_t) * ctx->commits.nr,
			  write_graph_chunk_generation_data);
	if (ctx->num_generation_data_overflows)
		add_chunk(cf, GRAPH_CHUNKID_GENERATION_DATA_OVERFLOW,
			  sizeof(timestamp_t) * ctx->num_generation_data_overflows,
			  write_graph_chunk_generation_data_overflow);
	if (ctx->num_extra_edges)
		add_chunk(cf, GRAPH_CHUNKID_EXTRAEDGES,
			  4 * ctx->num_extra_edges,
			  write_graph_chunk_extra_edges);
	if (ctx->changed_paths) {
		add_chunk(cf, GRAPH_CHUNKID_BLOOMINDEXES,
			  sizeof(uint32_t) * ctx->commits.nr,
			  write_graph_chunk_bloom_indexes);
		add_chunk(cf, GRAPH_CHUNKID_BLOOMDATA,
			  sizeof(uint32_t) * 3
				+ ctx->total_bloom_filter_data_size,
			  write_graph_chunk_bloom_data);
	}
	if (ctx->num_commit_graphs_after > 1)
		add_chunk(cf, GRAPH_CHUNKID_BASE,
			  hashsz * (ctx->num_commit_graphs_after - 1),
			  write_graph_chunk_base);

	hashwrite_be32(f, GRAPH_SIGNATURE);

	hashwrite_u8(f, GRAPH_VERSION);
	hashwrite_u8(f, oid_version(the_hash_algo));
	hashwrite_u8(f, get_num_chunks(cf));
	hashwrite_u8(f, ctx->num_commit_graphs_after - 1);

	if (ctx->report_progress) {
		strbuf_addf(&progress_title,
			    Q_("Writing out commit graph in %d pass",
			       "Writing out commit graph in %d passes",
			       get_num_chunks(cf)),
			    get_num_chunks(cf));
		ctx->progress = start_delayed_progress(
			progress_title.buf,
			get_num_chunks(cf) * ctx->commits.nr);
	}

	write_chunkfile(cf, ctx);

	stop_progress(&ctx->progress);
	strbuf_release(&progress_title);

	if (ctx->split && ctx->base_graph_name && ctx->num_commit_graphs_after > 1) {
		char *new_base_hash = xstrdup(oid_to_hex(&ctx->new_base_graph->oid));
		char *new_base_name = get_split_graph_filename(ctx->new_base_graph->odb, new_base_hash);

		free(ctx->commit_graph_filenames_after[ctx->num_commit_graphs_after - 2]);
		free(ctx->commit_graph_hash_after[ctx->num_commit_graphs_after - 2]);
		ctx->commit_graph_filenames_after[ctx->num_commit_graphs_after - 2] = new_base_name;
		ctx->commit_graph_hash_after[ctx->num_commit_graphs_after - 2] = new_base_hash;
	}

	close_commit_graph(ctx->r->objects);
	finalize_hashfile(f, file_hash, FSYNC_COMPONENT_COMMIT_GRAPH,
			  CSUM_HASH_IN_STREAM | CSUM_FSYNC);
	free_chunkfile(cf);

	if (ctx->split) {
		FILE *chainf = fdopen_lock_file(&lk, "w");
		char *final_graph_name;
		int result;

		close(fd);

		if (!chainf) {
			error(_("unable to open commit-graph chain file"));
			return -1;
		}

		if (ctx->base_graph_name) {
			const char *dest;
			int idx = ctx->num_commit_graphs_after - 1;
			if (ctx->num_commit_graphs_after > 1)
				idx--;

			dest = ctx->commit_graph_filenames_after[idx];

			if (strcmp(ctx->base_graph_name, dest)) {
				result = rename(ctx->base_graph_name, dest);

				if (result) {
					error(_("failed to rename base commit-graph file"));
					return -1;
				}
			}
		} else {
			char *graph_name = get_commit_graph_filename(ctx->odb);
			unlink(graph_name);
			free(graph_name);
		}

		ctx->commit_graph_hash_after[ctx->num_commit_graphs_after - 1] = xstrdup(hash_to_hex(file_hash));
		final_graph_name = get_split_graph_filename(ctx->odb,
					ctx->commit_graph_hash_after[ctx->num_commit_graphs_after - 1]);
		ctx->commit_graph_filenames_after[ctx->num_commit_graphs_after - 1] = final_graph_name;

		result = rename(ctx->graph_name, final_graph_name);

		for (i = 0; i < ctx->num_commit_graphs_after; i++)
			fprintf(get_lock_file_fp(&lk), "%s\n", ctx->commit_graph_hash_after[i]);

		if (result) {
			error(_("failed to rename temporary commit-graph file"));
			return -1;
		}
	}

	commit_lock_file(&lk);

	return 0;
}

static void split_graph_merge_strategy(struct write_commit_graph_context *ctx)
{
	struct commit_graph *g;
	uint32_t num_commits;
	enum commit_graph_split_flags flags = COMMIT_GRAPH_SPLIT_UNSPECIFIED;
	uint32_t i;

	int max_commits = 0;
	int size_mult = 2;

	if (ctx->opts) {
		max_commits = ctx->opts->max_commits;

		if (ctx->opts->size_multiple)
			size_mult = ctx->opts->size_multiple;

		flags = ctx->opts->split_flags;
	}

	g = ctx->r->objects->commit_graph;
	num_commits = ctx->commits.nr;
	if (flags == COMMIT_GRAPH_SPLIT_REPLACE)
		ctx->num_commit_graphs_after = 1;
	else
		ctx->num_commit_graphs_after = ctx->num_commit_graphs_before + 1;

	if (flags != COMMIT_GRAPH_SPLIT_MERGE_PROHIBITED &&
	    flags != COMMIT_GRAPH_SPLIT_REPLACE) {
		while (g && (g->num_commits <= size_mult * num_commits ||
			    (max_commits && num_commits > max_commits))) {
			if (g->odb != ctx->odb)
				break;

			num_commits += g->num_commits;
			g = g->base_graph;

			ctx->num_commit_graphs_after--;
		}
	}

	if (flags != COMMIT_GRAPH_SPLIT_REPLACE)
		ctx->new_base_graph = g;
	else if (ctx->num_commit_graphs_after != 1)
		BUG("split_graph_merge_strategy: num_commit_graphs_after "
		    "should be 1 with --split=replace");

	if (ctx->num_commit_graphs_after == 2) {
		char *old_graph_name = get_commit_graph_filename(g->odb);

		if (!strcmp(g->filename, old_graph_name) &&
		    g->odb != ctx->odb) {
			ctx->num_commit_graphs_after = 1;
			ctx->new_base_graph = NULL;
		}

		free(old_graph_name);
	}

	CALLOC_ARRAY(ctx->commit_graph_filenames_after, ctx->num_commit_graphs_after);
	CALLOC_ARRAY(ctx->commit_graph_hash_after, ctx->num_commit_graphs_after);

	for (i = 0; i < ctx->num_commit_graphs_after &&
		    i < ctx->num_commit_graphs_before; i++)
		ctx->commit_graph_filenames_after[i] = xstrdup(ctx->commit_graph_filenames_before[i]);

	i = ctx->num_commit_graphs_before - 1;
	g = ctx->r->objects->commit_graph;

	while (g) {
		if (i < ctx->num_commit_graphs_after)
			ctx->commit_graph_hash_after[i] = xstrdup(oid_to_hex(&g->oid));

		/*
		 * If the topmost remaining layer has generation data chunk, the
		 * resultant layer also has generation data chunk.
		 */
		if (i == ctx->num_commit_graphs_after - 2)
			ctx->write_generation_data = !!g->chunk_generation_data;

		i--;
		g = g->base_graph;
	}
}

static void merge_commit_graph(struct write_commit_graph_context *ctx,
			       struct commit_graph *g)
{
	uint32_t i;
	uint32_t offset = g->num_commits_in_base;

	ALLOC_GROW(ctx->commits.list, ctx->commits.nr + g->num_commits, ctx->commits.alloc);

	for (i = 0; i < g->num_commits; i++) {
		struct object_id oid;
		struct commit *result;

		display_progress(ctx->progress, i + 1);

		load_oid_from_graph(g, i + offset, &oid);

		/* only add commits if they still exist in the repo */
		result = lookup_commit_reference_gently(ctx->r, &oid, 1);

		if (result) {
			ctx->commits.list[ctx->commits.nr] = result;
			ctx->commits.nr++;
		}
	}
}

static int commit_compare(const void *_a, const void *_b)
{
	const struct commit *a = *(const struct commit **)_a;
	const struct commit *b = *(const struct commit **)_b;
	return oidcmp(&a->object.oid, &b->object.oid);
}

static void sort_and_scan_merged_commits(struct write_commit_graph_context *ctx)
{
	uint32_t i, dedup_i = 0;

	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Scanning merged commits"),
					ctx->commits.nr);

	QSORT(ctx->commits.list, ctx->commits.nr, commit_compare);

	ctx->num_extra_edges = 0;
	for (i = 0; i < ctx->commits.nr; i++) {
		display_progress(ctx->progress, i + 1);

		if (i && oideq(&ctx->commits.list[i - 1]->object.oid,
			  &ctx->commits.list[i]->object.oid)) {
			/*
			 * Silently ignore duplicates. These were likely
			 * created due to a commit appearing in multiple
			 * layers of the chain, which is unexpected but
			 * not invalid. We should make sure there is a
			 * unique copy in the new layer.
			 */
		} else {
			unsigned int num_parents;

			ctx->commits.list[dedup_i] = ctx->commits.list[i];
			dedup_i++;

			num_parents = commit_list_count(ctx->commits.list[i]->parents);
			if (num_parents > 2)
				ctx->num_extra_edges += num_parents - 1;
		}
	}

	ctx->commits.nr = dedup_i;

	stop_progress(&ctx->progress);
}

static void merge_commit_graphs(struct write_commit_graph_context *ctx)
{
	struct commit_graph *g = ctx->r->objects->commit_graph;
	uint32_t current_graph_number = ctx->num_commit_graphs_before;

	while (g && current_graph_number >= ctx->num_commit_graphs_after) {
		current_graph_number--;

		if (ctx->report_progress)
			ctx->progress = start_delayed_progress(_("Merging commit-graph"), 0);

		merge_commit_graph(ctx, g);
		stop_progress(&ctx->progress);

		g = g->base_graph;
	}

	if (g) {
		ctx->new_base_graph = g;
		ctx->new_num_commits_in_base = g->num_commits + g->num_commits_in_base;
	}

	if (ctx->new_base_graph)
		ctx->base_graph_name = xstrdup(ctx->new_base_graph->filename);

	sort_and_scan_merged_commits(ctx);
}

static void mark_commit_graphs(struct write_commit_graph_context *ctx)
{
	uint32_t i;
	time_t now = time(NULL);

	for (i = ctx->num_commit_graphs_after - 1; i < ctx->num_commit_graphs_before; i++) {
		struct stat st;
		struct utimbuf updated_time;

		if (stat(ctx->commit_graph_filenames_before[i], &st) < 0)
			continue;

		updated_time.actime = st.st_atime;
		updated_time.modtime = now;
		utime(ctx->commit_graph_filenames_before[i], &updated_time);
	}
}

static void expire_commit_graphs(struct write_commit_graph_context *ctx)
{
	struct strbuf path = STRBUF_INIT;
	DIR *dir;
	struct dirent *de;
	size_t dirnamelen;
	timestamp_t expire_time = time(NULL);

	if (ctx->opts && ctx->opts->expire_time)
		expire_time = ctx->opts->expire_time;
	if (!ctx->split) {
		char *chain_file_name = get_commit_graph_chain_filename(ctx->odb);
		unlink(chain_file_name);
		free(chain_file_name);
		ctx->num_commit_graphs_after = 0;
	}

	strbuf_addstr(&path, ctx->odb->path);
	strbuf_addstr(&path, "/info/commit-graphs");
	dir = opendir(path.buf);

	if (!dir)
		goto out;

	strbuf_addch(&path, '/');
	dirnamelen = path.len;
	while ((de = readdir(dir)) != NULL) {
		struct stat st;
		uint32_t i, found = 0;

		strbuf_setlen(&path, dirnamelen);
		strbuf_addstr(&path, de->d_name);

		if (stat(path.buf, &st) < 0)
			continue;

		if (st.st_mtime > expire_time)
			continue;
		if (path.len < 6 || strcmp(path.buf + path.len - 6, ".graph"))
			continue;

		for (i = 0; i < ctx->num_commit_graphs_after; i++) {
			if (!strcmp(ctx->commit_graph_filenames_after[i],
				    path.buf)) {
				found = 1;
				break;
			}
		}

		if (!found)
			unlink(path.buf);
	}

out:
	if(dir)
		closedir(dir);
	strbuf_release(&path);
}

int write_commit_graph(struct object_directory *odb,
		       const struct string_list *const pack_indexes,
		       struct oidset *commits,
		       enum commit_graph_write_flags flags,
		       const struct commit_graph_opts *opts)
{
	struct repository *r = the_repository;
	struct write_commit_graph_context *ctx;
	uint32_t i;
	int res = 0;
	int replace = 0;
	struct bloom_filter_settings bloom_settings = DEFAULT_BLOOM_FILTER_SETTINGS;
	struct topo_level_slab topo_levels;

	prepare_repo_settings(r);
	if (!r->settings.core_commit_graph) {
		warning(_("attempting to write a commit-graph, but 'core.commitGraph' is disabled"));
		return 0;
	}
	if (!commit_graph_compatible(r))
		return 0;

	CALLOC_ARRAY(ctx, 1);
	ctx->r = r;
	ctx->odb = odb;
	ctx->append = flags & COMMIT_GRAPH_WRITE_APPEND ? 1 : 0;
	ctx->report_progress = flags & COMMIT_GRAPH_WRITE_PROGRESS ? 1 : 0;
	ctx->split = flags & COMMIT_GRAPH_WRITE_SPLIT ? 1 : 0;
	ctx->opts = opts;
	ctx->total_bloom_filter_data_size = 0;
	ctx->write_generation_data = (get_configured_generation_version(r) == 2);
	ctx->num_generation_data_overflows = 0;

	bloom_settings.bits_per_entry = git_env_ulong("GIT_TEST_BLOOM_SETTINGS_BITS_PER_ENTRY",
						      bloom_settings.bits_per_entry);
	bloom_settings.num_hashes = git_env_ulong("GIT_TEST_BLOOM_SETTINGS_NUM_HASHES",
						  bloom_settings.num_hashes);
	bloom_settings.max_changed_paths = git_env_ulong("GIT_TEST_BLOOM_SETTINGS_MAX_CHANGED_PATHS",
							 bloom_settings.max_changed_paths);
	ctx->bloom_settings = &bloom_settings;

	init_topo_level_slab(&topo_levels);
	ctx->topo_levels = &topo_levels;

	prepare_commit_graph(ctx->r);
	if (ctx->r->objects->commit_graph) {
		struct commit_graph *g = ctx->r->objects->commit_graph;

		while (g) {
			g->topo_levels = &topo_levels;
			g = g->base_graph;
		}
	}

	if (flags & COMMIT_GRAPH_WRITE_BLOOM_FILTERS)
		ctx->changed_paths = 1;
	if (!(flags & COMMIT_GRAPH_NO_WRITE_BLOOM_FILTERS)) {
		struct commit_graph *g;

		g = ctx->r->objects->commit_graph;

		/* We have changed-paths already. Keep them in the next graph */
		if (g && g->chunk_bloom_data) {
			ctx->changed_paths = 1;
			ctx->bloom_settings = g->bloom_filter_settings;
		}
	}

	if (ctx->split) {
		struct commit_graph *g = ctx->r->objects->commit_graph;

		while (g) {
			ctx->num_commit_graphs_before++;
			g = g->base_graph;
		}

		if (ctx->num_commit_graphs_before) {
			ALLOC_ARRAY(ctx->commit_graph_filenames_before, ctx->num_commit_graphs_before);
			i = ctx->num_commit_graphs_before;
			g = ctx->r->objects->commit_graph;

			while (g) {
				ctx->commit_graph_filenames_before[--i] = xstrdup(g->filename);
				g = g->base_graph;
			}
		}

		if (ctx->opts)
			replace = ctx->opts->split_flags & COMMIT_GRAPH_SPLIT_REPLACE;
	}

	ctx->approx_nr_objects = repo_approximate_object_count(the_repository);

	if (ctx->append && ctx->r->objects->commit_graph) {
		struct commit_graph *g = ctx->r->objects->commit_graph;
		for (i = 0; i < g->num_commits; i++) {
			struct object_id oid;
			oidread(&oid, g->chunk_oid_lookup + g->hash_len * i);
			oid_array_append(&ctx->oids, &oid);
		}
	}

	if (pack_indexes) {
		ctx->order_by_pack = 1;
		if ((res = fill_oids_from_packs(ctx, pack_indexes)))
			goto cleanup;
	}

	if (commits) {
		if ((res = fill_oids_from_commits(ctx, commits)))
			goto cleanup;
	}

	if (!pack_indexes && !commits) {
		ctx->order_by_pack = 1;
		fill_oids_from_all_packs(ctx);
	}

	close_reachable(ctx);

	copy_oids_to_commits(ctx);

	if (ctx->commits.nr >= GRAPH_EDGE_LAST_MASK) {
		error(_("too many commits to write graph"));
		res = -1;
		goto cleanup;
	}

	if (!ctx->commits.nr && !replace)
		goto cleanup;

	if (ctx->split) {
		split_graph_merge_strategy(ctx);

		if (!replace)
			merge_commit_graphs(ctx);
	} else
		ctx->num_commit_graphs_after = 1;

	ctx->trust_generation_numbers = validate_mixed_generation_chain(ctx->r->objects->commit_graph);

	compute_topological_levels(ctx);
	if (ctx->write_generation_data)
		compute_generation_numbers(ctx);

	if (ctx->changed_paths)
		compute_bloom_filters(ctx);

	res = write_commit_graph_file(ctx);

	if (ctx->split)
		mark_commit_graphs(ctx);

	expire_commit_graphs(ctx);

cleanup:
	free(ctx->graph_name);
	free(ctx->commits.list);
	oid_array_clear(&ctx->oids);
	clear_topo_level_slab(&topo_levels);

	if (ctx->commit_graph_filenames_after) {
		for (i = 0; i < ctx->num_commit_graphs_after; i++) {
			free(ctx->commit_graph_filenames_after[i]);
			free(ctx->commit_graph_hash_after[i]);
		}

		for (i = 0; i < ctx->num_commit_graphs_before; i++)
			free(ctx->commit_graph_filenames_before[i]);

		free(ctx->commit_graph_filenames_after);
		free(ctx->commit_graph_filenames_before);
		free(ctx->commit_graph_hash_after);
	}

	free(ctx);

	return res;
}

#define VERIFY_COMMIT_GRAPH_ERROR_HASH 2
static int verify_commit_graph_error;

__attribute__((format (printf, 1, 2)))
static void graph_report(const char *fmt, ...)
{
	va_list ap;

	verify_commit_graph_error = 1;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

#define GENERATION_ZERO_EXISTS 1
#define GENERATION_NUMBER_EXISTS 2

static int commit_graph_checksum_valid(struct commit_graph *g)
{
	return hashfile_checksum_valid(g->data, g->data_len);
}

int verify_commit_graph(struct repository *r, struct commit_graph *g, int flags)
{
	uint32_t i, cur_fanout_pos = 0;
	struct object_id prev_oid, cur_oid;
	int generation_zero = 0;
	struct progress *progress = NULL;
	int local_error = 0;

	if (!g) {
		graph_report("no commit-graph file loaded");
		return 1;
	}

	verify_commit_graph_error = verify_commit_graph_lite(g);
	if (verify_commit_graph_error)
		return verify_commit_graph_error;

	if (!commit_graph_checksum_valid(g)) {
		graph_report(_("the commit-graph file has incorrect checksum and is likely corrupt"));
		verify_commit_graph_error = VERIFY_COMMIT_GRAPH_ERROR_HASH;
	}

	for (i = 0; i < g->num_commits; i++) {
		struct commit *graph_commit;

		oidread(&cur_oid, g->chunk_oid_lookup + g->hash_len * i);

		if (i && oidcmp(&prev_oid, &cur_oid) >= 0)
			graph_report(_("commit-graph has incorrect OID order: %s then %s"),
				     oid_to_hex(&prev_oid),
				     oid_to_hex(&cur_oid));

		oidcpy(&prev_oid, &cur_oid);

		while (cur_oid.hash[0] > cur_fanout_pos) {
			uint32_t fanout_value = get_be32(g->chunk_oid_fanout + cur_fanout_pos);

			if (i != fanout_value)
				graph_report(_("commit-graph has incorrect fanout value: fanout[%d] = %u != %u"),
					     cur_fanout_pos, fanout_value, i);
			cur_fanout_pos++;
		}

		graph_commit = lookup_commit(r, &cur_oid);
		if (!parse_commit_in_graph_one(r, g, graph_commit))
			graph_report(_("failed to parse commit %s from commit-graph"),
				     oid_to_hex(&cur_oid));
	}

	while (cur_fanout_pos < 256) {
		uint32_t fanout_value = get_be32(g->chunk_oid_fanout + cur_fanout_pos);

		if (g->num_commits != fanout_value)
			graph_report(_("commit-graph has incorrect fanout value: fanout[%d] = %u != %u"),
				     cur_fanout_pos, fanout_value, i);

		cur_fanout_pos++;
	}

	if (verify_commit_graph_error & ~VERIFY_COMMIT_GRAPH_ERROR_HASH)
		return verify_commit_graph_error;

	if (flags & COMMIT_GRAPH_WRITE_PROGRESS)
		progress = start_progress(_("Verifying commits in commit graph"),
					g->num_commits);

	for (i = 0; i < g->num_commits; i++) {
		struct commit *graph_commit, *odb_commit;
		struct commit_list *graph_parents, *odb_parents;
		timestamp_t max_generation = 0;
		timestamp_t generation;

		display_progress(progress, i + 1);
		oidread(&cur_oid, g->chunk_oid_lookup + g->hash_len * i);

		graph_commit = lookup_commit(r, &cur_oid);
		odb_commit = (struct commit *)create_object(r, &cur_oid, alloc_commit_node(r));
		if (repo_parse_commit_internal(r, odb_commit, 0, 0)) {
			graph_report(_("failed to parse commit %s from object database for commit-graph"),
				     oid_to_hex(&cur_oid));
			continue;
		}

		if (!oideq(&get_commit_tree_in_graph_one(r, g, graph_commit)->object.oid,
			   get_commit_tree_oid(odb_commit)))
			graph_report(_("root tree OID for commit %s in commit-graph is %s != %s"),
				     oid_to_hex(&cur_oid),
				     oid_to_hex(get_commit_tree_oid(graph_commit)),
				     oid_to_hex(get_commit_tree_oid(odb_commit)));

		graph_parents = graph_commit->parents;
		odb_parents = odb_commit->parents;

		while (graph_parents) {
			if (!odb_parents) {
				graph_report(_("commit-graph parent list for commit %s is too long"),
					     oid_to_hex(&cur_oid));
				break;
			}

			/* parse parent in case it is in a base graph */
			parse_commit_in_graph_one(r, g, graph_parents->item);

			if (!oideq(&graph_parents->item->object.oid, &odb_parents->item->object.oid))
				graph_report(_("commit-graph parent for %s is %s != %s"),
					     oid_to_hex(&cur_oid),
					     oid_to_hex(&graph_parents->item->object.oid),
					     oid_to_hex(&odb_parents->item->object.oid));

			generation = commit_graph_generation(graph_parents->item);
			if (generation > max_generation)
				max_generation = generation;

			graph_parents = graph_parents->next;
			odb_parents = odb_parents->next;
		}

		if (odb_parents)
			graph_report(_("commit-graph parent list for commit %s terminates early"),
				     oid_to_hex(&cur_oid));

		if (!commit_graph_generation(graph_commit)) {
			if (generation_zero == GENERATION_NUMBER_EXISTS)
				graph_report(_("commit-graph has generation number zero for commit %s, but non-zero elsewhere"),
					     oid_to_hex(&cur_oid));
			generation_zero = GENERATION_ZERO_EXISTS;
		} else if (generation_zero == GENERATION_ZERO_EXISTS)
			graph_report(_("commit-graph has non-zero generation number for commit %s, but zero elsewhere"),
				     oid_to_hex(&cur_oid));

		if (generation_zero == GENERATION_ZERO_EXISTS)
			continue;

		/*
		 * If we are using topological level and one of our parents has
		 * generation GENERATION_NUMBER_V1_MAX, then our generation is
		 * also GENERATION_NUMBER_V1_MAX. Decrement to avoid extra logic
		 * in the following condition.
		 */
		if (!g->read_generation_data && max_generation == GENERATION_NUMBER_V1_MAX)
			max_generation--;

		generation = commit_graph_generation(graph_commit);
		if (generation < max_generation + 1)
			graph_report(_("commit-graph generation for commit %s is %"PRItime" < %"PRItime),
				     oid_to_hex(&cur_oid),
				     generation,
				     max_generation + 1);

		if (graph_commit->date != odb_commit->date)
			graph_report(_("commit date for commit %s in commit-graph is %"PRItime" != %"PRItime),
				     oid_to_hex(&cur_oid),
				     graph_commit->date,
				     odb_commit->date);
	}
	stop_progress(&progress);

	local_error = verify_commit_graph_error;

	if (!(flags & COMMIT_GRAPH_VERIFY_SHALLOW) && g->base_graph)
		local_error |= verify_commit_graph(r, g->base_graph, flags);

	return local_error;
}

void free_commit_graph(struct commit_graph *g)
{
	if (!g)
		return;
	if (g->data) {
		munmap((void *)g->data, g->data_len);
		g->data = NULL;
	}
	free(g->filename);
	free(g->bloom_filter_settings);
	free(g);
}

void disable_commit_graph(struct repository *r)
{
	r->commit_graph_disabled = 1;
}
