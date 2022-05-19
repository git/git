#include "but-compat-util.h"
#include "config.h"
#include "lockfile.h"
#include "pack.h"
#include "packfile.h"
#include "cummit.h"
#include "object.h"
#include "refs.h"
#include "revision.h"
#include "hash-lookup.h"
#include "cummit-graph.h"
#include "object-store.h"
#include "alloc.h"
#include "hashmap.h"
#include "replace-object.h"
#include "progress.h"
#include "bloom.h"
#include "cummit-slab.h"
#include "shallow.h"
#include "json-writer.h"
#include "trace2.h"
#include "chunk-format.h"

void but_test_write_cummit_graph_or_die(void)
{
	int flags = 0;
	if (!but_env_bool(BUT_TEST_CUMMIT_GRAPH, 0))
		return;

	if (but_env_bool(BUT_TEST_CUMMIT_GRAPH_CHANGED_PATHS, 0))
		flags = CUMMIT_GRAPH_WRITE_BLOOM_FILTERS;

	if (write_cummit_graph_reachable(the_repository->objects->odb,
					 flags, NULL))
		die("failed to write cummit-graph under BUT_TEST_CUMMIT_GRAPH");
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

#define CORRECTED_CUMMIT_DATE_OFFSET_OVERFLOW (1ULL << 31)

/* Remember to update object flag allocation in object.h */
#define REACHABLE       (1u<<15)

define_cummit_slab(topo_level_slab, uint32_t);

/* Keep track of the order in which cummits are added to our list. */
define_cummit_slab(cummit_pos, int);
static struct cummit_pos cummit_pos = CUMMIT_SLAB_INIT(1, cummit_pos);

static void set_cummit_pos(struct repository *r, const struct object_id *oid)
{
	static int32_t max_pos;
	struct cummit *cummit = lookup_cummit(r, oid);

	if (!cummit)
		return; /* should never happen, but be lenient */

	*cummit_pos_at(&cummit_pos, cummit) = max_pos++;
}

static int cummit_pos_cmp(const void *va, const void *vb)
{
	const struct cummit *a = *(const struct cummit **)va;
	const struct cummit *b = *(const struct cummit **)vb;
	return cummit_pos_at(&cummit_pos, a) -
	       cummit_pos_at(&cummit_pos, b);
}

define_cummit_slab(cummit_graph_data_slab, struct cummit_graph_data);
static struct cummit_graph_data_slab cummit_graph_data_slab =
	CUMMIT_SLAB_INIT(1, cummit_graph_data_slab);

static int get_configured_generation_version(struct repository *r)
{
	int version = 2;
	repo_config_get_int(r, "cummitgraph.generationversion", &version);
	return version;
}

uint32_t cummit_graph_position(const struct cummit *c)
{
	struct cummit_graph_data *data =
		cummit_graph_data_slab_peek(&cummit_graph_data_slab, c);

	return data ? data->graph_pos : CUMMIT_NOT_FROM_GRAPH;
}

timestamp_t cummit_graph_generation(const struct cummit *c)
{
	struct cummit_graph_data *data =
		cummit_graph_data_slab_peek(&cummit_graph_data_slab, c);

	if (!data)
		return GENERATION_NUMBER_INFINITY;
	else if (data->graph_pos == CUMMIT_NOT_FROM_GRAPH)
		return GENERATION_NUMBER_INFINITY;

	return data->generation;
}

static struct cummit_graph_data *cummit_graph_data_at(const struct cummit *c)
{
	unsigned int i, nth_slab;
	struct cummit_graph_data *data =
		cummit_graph_data_slab_peek(&cummit_graph_data_slab, c);

	if (data)
		return data;

	nth_slab = c->index / cummit_graph_data_slab.slab_size;
	data = cummit_graph_data_slab_at(&cummit_graph_data_slab, c);

	/*
	 * cummit-slab initializes elements with zero, overwrite this with
	 * CUMMIT_NOT_FROM_GRAPH for graph_pos.
	 *
	 * We avoid initializing generation with checking if graph position
	 * is not CUMMIT_NOT_FROM_GRAPH.
	 */
	for (i = 0; i < cummit_graph_data_slab.slab_size; i++) {
		cummit_graph_data_slab.slab[nth_slab][i].graph_pos =
			CUMMIT_NOT_FROM_GRAPH;
	}

	return data;
}

/*
 * Should be used only while writing cummit-graph as it compares
 * generation value of cummits by directly accessing cummit-slab.
 */
static int cummit_gen_cmp(const void *va, const void *vb)
{
	const struct cummit *a = *(const struct cummit **)va;
	const struct cummit *b = *(const struct cummit **)vb;

	const timestamp_t generation_a = cummit_graph_data_at(a)->generation;
	const timestamp_t generation_b = cummit_graph_data_at(b)->generation;
	/* lower generation cummits first */
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

char *get_cummit_graph_filename(struct object_directory *obj_dir)
{
	return xstrfmt("%s/info/cummit-graph", obj_dir->path);
}

static char *get_split_graph_filename(struct object_directory *odb,
				      const char *oid_hex)
{
	return xstrfmt("%s/info/cummit-graphs/graph-%s.graph", odb->path,
		       oid_hex);
}

char *get_cummit_graph_chain_filename(struct object_directory *odb)
{
	return xstrfmt("%s/info/cummit-graphs/cummit-graph-chain", odb->path);
}

static uint8_t oid_version(void)
{
	switch (hash_algo_by_ptr(the_hash_algo)) {
	case BUT_HASH_SHA1:
		return 1;
	case BUT_HASH_SHA256:
		return 2;
	default:
		die(_("invalid hash version"));
	}
}

static struct cummit_graph *alloc_cummit_graph(void)
{
	struct cummit_graph *g = xcalloc(1, sizeof(*g));

	return g;
}

extern int read_replace_refs;

static int cummit_graph_compatible(struct repository *r)
{
	if (!r->butdir)
		return 0;

	if (read_replace_refs) {
		prepare_replace_object(r);
		if (hashmap_get_size(&r->objects->replace_map->map))
			return 0;
	}

	prepare_cummit_graft(r);
	if (r->parsed_objects &&
	    (r->parsed_objects->grafts_nr || r->parsed_objects->substituted_parent))
		return 0;
	if (is_repository_shallow(r))
		return 0;

	return 1;
}

int open_cummit_graph(const char *graph_file, int *fd, struct stat *st)
{
	*fd = but_open(graph_file);
	if (*fd < 0)
		return 0;
	if (fstat(*fd, st)) {
		close(*fd);
		return 0;
	}
	return 1;
}

struct cummit_graph *load_cummit_graph_one_fd_st(struct repository *r,
						 int fd, struct stat *st,
						 struct object_directory *odb)
{
	void *graph_map;
	size_t graph_size;
	struct cummit_graph *ret;

	graph_size = xsize_t(st->st_size);

	if (graph_size < GRAPH_MIN_SIZE) {
		close(fd);
		error(_("cummit-graph file is too small"));
		return NULL;
	}
	graph_map = xmmap(NULL, graph_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	ret = parse_cummit_graph(r, graph_map, graph_size);

	if (ret)
		ret->odb = odb;
	else
		munmap(graph_map, graph_size);

	return ret;
}

static int verify_cummit_graph_lite(struct cummit_graph *g)
{
	/*
	 * Basic validation shared between parse_cummit_graph()
	 * which'll be called every time the graph is used, and the
	 * much more expensive verify_cummit_graph() used by
	 * "cummit-graph verify".
	 *
	 * There should only be very basic checks here to ensure that
	 * we don't e.g. segfault in fill_cummit_in_graph(), but
	 * because this is a very hot codepath nothing that e.g. loops
	 * over g->num_cummits, or runs a checksum on the cummit-graph
	 * itself.
	 */
	if (!g->chunk_oid_fanout) {
		error("cummit-graph is missing the OID Fanout chunk");
		return 1;
	}
	if (!g->chunk_oid_lookup) {
		error("cummit-graph is missing the OID Lookup chunk");
		return 1;
	}
	if (!g->chunk_cummit_data) {
		error("cummit-graph is missing the cummit Data chunk");
		return 1;
	}

	return 0;
}

static int graph_read_oid_lookup(const unsigned char *chunk_start,
				 size_t chunk_size, void *data)
{
	struct cummit_graph *g = data;
	g->chunk_oid_lookup = chunk_start;
	g->num_cummits = chunk_size / g->hash_len;
	return 0;
}

static int graph_read_bloom_data(const unsigned char *chunk_start,
				  size_t chunk_size, void *data)
{
	struct cummit_graph *g = data;
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

struct cummit_graph *parse_cummit_graph(struct repository *r,
					void *graph_map, size_t graph_size)
{
	const unsigned char *data;
	struct cummit_graph *graph;
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
		error(_("cummit-graph signature %X does not match signature %X"),
		      graph_signature, GRAPH_SIGNATURE);
		return NULL;
	}

	graph_version = *(unsigned char*)(data + 4);
	if (graph_version != GRAPH_VERSION) {
		error(_("cummit-graph version %X does not match version %X"),
		      graph_version, GRAPH_VERSION);
		return NULL;
	}

	hash_version = *(unsigned char*)(data + 5);
	if (hash_version != oid_version()) {
		error(_("cummit-graph hash version %X does not match version %X"),
		      hash_version, oid_version());
		return NULL;
	}

	prepare_repo_settings(r);

	graph = alloc_cummit_graph();

	graph->hash_len = the_hash_algo->rawsz;
	graph->num_chunks = *(unsigned char*)(data + 6);
	graph->data = graph_map;
	graph->data_len = graph_size;

	if (graph_size < GRAPH_HEADER_SIZE +
			 (graph->num_chunks + 1) * CHUNK_TOC_ENTRY_SIZE +
			 GRAPH_FANOUT_SIZE + the_hash_algo->rawsz) {
		error(_("cummit-graph file is too small to hold %u chunks"),
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
	pair_chunk(cf, GRAPH_CHUNKID_DATA, &graph->chunk_cummit_data);
	pair_chunk(cf, GRAPH_CHUNKID_EXTRAEDGES, &graph->chunk_extra_edges);
	pair_chunk(cf, GRAPH_CHUNKID_BASE, &graph->chunk_base_graphs);

	if (get_configured_generation_version(r) >= 2) {
		pair_chunk(cf, GRAPH_CHUNKID_GENERATION_DATA,
			&graph->chunk_generation_data);
		pair_chunk(cf, GRAPH_CHUNKID_GENERATION_DATA_OVERFLOW,
			&graph->chunk_generation_data_overflow);

		if (graph->chunk_generation_data)
			graph->read_generation_data = 1;
	}

	if (r->settings.cummit_graph_read_changed_paths) {
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

	if (verify_cummit_graph_lite(graph))
		goto free_and_return;

	free_chunkfile(cf);
	return graph;

free_and_return:
	free_chunkfile(cf);
	free(graph->bloom_filter_settings);
	free(graph);
	return NULL;
}

static struct cummit_graph *load_cummit_graph_one(struct repository *r,
						  const char *graph_file,
						  struct object_directory *odb)
{

	struct stat st;
	int fd;
	struct cummit_graph *g;
	int open_ok = open_cummit_graph(graph_file, &fd, &st);

	if (!open_ok)
		return NULL;

	g = load_cummit_graph_one_fd_st(r, fd, &st, odb);

	if (g)
		g->filename = xstrdup(graph_file);

	return g;
}

static struct cummit_graph *load_cummit_graph_v1(struct repository *r,
						 struct object_directory *odb)
{
	char *graph_name = get_cummit_graph_filename(odb);
	struct cummit_graph *g = load_cummit_graph_one(r, graph_name, odb);
	free(graph_name);

	return g;
}

static int add_graph_to_chain(struct cummit_graph *g,
			      struct cummit_graph *chain,
			      struct object_id *oids,
			      int n)
{
	struct cummit_graph *cur_g = chain;

	if (n && !g->chunk_base_graphs) {
		warning(_("cummit-graph has no base graphs chunk"));
		return 0;
	}

	while (n) {
		n--;

		if (!cur_g ||
		    !oideq(&oids[n], &cur_g->oid) ||
		    !hasheq(oids[n].hash, g->chunk_base_graphs + g->hash_len * n)) {
			warning(_("cummit-graph chain does not match"));
			return 0;
		}

		cur_g = cur_g->base_graph;
	}

	g->base_graph = chain;

	if (chain)
		g->num_cummits_in_base = chain->num_cummits + chain->num_cummits_in_base;

	return 1;
}

static struct cummit_graph *load_cummit_graph_chain(struct repository *r,
						    struct object_directory *odb)
{
	struct cummit_graph *graph_chain = NULL;
	struct strbuf line = STRBUF_INIT;
	struct stat st;
	struct object_id *oids;
	int i = 0, valid = 1, count;
	char *chain_name = get_cummit_graph_chain_filename(odb);
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
			warning(_("invalid cummit-graph chain: line '%s' not a hash"),
				line.buf);
			valid = 0;
			break;
		}

		valid = 0;
		for (odb = r->objects->odb; odb; odb = odb->next) {
			char *graph_name = get_split_graph_filename(odb, line.buf);
			struct cummit_graph *g = load_cummit_graph_one(r, graph_name, odb);

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
			warning(_("unable to find all cummit-graph files"));
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
 * corrected cummit dates stored in the generation_data chunk.
 */
static int validate_mixed_generation_chain(struct cummit_graph *g)
{
	int read_generation_data = 1;
	struct cummit_graph *p = g;

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

struct cummit_graph *read_cummit_graph_one(struct repository *r,
					   struct object_directory *odb)
{
	struct cummit_graph *g = load_cummit_graph_v1(r, odb);

	if (!g)
		g = load_cummit_graph_chain(r, odb);

	validate_mixed_generation_chain(g);

	return g;
}

static void prepare_cummit_graph_one(struct repository *r,
				     struct object_directory *odb)
{

	if (r->objects->cummit_graph)
		return;

	r->objects->cummit_graph = read_cummit_graph_one(r, odb);
}

/*
 * Return 1 if cummit_graph is non-NULL, and 0 otherwise.
 *
 * On the first invocation, this function attempts to load the cummit
 * graph if the_repository is configured to have one.
 */
static int prepare_cummit_graph(struct repository *r)
{
	struct object_directory *odb;

	/*
	 * Early return if there is no but dir or if the cummit graph is
	 * disabled.
	 *
	 * This must come before the "already attempted?" check below, because
	 * we want to disable even an already-loaded graph file.
	 */
	if (!r->butdir || r->cummit_graph_disabled)
		return 0;

	if (r->objects->cummit_graph_attempted)
		return !!r->objects->cummit_graph;
	r->objects->cummit_graph_attempted = 1;

	prepare_repo_settings(r);

	if (!but_env_bool(BUT_TEST_CUMMIT_GRAPH, 0) &&
	    r->settings.core_cummit_graph != 1)
		/*
		 * This repository is not configured to use cummit graphs, so
		 * do not load one. (But report cummit_graph_attempted anyway
		 * so that cummit graph loading is not attempted again for this
		 * repository.)
		 */
		return 0;

	if (!cummit_graph_compatible(r))
		return 0;

	prepare_alt_odb(r);
	for (odb = r->objects->odb;
	     !r->objects->cummit_graph && odb;
	     odb = odb->next)
		prepare_cummit_graph_one(r, odb);
	return !!r->objects->cummit_graph;
}

int generation_numbers_enabled(struct repository *r)
{
	uint32_t first_generation;
	struct cummit_graph *g;
	if (!prepare_cummit_graph(r))
	       return 0;

	g = r->objects->cummit_graph;

	if (!g->num_cummits)
		return 0;

	first_generation = get_be32(g->chunk_cummit_data +
				    g->hash_len + 8) >> 2;

	return !!first_generation;
}

int corrected_cummit_dates_enabled(struct repository *r)
{
	struct cummit_graph *g;
	if (!prepare_cummit_graph(r))
		return 0;

	g = r->objects->cummit_graph;

	if (!g->num_cummits)
		return 0;

	return g->read_generation_data;
}

struct bloom_filter_settings *get_bloom_filter_settings(struct repository *r)
{
	struct cummit_graph *g = r->objects->cummit_graph;
	while (g) {
		if (g->bloom_filter_settings)
			return g->bloom_filter_settings;
		g = g->base_graph;
	}
	return NULL;
}

static void close_cummit_graph_one(struct cummit_graph *g)
{
	if (!g)
		return;

	clear_cummit_graph_data_slab(&cummit_graph_data_slab);
	close_cummit_graph_one(g->base_graph);
	free_cummit_graph(g);
}

void close_cummit_graph(struct raw_object_store *o)
{
	close_cummit_graph_one(o->cummit_graph);
	o->cummit_graph = NULL;
}

static int bsearch_graph(struct cummit_graph *g, const struct object_id *oid, uint32_t *pos)
{
	return bsearch_hash(oid->hash, g->chunk_oid_fanout,
			    g->chunk_oid_lookup, g->hash_len, pos);
}

static void load_oid_from_graph(struct cummit_graph *g,
				uint32_t pos,
				struct object_id *oid)
{
	uint32_t lex_index;

	while (g && pos < g->num_cummits_in_base)
		g = g->base_graph;

	if (!g)
		BUG("NULL cummit-graph");

	if (pos >= g->num_cummits + g->num_cummits_in_base)
		die(_("invalid cummit position. cummit-graph is likely corrupt"));

	lex_index = pos - g->num_cummits_in_base;

	oidread(oid, g->chunk_oid_lookup + g->hash_len * lex_index);
}

static struct cummit_list **insert_parent_or_die(struct repository *r,
						 struct cummit_graph *g,
						 uint32_t pos,
						 struct cummit_list **pptr)
{
	struct cummit *c;
	struct object_id oid;

	if (pos >= g->num_cummits + g->num_cummits_in_base)
		die("invalid parent position %"PRIu32, pos);

	load_oid_from_graph(g, pos, &oid);
	c = lookup_cummit(r, &oid);
	if (!c)
		die(_("could not find cummit %s"), oid_to_hex(&oid));
	cummit_graph_data_at(c)->graph_pos = pos;
	return &cummit_list_insert(c, pptr)->next;
}

static void fill_cummit_graph_info(struct cummit *item, struct cummit_graph *g, uint32_t pos)
{
	const unsigned char *cummit_data;
	struct cummit_graph_data *graph_data;
	uint32_t lex_index, offset_pos;
	uint64_t date_high, date_low, offset;

	while (pos < g->num_cummits_in_base)
		g = g->base_graph;

	if (pos >= g->num_cummits + g->num_cummits_in_base)
		die(_("invalid cummit position. cummit-graph is likely corrupt"));

	lex_index = pos - g->num_cummits_in_base;
	cummit_data = g->chunk_cummit_data + GRAPH_DATA_WIDTH * lex_index;

	graph_data = cummit_graph_data_at(item);
	graph_data->graph_pos = pos;

	date_high = get_be32(cummit_data + g->hash_len + 8) & 0x3;
	date_low = get_be32(cummit_data + g->hash_len + 12);
	item->date = (timestamp_t)((date_high << 32) | date_low);

	if (g->read_generation_data) {
		offset = (timestamp_t)get_be32(g->chunk_generation_data + sizeof(uint32_t) * lex_index);

		if (offset & CORRECTED_CUMMIT_DATE_OFFSET_OVERFLOW) {
			if (!g->chunk_generation_data_overflow)
				die(_("cummit-graph requires overflow generation data but has none"));

			offset_pos = offset ^ CORRECTED_CUMMIT_DATE_OFFSET_OVERFLOW;
			graph_data->generation = item->date + get_be64(g->chunk_generation_data_overflow + 8 * offset_pos);
		} else
			graph_data->generation = item->date + offset;
	} else
		graph_data->generation = get_be32(cummit_data + g->hash_len + 8) >> 2;

	if (g->topo_levels)
		*topo_level_slab_at(g->topo_levels, item) = get_be32(cummit_data + g->hash_len + 8) >> 2;
}

static inline void set_cummit_tree(struct cummit *c, struct tree *t)
{
	c->maybe_tree = t;
}

static int fill_cummit_in_graph(struct repository *r,
				struct cummit *item,
				struct cummit_graph *g, uint32_t pos)
{
	uint32_t edge_value;
	uint32_t *parent_data_ptr;
	struct cummit_list **pptr;
	const unsigned char *cummit_data;
	uint32_t lex_index;

	while (pos < g->num_cummits_in_base)
		g = g->base_graph;

	fill_cummit_graph_info(item, g, pos);

	lex_index = pos - g->num_cummits_in_base;
	cummit_data = g->chunk_cummit_data + (g->hash_len + 16) * lex_index;

	item->object.parsed = 1;

	set_cummit_tree(item, NULL);

	pptr = &item->parents;

	edge_value = get_be32(cummit_data + g->hash_len);
	if (edge_value == GRAPH_PARENT_NONE)
		return 1;
	pptr = insert_parent_or_die(r, g, edge_value, pptr);

	edge_value = get_be32(cummit_data + g->hash_len + 4);
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

static int search_cummit_pos_in_graph(const struct object_id *id, struct cummit_graph *g, uint32_t *pos)
{
	struct cummit_graph *cur_g = g;
	uint32_t lex_index;

	while (cur_g && !bsearch_graph(cur_g, id, &lex_index))
		cur_g = cur_g->base_graph;

	if (cur_g) {
		*pos = lex_index + cur_g->num_cummits_in_base;
		return 1;
	}

	return 0;
}

static int find_cummit_pos_in_graph(struct cummit *item, struct cummit_graph *g, uint32_t *pos)
{
	uint32_t graph_pos = cummit_graph_position(item);
	if (graph_pos != CUMMIT_NOT_FROM_GRAPH) {
		*pos = graph_pos;
		return 1;
	} else {
		return search_cummit_pos_in_graph(&item->object.oid, g, pos);
	}
}

struct cummit *lookup_cummit_in_graph(struct repository *repo, const struct object_id *id)
{
	struct cummit *cummit;
	uint32_t pos;

	if (!repo->objects->cummit_graph)
		return NULL;
	if (!search_cummit_pos_in_graph(id, repo->objects->cummit_graph, &pos))
		return NULL;
	if (!repo_has_object_file(repo, id))
		return NULL;

	cummit = lookup_cummit(repo, id);
	if (!cummit)
		return NULL;
	if (cummit->object.parsed)
		return cummit;

	if (!fill_cummit_in_graph(repo, cummit, repo->objects->cummit_graph, pos))
		return NULL;

	return cummit;
}

static int parse_cummit_in_graph_one(struct repository *r,
				     struct cummit_graph *g,
				     struct cummit *item)
{
	uint32_t pos;

	if (item->object.parsed)
		return 1;

	if (find_cummit_pos_in_graph(item, g, &pos))
		return fill_cummit_in_graph(r, item, g, pos);

	return 0;
}

int parse_cummit_in_graph(struct repository *r, struct cummit *item)
{
	static int checked_env = 0;

	if (!checked_env &&
	    but_env_bool(BUT_TEST_CUMMIT_GRAPH_DIE_ON_PARSE, 0))
		die("dying as requested by the '%s' variable on cummit-graph parse!",
		    BUT_TEST_CUMMIT_GRAPH_DIE_ON_PARSE);
	checked_env = 1;

	if (!prepare_cummit_graph(r))
		return 0;
	return parse_cummit_in_graph_one(r, r->objects->cummit_graph, item);
}

void load_cummit_graph_info(struct repository *r, struct cummit *item)
{
	uint32_t pos;
	if (!prepare_cummit_graph(r))
		return;
	if (find_cummit_pos_in_graph(item, r->objects->cummit_graph, &pos))
		fill_cummit_graph_info(item, r->objects->cummit_graph, pos);
}

static struct tree *load_tree_for_cummit(struct repository *r,
					 struct cummit_graph *g,
					 struct cummit *c)
{
	struct object_id oid;
	const unsigned char *cummit_data;
	uint32_t graph_pos = cummit_graph_position(c);

	while (graph_pos < g->num_cummits_in_base)
		g = g->base_graph;

	cummit_data = g->chunk_cummit_data +
			GRAPH_DATA_WIDTH * (graph_pos - g->num_cummits_in_base);

	oidread(&oid, cummit_data);
	set_cummit_tree(c, lookup_tree(r, &oid));

	return c->maybe_tree;
}

static struct tree *get_cummit_tree_in_graph_one(struct repository *r,
						 struct cummit_graph *g,
						 const struct cummit *c)
{
	if (c->maybe_tree)
		return c->maybe_tree;
	if (cummit_graph_position(c) == CUMMIT_NOT_FROM_GRAPH)
		BUG("get_cummit_tree_in_graph_one called from non-cummit-graph cummit");

	return load_tree_for_cummit(r, g, (struct cummit *)c);
}

struct tree *get_cummit_tree_in_graph(struct repository *r, const struct cummit *c)
{
	return get_cummit_tree_in_graph_one(r, r->objects->cummit_graph, c);
}

struct packed_cummit_list {
	struct cummit **list;
	size_t nr;
	size_t alloc;
};

struct write_cummit_graph_context {
	struct repository *r;
	struct object_directory *odb;
	char *graph_name;
	struct oid_array oids;
	struct packed_cummit_list cummits;
	int num_extra_edges;
	int num_generation_data_overflows;
	unsigned long approx_nr_objects;
	struct progress *progress;
	int progress_done;
	uint64_t progress_cnt;

	char *base_graph_name;
	int num_cummit_graphs_before;
	int num_cummit_graphs_after;
	char **cummit_graph_filenames_before;
	char **cummit_graph_filenames_after;
	char **cummit_graph_hash_after;
	uint32_t new_num_cummits_in_base;
	struct cummit_graph *new_base_graph;

	unsigned append:1,
		 report_progress:1,
		 split:1,
		 changed_paths:1,
		 order_by_pack:1,
		 write_generation_data:1,
		 trust_generation_numbers:1;

	struct topo_level_slab *topo_levels;
	const struct cummit_graph_opts *opts;
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
	struct write_cummit_graph_context *ctx = data;
	int i, count = 0;
	struct cummit **list = ctx->cummits.list;

	/*
	 * Write the first-level table (the list is sorted,
	 * but we use a 256-entry lookup to be able to avoid
	 * having to do eight extra binary search iterations).
	 */
	for (i = 0; i < 256; i++) {
		while (count < ctx->cummits.nr) {
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
	struct write_cummit_graph_context *ctx = data;
	struct cummit **list = ctx->cummits.list;
	int count;
	for (count = 0; count < ctx->cummits.nr; count++, list++) {
		display_progress(ctx->progress, ++ctx->progress_cnt);
		hashwrite(f, (*list)->object.oid.hash, the_hash_algo->rawsz);
	}

	return 0;
}

static const struct object_id *cummit_to_oid(size_t index, const void *table)
{
	const struct cummit * const *cummits = table;
	return &cummits[index]->object.oid;
}

static int write_graph_chunk_data(struct hashfile *f,
				  void *data)
{
	struct write_cummit_graph_context *ctx = data;
	struct cummit **list = ctx->cummits.list;
	struct cummit **last = ctx->cummits.list + ctx->cummits.nr;
	uint32_t num_extra_edges = 0;

	while (list < last) {
		struct cummit_list *parent;
		struct object_id *tree;
		int edge_value;
		uint32_t packedDate[2];
		display_progress(ctx->progress, ++ctx->progress_cnt);

		if (repo_parse_cummit_no_graph(ctx->r, *list))
			die(_("unable to parse cummit %s"),
				oid_to_hex(&(*list)->object.oid));
		tree = get_cummit_tree_oid(*list);
		hashwrite(f, tree->hash, the_hash_algo->rawsz);

		parent = (*list)->parents;

		if (!parent)
			edge_value = GRAPH_PARENT_NONE;
		else {
			edge_value = oid_pos(&parent->item->object.oid,
					     ctx->cummits.list,
					     ctx->cummits.nr,
					     cummit_to_oid);

			if (edge_value >= 0)
				edge_value += ctx->new_num_cummits_in_base;
			else if (ctx->new_base_graph) {
				uint32_t pos;
				if (find_cummit_pos_in_graph(parent->item,
							     ctx->new_base_graph,
							     &pos))
					edge_value = pos;
			}

			if (edge_value < 0)
				BUG("missing parent %s for cummit %s",
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
					     ctx->cummits.list,
					     ctx->cummits.nr,
					     cummit_to_oid);

			if (edge_value >= 0)
				edge_value += ctx->new_num_cummits_in_base;
			else if (ctx->new_base_graph) {
				uint32_t pos;
				if (find_cummit_pos_in_graph(parent->item,
							     ctx->new_base_graph,
							     &pos))
					edge_value = pos;
			}

			if (edge_value < 0)
				BUG("missing parent %s for cummit %s",
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
	struct write_cummit_graph_context *ctx = data;
	int i, num_generation_data_overflows = 0;

	for (i = 0; i < ctx->cummits.nr; i++) {
		struct cummit *c = ctx->cummits.list[i];
		timestamp_t offset;
		repo_parse_cummit(ctx->r, c);
		offset = cummit_graph_data_at(c)->generation - c->date;
		display_progress(ctx->progress, ++ctx->progress_cnt);

		if (offset > GENERATION_NUMBER_V2_OFFSET_MAX) {
			offset = CORRECTED_CUMMIT_DATE_OFFSET_OVERFLOW | num_generation_data_overflows;
			num_generation_data_overflows++;
		}

		hashwrite_be32(f, offset);
	}

	return 0;
}

static int write_graph_chunk_generation_data_overflow(struct hashfile *f,
						      void *data)
{
	struct write_cummit_graph_context *ctx = data;
	int i;
	for (i = 0; i < ctx->cummits.nr; i++) {
		struct cummit *c = ctx->cummits.list[i];
		timestamp_t offset = cummit_graph_data_at(c)->generation - c->date;
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
	struct write_cummit_graph_context *ctx = data;
	struct cummit **list = ctx->cummits.list;
	struct cummit **last = ctx->cummits.list + ctx->cummits.nr;
	struct cummit_list *parent;

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
						 ctx->cummits.list,
						 ctx->cummits.nr,
						 cummit_to_oid);

			if (edge_value >= 0)
				edge_value += ctx->new_num_cummits_in_base;
			else if (ctx->new_base_graph) {
				uint32_t pos;
				if (find_cummit_pos_in_graph(parent->item,
							     ctx->new_base_graph,
							     &pos))
					edge_value = pos;
			}

			if (edge_value < 0)
				BUG("missing parent %s for cummit %s",
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
	struct write_cummit_graph_context *ctx = data;
	struct cummit **list = ctx->cummits.list;
	struct cummit **last = ctx->cummits.list + ctx->cummits.nr;
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

static void trace2_bloom_filter_settings(struct write_cummit_graph_context *ctx)
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
	struct write_cummit_graph_context *ctx = data;
	struct cummit **list = ctx->cummits.list;
	struct cummit **last = ctx->cummits.list + ctx->cummits.nr;

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

static int add_packed_cummits(const struct object_id *oid,
			      struct packed_but *pack,
			      uint32_t pos,
			      void *data)
{
	struct write_cummit_graph_context *ctx = (struct write_cummit_graph_context*)data;
	enum object_type type;
	off_t offset = nth_packed_object_offset(pack, pos);
	struct object_info oi = OBJECT_INFO_INIT;

	if (ctx->progress)
		display_progress(ctx->progress, ++ctx->progress_done);

	oi.typep = &type;
	if (packed_object_info(ctx->r, pack, offset, &oi) < 0)
		die(_("unable to get type of object %s"), oid_to_hex(oid));

	if (type != OBJ_CUMMIT)
		return 0;

	oid_array_append(&ctx->oids, oid);
	set_cummit_pos(ctx->r, oid);

	return 0;
}

static void add_missing_parents(struct write_cummit_graph_context *ctx, struct cummit *cummit)
{
	struct cummit_list *parent;
	for (parent = cummit->parents; parent; parent = parent->next) {
		if (!(parent->item->object.flags & REACHABLE)) {
			oid_array_append(&ctx->oids, &parent->item->object.oid);
			parent->item->object.flags |= REACHABLE;
		}
	}
}

static void close_reachable(struct write_cummit_graph_context *ctx)
{
	int i;
	struct cummit *cummit;
	enum cummit_graph_split_flags flags = ctx->opts ?
		ctx->opts->split_flags : CUMMIT_GRAPH_SPLIT_UNSPECIFIED;

	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Loading known cummits in cummit graph"),
					ctx->oids.nr);
	for (i = 0; i < ctx->oids.nr; i++) {
		display_progress(ctx->progress, i + 1);
		cummit = lookup_cummit(ctx->r, &ctx->oids.oid[i]);
		if (cummit)
			cummit->object.flags |= REACHABLE;
	}
	stop_progress(&ctx->progress);

	/*
	 * As this loop runs, ctx->oids.nr may grow, but not more
	 * than the number of missing cummits in the reachable
	 * closure.
	 */
	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Expanding reachable cummits in cummit graph"),
					0);
	for (i = 0; i < ctx->oids.nr; i++) {
		display_progress(ctx->progress, i + 1);
		cummit = lookup_cummit(ctx->r, &ctx->oids.oid[i]);

		if (!cummit)
			continue;
		if (ctx->split) {
			if ((!repo_parse_cummit(ctx->r, cummit) &&
			     cummit_graph_position(cummit) == CUMMIT_NOT_FROM_GRAPH) ||
			    flags == CUMMIT_GRAPH_SPLIT_REPLACE)
				add_missing_parents(ctx, cummit);
		} else if (!repo_parse_cummit_no_graph(ctx->r, cummit))
			add_missing_parents(ctx, cummit);
	}
	stop_progress(&ctx->progress);

	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Clearing cummit marks in cummit graph"),
					ctx->oids.nr);
	for (i = 0; i < ctx->oids.nr; i++) {
		display_progress(ctx->progress, i + 1);
		cummit = lookup_cummit(ctx->r, &ctx->oids.oid[i]);

		if (cummit)
			cummit->object.flags &= ~REACHABLE;
	}
	stop_progress(&ctx->progress);
}

static void compute_topological_levels(struct write_cummit_graph_context *ctx)
{
	int i;
	struct cummit_list *list = NULL;

	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Computing cummit graph topological levels"),
					ctx->cummits.nr);
	for (i = 0; i < ctx->cummits.nr; i++) {
		struct cummit *c = ctx->cummits.list[i];
		uint32_t level;

		repo_parse_cummit(ctx->r, c);
		level = *topo_level_slab_at(ctx->topo_levels, c);

		display_progress(ctx->progress, i + 1);
		if (level != GENERATION_NUMBER_ZERO)
			continue;

		cummit_list_insert(c, &list);
		while (list) {
			struct cummit *current = list->item;
			struct cummit_list *parent;
			int all_parents_computed = 1;
			uint32_t max_level = 0;

			for (parent = current->parents; parent; parent = parent->next) {
				repo_parse_cummit(ctx->r, parent->item);
				level = *topo_level_slab_at(ctx->topo_levels, parent->item);

				if (level == GENERATION_NUMBER_ZERO) {
					all_parents_computed = 0;
					cummit_list_insert(parent->item, &list);
					break;
				}

				if (level > max_level)
					max_level = level;
			}

			if (all_parents_computed) {
				pop_cummit(&list);

				if (max_level > GENERATION_NUMBER_V1_MAX - 1)
					max_level = GENERATION_NUMBER_V1_MAX - 1;
				*topo_level_slab_at(ctx->topo_levels, current) = max_level + 1;
			}
		}
	}
	stop_progress(&ctx->progress);
}

static void compute_generation_numbers(struct write_cummit_graph_context *ctx)
{
	int i;
	struct cummit_list *list = NULL;

	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Computing cummit graph generation numbers"),
					ctx->cummits.nr);

	if (!ctx->trust_generation_numbers) {
		for (i = 0; i < ctx->cummits.nr; i++) {
			struct cummit *c = ctx->cummits.list[i];
			repo_parse_cummit(ctx->r, c);
			cummit_graph_data_at(c)->generation = GENERATION_NUMBER_ZERO;
		}
	}

	for (i = 0; i < ctx->cummits.nr; i++) {
		struct cummit *c = ctx->cummits.list[i];
		timestamp_t corrected_cummit_date;

		repo_parse_cummit(ctx->r, c);
		corrected_cummit_date = cummit_graph_data_at(c)->generation;

		display_progress(ctx->progress, i + 1);
		if (corrected_cummit_date != GENERATION_NUMBER_ZERO)
			continue;

		cummit_list_insert(c, &list);
		while (list) {
			struct cummit *current = list->item;
			struct cummit_list *parent;
			int all_parents_computed = 1;
			timestamp_t max_corrected_cummit_date = 0;

			for (parent = current->parents; parent; parent = parent->next) {
				repo_parse_cummit(ctx->r, parent->item);
				corrected_cummit_date = cummit_graph_data_at(parent->item)->generation;

				if (corrected_cummit_date == GENERATION_NUMBER_ZERO) {
					all_parents_computed = 0;
					cummit_list_insert(parent->item, &list);
					break;
				}

				if (corrected_cummit_date > max_corrected_cummit_date)
					max_corrected_cummit_date = corrected_cummit_date;
			}

			if (all_parents_computed) {
				pop_cummit(&list);

				if (current->date && current->date > max_corrected_cummit_date)
					max_corrected_cummit_date = current->date - 1;
				cummit_graph_data_at(current)->generation = max_corrected_cummit_date + 1;
			}
		}
	}

	for (i = 0; i < ctx->cummits.nr; i++) {
		struct cummit *c = ctx->cummits.list[i];
		timestamp_t offset = cummit_graph_data_at(c)->generation - c->date;
		if (offset > GENERATION_NUMBER_V2_OFFSET_MAX)
			ctx->num_generation_data_overflows++;
	}
	stop_progress(&ctx->progress);
}

static void trace2_bloom_filter_write_statistics(struct write_cummit_graph_context *ctx)
{
	trace2_data_intmax("cummit-graph", ctx->r, "filter-computed",
			   ctx->count_bloom_filter_computed);
	trace2_data_intmax("cummit-graph", ctx->r, "filter-not-computed",
			   ctx->count_bloom_filter_not_computed);
	trace2_data_intmax("cummit-graph", ctx->r, "filter-trunc-empty",
			   ctx->count_bloom_filter_trunc_empty);
	trace2_data_intmax("cummit-graph", ctx->r, "filter-trunc-large",
			   ctx->count_bloom_filter_trunc_large);
}

static void compute_bloom_filters(struct write_cummit_graph_context *ctx)
{
	int i;
	struct progress *progress = NULL;
	struct cummit **sorted_cummits;
	int max_new_filters;

	init_bloom_filters();

	if (ctx->report_progress)
		progress = start_delayed_progress(
			_("Computing cummit changed paths Bloom filters"),
			ctx->cummits.nr);

	ALLOC_ARRAY(sorted_cummits, ctx->cummits.nr);
	COPY_ARRAY(sorted_cummits, ctx->cummits.list, ctx->cummits.nr);

	if (ctx->order_by_pack)
		QSORT(sorted_cummits, ctx->cummits.nr, cummit_pos_cmp);
	else
		QSORT(sorted_cummits, ctx->cummits.nr, cummit_gen_cmp);

	max_new_filters = ctx->opts && ctx->opts->max_new_filters >= 0 ?
		ctx->opts->max_new_filters : ctx->cummits.nr;

	for (i = 0; i < ctx->cummits.nr; i++) {
		enum bloom_filter_computed computed = 0;
		struct cummit *c = sorted_cummits[i];
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

	free(sorted_cummits);
	stop_progress(&progress);
}

struct refs_cb_data {
	struct oidset *cummits;
	struct progress *progress;
};

static int add_ref_to_set(const char *refname,
			  const struct object_id *oid,
			  int flags, void *cb_data)
{
	struct object_id peeled;
	struct refs_cb_data *data = (struct refs_cb_data *)cb_data;

	if (!peel_iterated_oid(oid, &peeled))
		oid = &peeled;
	if (oid_object_info(the_repository, oid, NULL) == OBJ_CUMMIT)
		oidset_insert(data->cummits, oid);

	display_progress(data->progress, oidset_size(data->cummits));

	return 0;
}

int write_cummit_graph_reachable(struct object_directory *odb,
				 enum cummit_graph_write_flags flags,
				 const struct cummit_graph_opts *opts)
{
	struct oidset cummits = OIDSET_INIT;
	struct refs_cb_data data;
	int result;

	memset(&data, 0, sizeof(data));
	data.cummits = &cummits;
	if (flags & CUMMIT_GRAPH_WRITE_PROGRESS)
		data.progress = start_delayed_progress(
			_("Collecting referenced cummits"), 0);

	for_each_ref(add_ref_to_set, &data);

	stop_progress(&data.progress);

	result = write_cummit_graph(odb, NULL, &cummits,
				    flags, opts);

	oidset_clear(&cummits);
	return result;
}

static int fill_oids_from_packs(struct write_cummit_graph_context *ctx,
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
			    Q_("Finding cummits for cummit graph in %"PRIuMAX" pack",
			       "Finding cummits for cummit graph in %"PRIuMAX" packs",
			       pack_indexes->nr),
			    (uintmax_t)pack_indexes->nr);
		ctx->progress = start_delayed_progress(progress_title.buf, 0);
		ctx->progress_done = 0;
	}
	for (i = 0; i < pack_indexes->nr; i++) {
		struct packed_but *p;
		strbuf_setlen(&packname, dirlen);
		strbuf_addstr(&packname, pack_indexes->items[i].string);
		p = add_packed_but(packname.buf, packname.len, 1);
		if (!p) {
			ret = error(_("error adding pack %s"), packname.buf);
			goto cleanup;
		}
		if (open_pack_index(p)) {
			ret = error(_("error opening index for %s"), packname.buf);
			goto cleanup;
		}
		for_each_object_in_pack(p, add_packed_cummits, ctx,
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

static int fill_oids_from_cummits(struct write_cummit_graph_context *ctx,
				  struct oidset *cummits)
{
	struct oidset_iter iter;
	struct object_id *oid;

	if (!oidset_size(cummits))
		return 0;

	oidset_iter_init(cummits, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		oid_array_append(&ctx->oids, oid);
	}

	return 0;
}

static void fill_oids_from_all_packs(struct write_cummit_graph_context *ctx)
{
	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
			_("Finding cummits for cummit graph among packed objects"),
			ctx->approx_nr_objects);
	for_each_packed_object(add_packed_cummits, ctx,
			       FOR_EACH_OBJECT_PACK_ORDER);
	if (ctx->progress_done < ctx->approx_nr_objects)
		display_progress(ctx->progress, ctx->approx_nr_objects);
	stop_progress(&ctx->progress);
}

static void copy_oids_to_cummits(struct write_cummit_graph_context *ctx)
{
	uint32_t i;
	enum cummit_graph_split_flags flags = ctx->opts ?
		ctx->opts->split_flags : CUMMIT_GRAPH_SPLIT_UNSPECIFIED;

	ctx->num_extra_edges = 0;
	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
			_("Finding extra edges in cummit graph"),
			ctx->oids.nr);
	oid_array_sort(&ctx->oids);
	for (i = 0; i < ctx->oids.nr; i = oid_array_next_unique(&ctx->oids, i)) {
		unsigned int num_parents;

		display_progress(ctx->progress, i + 1);

		ALLOC_GROW(ctx->cummits.list, ctx->cummits.nr + 1, ctx->cummits.alloc);
		ctx->cummits.list[ctx->cummits.nr] = lookup_cummit(ctx->r, &ctx->oids.oid[i]);

		if (ctx->split && flags != CUMMIT_GRAPH_SPLIT_REPLACE &&
		    cummit_graph_position(ctx->cummits.list[ctx->cummits.nr]) != CUMMIT_NOT_FROM_GRAPH)
			continue;

		if (ctx->split && flags == CUMMIT_GRAPH_SPLIT_REPLACE)
			repo_parse_cummit(ctx->r, ctx->cummits.list[ctx->cummits.nr]);
		else
			repo_parse_cummit_no_graph(ctx->r, ctx->cummits.list[ctx->cummits.nr]);

		num_parents = cummit_list_count(ctx->cummits.list[ctx->cummits.nr]->parents);
		if (num_parents > 2)
			ctx->num_extra_edges += num_parents - 1;

		ctx->cummits.nr++;
	}
	stop_progress(&ctx->progress);
}

static int write_graph_chunk_base_1(struct hashfile *f,
				    struct cummit_graph *g)
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
	struct write_cummit_graph_context *ctx = data;
	int num = write_graph_chunk_base_1(f, ctx->new_base_graph);

	if (num != ctx->num_cummit_graphs_after - 1) {
		error(_("failed to write correct number of base graph ids"));
		return -1;
	}

	return 0;
}

static int write_cummit_graph_file(struct write_cummit_graph_context *ctx)
{
	uint32_t i;
	int fd;
	struct hashfile *f;
	struct lock_file lk = LOCK_INIT;
	const unsigned hashsz = the_hash_algo->rawsz;
	struct strbuf progress_title = STRBUF_INIT;
	struct chunkfile *cf;
	unsigned char file_hash[BUT_MAX_RAWSZ];

	if (ctx->split) {
		struct strbuf tmp_file = STRBUF_INIT;

		strbuf_addf(&tmp_file,
			    "%s/info/cummit-graphs/tmp_graph_XXXXXX",
			    ctx->odb->path);
		ctx->graph_name = strbuf_detach(&tmp_file, NULL);
	} else {
		ctx->graph_name = get_cummit_graph_filename(ctx->odb);
	}

	if (safe_create_leading_directories(ctx->graph_name)) {
		UNLEAK(ctx->graph_name);
		error(_("unable to create leading directories of %s"),
			ctx->graph_name);
		return -1;
	}

	if (ctx->split) {
		char *lock_name = get_cummit_graph_chain_filename(ctx->odb);

		hold_lock_file_for_update_mode(&lk, lock_name,
					       LOCK_DIE_ON_ERROR, 0444);
		free(lock_name);

		fd = but_mkstemp_mode(ctx->graph_name, 0444);
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
	add_chunk(cf, GRAPH_CHUNKID_OIDLOOKUP, hashsz * ctx->cummits.nr,
		  write_graph_chunk_oids);
	add_chunk(cf, GRAPH_CHUNKID_DATA, (hashsz + 16) * ctx->cummits.nr,
		  write_graph_chunk_data);

	if (ctx->write_generation_data)
		add_chunk(cf, GRAPH_CHUNKID_GENERATION_DATA,
			  sizeof(uint32_t) * ctx->cummits.nr,
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
			  sizeof(uint32_t) * ctx->cummits.nr,
			  write_graph_chunk_bloom_indexes);
		add_chunk(cf, GRAPH_CHUNKID_BLOOMDATA,
			  sizeof(uint32_t) * 3
				+ ctx->total_bloom_filter_data_size,
			  write_graph_chunk_bloom_data);
	}
	if (ctx->num_cummit_graphs_after > 1)
		add_chunk(cf, GRAPH_CHUNKID_BASE,
			  hashsz * (ctx->num_cummit_graphs_after - 1),
			  write_graph_chunk_base);

	hashwrite_be32(f, GRAPH_SIGNATURE);

	hashwrite_u8(f, GRAPH_VERSION);
	hashwrite_u8(f, oid_version());
	hashwrite_u8(f, get_num_chunks(cf));
	hashwrite_u8(f, ctx->num_cummit_graphs_after - 1);

	if (ctx->report_progress) {
		strbuf_addf(&progress_title,
			    Q_("Writing out cummit graph in %d pass",
			       "Writing out cummit graph in %d passes",
			       get_num_chunks(cf)),
			    get_num_chunks(cf));
		ctx->progress = start_delayed_progress(
			progress_title.buf,
			get_num_chunks(cf) * ctx->cummits.nr);
	}

	write_chunkfile(cf, ctx);

	stop_progress(&ctx->progress);
	strbuf_release(&progress_title);

	if (ctx->split && ctx->base_graph_name && ctx->num_cummit_graphs_after > 1) {
		char *new_base_hash = xstrdup(oid_to_hex(&ctx->new_base_graph->oid));
		char *new_base_name = get_split_graph_filename(ctx->new_base_graph->odb, new_base_hash);

		free(ctx->cummit_graph_filenames_after[ctx->num_cummit_graphs_after - 2]);
		free(ctx->cummit_graph_hash_after[ctx->num_cummit_graphs_after - 2]);
		ctx->cummit_graph_filenames_after[ctx->num_cummit_graphs_after - 2] = new_base_name;
		ctx->cummit_graph_hash_after[ctx->num_cummit_graphs_after - 2] = new_base_hash;
	}

	close_cummit_graph(ctx->r->objects);
	finalize_hashfile(f, file_hash, FSYNC_COMPONENT_CUMMIT_GRAPH,
			  CSUM_HASH_IN_STREAM | CSUM_FSYNC);
	free_chunkfile(cf);

	if (ctx->split) {
		FILE *chainf = fdopen_lock_file(&lk, "w");
		char *final_graph_name;
		int result;

		close(fd);

		if (!chainf) {
			error(_("unable to open cummit-graph chain file"));
			return -1;
		}

		if (ctx->base_graph_name) {
			const char *dest;
			int idx = ctx->num_cummit_graphs_after - 1;
			if (ctx->num_cummit_graphs_after > 1)
				idx--;

			dest = ctx->cummit_graph_filenames_after[idx];

			if (strcmp(ctx->base_graph_name, dest)) {
				result = rename(ctx->base_graph_name, dest);

				if (result) {
					error(_("failed to rename base cummit-graph file"));
					return -1;
				}
			}
		} else {
			char *graph_name = get_cummit_graph_filename(ctx->odb);
			unlink(graph_name);
			free(graph_name);
		}

		ctx->cummit_graph_hash_after[ctx->num_cummit_graphs_after - 1] = xstrdup(hash_to_hex(file_hash));
		final_graph_name = get_split_graph_filename(ctx->odb,
					ctx->cummit_graph_hash_after[ctx->num_cummit_graphs_after - 1]);
		ctx->cummit_graph_filenames_after[ctx->num_cummit_graphs_after - 1] = final_graph_name;

		result = rename(ctx->graph_name, final_graph_name);

		for (i = 0; i < ctx->num_cummit_graphs_after; i++)
			fprintf(get_lock_file_fp(&lk), "%s\n", ctx->cummit_graph_hash_after[i]);

		if (result) {
			error(_("failed to rename temporary cummit-graph file"));
			return -1;
		}
	}

	cummit_lock_file(&lk);

	return 0;
}

static void split_graph_merge_strategy(struct write_cummit_graph_context *ctx)
{
	struct cummit_graph *g;
	uint32_t num_cummits;
	enum cummit_graph_split_flags flags = CUMMIT_GRAPH_SPLIT_UNSPECIFIED;
	uint32_t i;

	int max_cummits = 0;
	int size_mult = 2;

	if (ctx->opts) {
		max_cummits = ctx->opts->max_cummits;

		if (ctx->opts->size_multiple)
			size_mult = ctx->opts->size_multiple;

		flags = ctx->opts->split_flags;
	}

	g = ctx->r->objects->cummit_graph;
	num_cummits = ctx->cummits.nr;
	if (flags == CUMMIT_GRAPH_SPLIT_REPLACE)
		ctx->num_cummit_graphs_after = 1;
	else
		ctx->num_cummit_graphs_after = ctx->num_cummit_graphs_before + 1;

	if (flags != CUMMIT_GRAPH_SPLIT_MERGE_PROHIBITED &&
	    flags != CUMMIT_GRAPH_SPLIT_REPLACE) {
		while (g && (g->num_cummits <= size_mult * num_cummits ||
			    (max_cummits && num_cummits > max_cummits))) {
			if (g->odb != ctx->odb)
				break;

			num_cummits += g->num_cummits;
			g = g->base_graph;

			ctx->num_cummit_graphs_after--;
		}
	}

	if (flags != CUMMIT_GRAPH_SPLIT_REPLACE)
		ctx->new_base_graph = g;
	else if (ctx->num_cummit_graphs_after != 1)
		BUG("split_graph_merge_strategy: num_cummit_graphs_after "
		    "should be 1 with --split=replace");

	if (ctx->num_cummit_graphs_after == 2) {
		char *old_graph_name = get_cummit_graph_filename(g->odb);

		if (!strcmp(g->filename, old_graph_name) &&
		    g->odb != ctx->odb) {
			ctx->num_cummit_graphs_after = 1;
			ctx->new_base_graph = NULL;
		}

		free(old_graph_name);
	}

	CALLOC_ARRAY(ctx->cummit_graph_filenames_after, ctx->num_cummit_graphs_after);
	CALLOC_ARRAY(ctx->cummit_graph_hash_after, ctx->num_cummit_graphs_after);

	for (i = 0; i < ctx->num_cummit_graphs_after &&
		    i < ctx->num_cummit_graphs_before; i++)
		ctx->cummit_graph_filenames_after[i] = xstrdup(ctx->cummit_graph_filenames_before[i]);

	i = ctx->num_cummit_graphs_before - 1;
	g = ctx->r->objects->cummit_graph;

	while (g) {
		if (i < ctx->num_cummit_graphs_after)
			ctx->cummit_graph_hash_after[i] = xstrdup(oid_to_hex(&g->oid));

		/*
		 * If the topmost remaining layer has generation data chunk, the
		 * resultant layer also has generation data chunk.
		 */
		if (i == ctx->num_cummit_graphs_after - 2)
			ctx->write_generation_data = !!g->chunk_generation_data;

		i--;
		g = g->base_graph;
	}
}

static void merge_cummit_graph(struct write_cummit_graph_context *ctx,
			       struct cummit_graph *g)
{
	uint32_t i;
	uint32_t offset = g->num_cummits_in_base;

	ALLOC_GROW(ctx->cummits.list, ctx->cummits.nr + g->num_cummits, ctx->cummits.alloc);

	for (i = 0; i < g->num_cummits; i++) {
		struct object_id oid;
		struct cummit *result;

		display_progress(ctx->progress, i + 1);

		load_oid_from_graph(g, i + offset, &oid);

		/* only add cummits if they still exist in the repo */
		result = lookup_cummit_reference_gently(ctx->r, &oid, 1);

		if (result) {
			ctx->cummits.list[ctx->cummits.nr] = result;
			ctx->cummits.nr++;
		}
	}
}

static int cummit_compare(const void *_a, const void *_b)
{
	const struct cummit *a = *(const struct cummit **)_a;
	const struct cummit *b = *(const struct cummit **)_b;
	return oidcmp(&a->object.oid, &b->object.oid);
}

static void sort_and_scan_merged_cummits(struct write_cummit_graph_context *ctx)
{
	uint32_t i, dedup_i = 0;

	if (ctx->report_progress)
		ctx->progress = start_delayed_progress(
					_("Scanning merged cummits"),
					ctx->cummits.nr);

	QSORT(ctx->cummits.list, ctx->cummits.nr, cummit_compare);

	ctx->num_extra_edges = 0;
	for (i = 0; i < ctx->cummits.nr; i++) {
		display_progress(ctx->progress, i + 1);

		if (i && oideq(&ctx->cummits.list[i - 1]->object.oid,
			  &ctx->cummits.list[i]->object.oid)) {
			/*
			 * Silently ignore duplicates. These were likely
			 * created due to a cummit appearing in multiple
			 * layers of the chain, which is unexpected but
			 * not invalid. We should make sure there is a
			 * unique copy in the new layer.
			 */
		} else {
			unsigned int num_parents;

			ctx->cummits.list[dedup_i] = ctx->cummits.list[i];
			dedup_i++;

			num_parents = cummit_list_count(ctx->cummits.list[i]->parents);
			if (num_parents > 2)
				ctx->num_extra_edges += num_parents - 1;
		}
	}

	ctx->cummits.nr = dedup_i;

	stop_progress(&ctx->progress);
}

static void merge_cummit_graphs(struct write_cummit_graph_context *ctx)
{
	struct cummit_graph *g = ctx->r->objects->cummit_graph;
	uint32_t current_graph_number = ctx->num_cummit_graphs_before;

	while (g && current_graph_number >= ctx->num_cummit_graphs_after) {
		current_graph_number--;

		if (ctx->report_progress)
			ctx->progress = start_delayed_progress(_("Merging cummit-graph"), 0);

		merge_cummit_graph(ctx, g);
		stop_progress(&ctx->progress);

		g = g->base_graph;
	}

	if (g) {
		ctx->new_base_graph = g;
		ctx->new_num_cummits_in_base = g->num_cummits + g->num_cummits_in_base;
	}

	if (ctx->new_base_graph)
		ctx->base_graph_name = xstrdup(ctx->new_base_graph->filename);

	sort_and_scan_merged_cummits(ctx);
}

static void mark_cummit_graphs(struct write_cummit_graph_context *ctx)
{
	uint32_t i;
	time_t now = time(NULL);

	for (i = ctx->num_cummit_graphs_after - 1; i < ctx->num_cummit_graphs_before; i++) {
		struct stat st;
		struct utimbuf updated_time;

		stat(ctx->cummit_graph_filenames_before[i], &st);

		updated_time.actime = st.st_atime;
		updated_time.modtime = now;
		utime(ctx->cummit_graph_filenames_before[i], &updated_time);
	}
}

static void expire_cummit_graphs(struct write_cummit_graph_context *ctx)
{
	struct strbuf path = STRBUF_INIT;
	DIR *dir;
	struct dirent *de;
	size_t dirnamelen;
	timestamp_t expire_time = time(NULL);

	if (ctx->opts && ctx->opts->expire_time)
		expire_time = ctx->opts->expire_time;
	if (!ctx->split) {
		char *chain_file_name = get_cummit_graph_chain_filename(ctx->odb);
		unlink(chain_file_name);
		free(chain_file_name);
		ctx->num_cummit_graphs_after = 0;
	}

	strbuf_addstr(&path, ctx->odb->path);
	strbuf_addstr(&path, "/info/cummit-graphs");
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

		stat(path.buf, &st);

		if (st.st_mtime > expire_time)
			continue;
		if (path.len < 6 || strcmp(path.buf + path.len - 6, ".graph"))
			continue;

		for (i = 0; i < ctx->num_cummit_graphs_after; i++) {
			if (!strcmp(ctx->cummit_graph_filenames_after[i],
				    path.buf)) {
				found = 1;
				break;
			}
		}

		if (!found)
			unlink(path.buf);
	}

out:
	strbuf_release(&path);
}

int write_cummit_graph(struct object_directory *odb,
		       const struct string_list *const pack_indexes,
		       struct oidset *cummits,
		       enum cummit_graph_write_flags flags,
		       const struct cummit_graph_opts *opts)
{
	struct repository *r = the_repository;
	struct write_cummit_graph_context *ctx;
	uint32_t i;
	int res = 0;
	int replace = 0;
	struct bloom_filter_settings bloom_settings = DEFAULT_BLOOM_FILTER_SETTINGS;
	struct topo_level_slab topo_levels;

	prepare_repo_settings(r);
	if (!r->settings.core_cummit_graph) {
		warning(_("attempting to write a cummit-graph, but 'core.cummitGraph' is disabled"));
		return 0;
	}
	if (!cummit_graph_compatible(r))
		return 0;

	CALLOC_ARRAY(ctx, 1);
	ctx->r = r;
	ctx->odb = odb;
	ctx->append = flags & CUMMIT_GRAPH_WRITE_APPEND ? 1 : 0;
	ctx->report_progress = flags & CUMMIT_GRAPH_WRITE_PROGRESS ? 1 : 0;
	ctx->split = flags & CUMMIT_GRAPH_WRITE_SPLIT ? 1 : 0;
	ctx->opts = opts;
	ctx->total_bloom_filter_data_size = 0;
	ctx->write_generation_data = (get_configured_generation_version(r) == 2);
	ctx->num_generation_data_overflows = 0;

	bloom_settings.bits_per_entry = but_env_ulong("BUT_TEST_BLOOM_SETTINGS_BITS_PER_ENTRY",
						      bloom_settings.bits_per_entry);
	bloom_settings.num_hashes = but_env_ulong("BUT_TEST_BLOOM_SETTINGS_NUM_HASHES",
						  bloom_settings.num_hashes);
	bloom_settings.max_changed_paths = but_env_ulong("BUT_TEST_BLOOM_SETTINGS_MAX_CHANGED_PATHS",
							 bloom_settings.max_changed_paths);
	ctx->bloom_settings = &bloom_settings;

	init_topo_level_slab(&topo_levels);
	ctx->topo_levels = &topo_levels;

	prepare_cummit_graph(ctx->r);
	if (ctx->r->objects->cummit_graph) {
		struct cummit_graph *g = ctx->r->objects->cummit_graph;

		while (g) {
			g->topo_levels = &topo_levels;
			g = g->base_graph;
		}
	}

	if (flags & CUMMIT_GRAPH_WRITE_BLOOM_FILTERS)
		ctx->changed_paths = 1;
	if (!(flags & CUMMIT_GRAPH_NO_WRITE_BLOOM_FILTERS)) {
		struct cummit_graph *g;

		g = ctx->r->objects->cummit_graph;

		/* We have changed-paths already. Keep them in the next graph */
		if (g && g->chunk_bloom_data) {
			ctx->changed_paths = 1;
			ctx->bloom_settings = g->bloom_filter_settings;
		}
	}

	if (ctx->split) {
		struct cummit_graph *g = ctx->r->objects->cummit_graph;

		while (g) {
			ctx->num_cummit_graphs_before++;
			g = g->base_graph;
		}

		if (ctx->num_cummit_graphs_before) {
			ALLOC_ARRAY(ctx->cummit_graph_filenames_before, ctx->num_cummit_graphs_before);
			i = ctx->num_cummit_graphs_before;
			g = ctx->r->objects->cummit_graph;

			while (g) {
				ctx->cummit_graph_filenames_before[--i] = xstrdup(g->filename);
				g = g->base_graph;
			}
		}

		if (ctx->opts)
			replace = ctx->opts->split_flags & CUMMIT_GRAPH_SPLIT_REPLACE;
	}

	ctx->approx_nr_objects = approximate_object_count();

	if (ctx->append && ctx->r->objects->cummit_graph) {
		struct cummit_graph *g = ctx->r->objects->cummit_graph;
		for (i = 0; i < g->num_cummits; i++) {
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

	if (cummits) {
		if ((res = fill_oids_from_cummits(ctx, cummits)))
			goto cleanup;
	}

	if (!pack_indexes && !cummits) {
		ctx->order_by_pack = 1;
		fill_oids_from_all_packs(ctx);
	}

	close_reachable(ctx);

	copy_oids_to_cummits(ctx);

	if (ctx->cummits.nr >= GRAPH_EDGE_LAST_MASK) {
		error(_("too many cummits to write graph"));
		res = -1;
		goto cleanup;
	}

	if (!ctx->cummits.nr && !replace)
		goto cleanup;

	if (ctx->split) {
		split_graph_merge_strategy(ctx);

		if (!replace)
			merge_cummit_graphs(ctx);
	} else
		ctx->num_cummit_graphs_after = 1;

	ctx->trust_generation_numbers = validate_mixed_generation_chain(ctx->r->objects->cummit_graph);

	compute_topological_levels(ctx);
	if (ctx->write_generation_data)
		compute_generation_numbers(ctx);

	if (ctx->changed_paths)
		compute_bloom_filters(ctx);

	res = write_cummit_graph_file(ctx);

	if (ctx->split)
		mark_cummit_graphs(ctx);

	expire_cummit_graphs(ctx);

cleanup:
	free(ctx->graph_name);
	free(ctx->cummits.list);
	oid_array_clear(&ctx->oids);
	clear_topo_level_slab(&topo_levels);

	if (ctx->cummit_graph_filenames_after) {
		for (i = 0; i < ctx->num_cummit_graphs_after; i++) {
			free(ctx->cummit_graph_filenames_after[i]);
			free(ctx->cummit_graph_hash_after[i]);
		}

		for (i = 0; i < ctx->num_cummit_graphs_before; i++)
			free(ctx->cummit_graph_filenames_before[i]);

		free(ctx->cummit_graph_filenames_after);
		free(ctx->cummit_graph_filenames_before);
		free(ctx->cummit_graph_hash_after);
	}

	free(ctx);

	return res;
}

#define VERIFY_CUMMIT_GRAPH_ERROR_HASH 2
static int verify_cummit_graph_error;

__attribute__((format (printf, 1, 2)))
static void graph_report(const char *fmt, ...)
{
	va_list ap;

	verify_cummit_graph_error = 1;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

#define GENERATION_ZERO_EXISTS 1
#define GENERATION_NUMBER_EXISTS 2

static int cummit_graph_checksum_valid(struct cummit_graph *g)
{
	return hashfile_checksum_valid(g->data, g->data_len);
}

int verify_cummit_graph(struct repository *r, struct cummit_graph *g, int flags)
{
	uint32_t i, cur_fanout_pos = 0;
	struct object_id prev_oid, cur_oid;
	int generation_zero = 0;
	struct progress *progress = NULL;
	int local_error = 0;

	if (!g) {
		graph_report("no cummit-graph file loaded");
		return 1;
	}

	verify_cummit_graph_error = verify_cummit_graph_lite(g);
	if (verify_cummit_graph_error)
		return verify_cummit_graph_error;

	if (!cummit_graph_checksum_valid(g)) {
		graph_report(_("the cummit-graph file has incorrect checksum and is likely corrupt"));
		verify_cummit_graph_error = VERIFY_CUMMIT_GRAPH_ERROR_HASH;
	}

	for (i = 0; i < g->num_cummits; i++) {
		struct cummit *graph_cummit;

		oidread(&cur_oid, g->chunk_oid_lookup + g->hash_len * i);

		if (i && oidcmp(&prev_oid, &cur_oid) >= 0)
			graph_report(_("cummit-graph has incorrect OID order: %s then %s"),
				     oid_to_hex(&prev_oid),
				     oid_to_hex(&cur_oid));

		oidcpy(&prev_oid, &cur_oid);

		while (cur_oid.hash[0] > cur_fanout_pos) {
			uint32_t fanout_value = get_be32(g->chunk_oid_fanout + cur_fanout_pos);

			if (i != fanout_value)
				graph_report(_("cummit-graph has incorrect fanout value: fanout[%d] = %u != %u"),
					     cur_fanout_pos, fanout_value, i);
			cur_fanout_pos++;
		}

		graph_cummit = lookup_cummit(r, &cur_oid);
		if (!parse_cummit_in_graph_one(r, g, graph_cummit))
			graph_report(_("failed to parse cummit %s from cummit-graph"),
				     oid_to_hex(&cur_oid));
	}

	while (cur_fanout_pos < 256) {
		uint32_t fanout_value = get_be32(g->chunk_oid_fanout + cur_fanout_pos);

		if (g->num_cummits != fanout_value)
			graph_report(_("cummit-graph has incorrect fanout value: fanout[%d] = %u != %u"),
				     cur_fanout_pos, fanout_value, i);

		cur_fanout_pos++;
	}

	if (verify_cummit_graph_error & ~VERIFY_CUMMIT_GRAPH_ERROR_HASH)
		return verify_cummit_graph_error;

	if (flags & CUMMIT_GRAPH_WRITE_PROGRESS)
		progress = start_progress(_("Verifying cummits in cummit graph"),
					g->num_cummits);

	for (i = 0; i < g->num_cummits; i++) {
		struct cummit *graph_cummit, *odb_cummit;
		struct cummit_list *graph_parents, *odb_parents;
		timestamp_t max_generation = 0;
		timestamp_t generation;

		display_progress(progress, i + 1);
		oidread(&cur_oid, g->chunk_oid_lookup + g->hash_len * i);

		graph_cummit = lookup_cummit(r, &cur_oid);
		odb_cummit = (struct cummit *)create_object(r, &cur_oid, alloc_cummit_node(r));
		if (parse_cummit_internal(odb_cummit, 0, 0)) {
			graph_report(_("failed to parse cummit %s from object database for cummit-graph"),
				     oid_to_hex(&cur_oid));
			continue;
		}

		if (!oideq(&get_cummit_tree_in_graph_one(r, g, graph_cummit)->object.oid,
			   get_cummit_tree_oid(odb_cummit)))
			graph_report(_("root tree OID for cummit %s in cummit-graph is %s != %s"),
				     oid_to_hex(&cur_oid),
				     oid_to_hex(get_cummit_tree_oid(graph_cummit)),
				     oid_to_hex(get_cummit_tree_oid(odb_cummit)));

		graph_parents = graph_cummit->parents;
		odb_parents = odb_cummit->parents;

		while (graph_parents) {
			if (odb_parents == NULL) {
				graph_report(_("cummit-graph parent list for cummit %s is too long"),
					     oid_to_hex(&cur_oid));
				break;
			}

			/* parse parent in case it is in a base graph */
			parse_cummit_in_graph_one(r, g, graph_parents->item);

			if (!oideq(&graph_parents->item->object.oid, &odb_parents->item->object.oid))
				graph_report(_("cummit-graph parent for %s is %s != %s"),
					     oid_to_hex(&cur_oid),
					     oid_to_hex(&graph_parents->item->object.oid),
					     oid_to_hex(&odb_parents->item->object.oid));

			generation = cummit_graph_generation(graph_parents->item);
			if (generation > max_generation)
				max_generation = generation;

			graph_parents = graph_parents->next;
			odb_parents = odb_parents->next;
		}

		if (odb_parents != NULL)
			graph_report(_("cummit-graph parent list for cummit %s terminates early"),
				     oid_to_hex(&cur_oid));

		if (!cummit_graph_generation(graph_cummit)) {
			if (generation_zero == GENERATION_NUMBER_EXISTS)
				graph_report(_("cummit-graph has generation number zero for cummit %s, but non-zero elsewhere"),
					     oid_to_hex(&cur_oid));
			generation_zero = GENERATION_ZERO_EXISTS;
		} else if (generation_zero == GENERATION_ZERO_EXISTS)
			graph_report(_("cummit-graph has non-zero generation number for cummit %s, but zero elsewhere"),
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

		generation = cummit_graph_generation(graph_cummit);
		if (generation < max_generation + 1)
			graph_report(_("cummit-graph generation for cummit %s is %"PRItime" < %"PRItime),
				     oid_to_hex(&cur_oid),
				     generation,
				     max_generation + 1);

		if (graph_cummit->date != odb_cummit->date)
			graph_report(_("cummit date for cummit %s in cummit-graph is %"PRItime" != %"PRItime),
				     oid_to_hex(&cur_oid),
				     graph_cummit->date,
				     odb_cummit->date);
	}
	stop_progress(&progress);

	local_error = verify_cummit_graph_error;

	if (!(flags & CUMMIT_GRAPH_VERIFY_SHALLOW) && g->base_graph)
		local_error |= verify_cummit_graph(r, g->base_graph, flags);

	return local_error;
}

void free_cummit_graph(struct cummit_graph *g)
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

void disable_cummit_graph(struct repository *r)
{
	r->cummit_graph_disabled = 1;
}
