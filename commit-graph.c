#include "cache.h"
#include "config.h"
#include "git-compat-util.h"
#include "lockfile.h"
#include "pack.h"
#include "packfile.h"
#include "commit.h"
#include "object.h"
#include "revision.h"
#include "sha1-lookup.h"
#include "commit-graph.h"
#include "object-store.h"

#define GRAPH_SIGNATURE 0x43475048 /* "CGPH" */
#define GRAPH_CHUNKID_OIDFANOUT 0x4f494446 /* "OIDF" */
#define GRAPH_CHUNKID_OIDLOOKUP 0x4f49444c /* "OIDL" */
#define GRAPH_CHUNKID_DATA 0x43444154 /* "CDAT" */
#define GRAPH_CHUNKID_LARGEEDGES 0x45444745 /* "EDGE" */

#define GRAPH_DATA_WIDTH 36

#define GRAPH_VERSION_1 0x1
#define GRAPH_VERSION GRAPH_VERSION_1

#define GRAPH_OID_VERSION_SHA1 1
#define GRAPH_OID_LEN_SHA1 GIT_SHA1_RAWSZ
#define GRAPH_OID_VERSION GRAPH_OID_VERSION_SHA1
#define GRAPH_OID_LEN GRAPH_OID_LEN_SHA1

#define GRAPH_OCTOPUS_EDGES_NEEDED 0x80000000
#define GRAPH_PARENT_MISSING 0x7fffffff
#define GRAPH_EDGE_LAST_MASK 0x7fffffff
#define GRAPH_PARENT_NONE 0x70000000

#define GRAPH_LAST_EDGE 0x80000000

#define GRAPH_FANOUT_SIZE (4 * 256)
#define GRAPH_CHUNKLOOKUP_WIDTH 12
#define GRAPH_MIN_SIZE (5 * GRAPH_CHUNKLOOKUP_WIDTH + GRAPH_FANOUT_SIZE + \
			GRAPH_OID_LEN + 8)

char *get_commit_graph_filename(const char *obj_dir)
{
	return xstrfmt("%s/info/commit-graph", obj_dir);
}

static struct commit_graph *alloc_commit_graph(void)
{
	struct commit_graph *g = xcalloc(1, sizeof(*g));
	g->graph_fd = -1;

	return g;
}

struct commit_graph *load_commit_graph_one(const char *graph_file)
{
	void *graph_map;
	const unsigned char *data, *chunk_lookup;
	size_t graph_size;
	struct stat st;
	uint32_t i;
	struct commit_graph *graph;
	int fd = git_open(graph_file);
	uint64_t last_chunk_offset;
	uint32_t last_chunk_id;
	uint32_t graph_signature;
	unsigned char graph_version, hash_version;

	if (fd < 0)
		return NULL;
	if (fstat(fd, &st)) {
		close(fd);
		return NULL;
	}
	graph_size = xsize_t(st.st_size);

	if (graph_size < GRAPH_MIN_SIZE) {
		close(fd);
		die("graph file %s is too small", graph_file);
	}
	graph_map = xmmap(NULL, graph_size, PROT_READ, MAP_PRIVATE, fd, 0);
	data = (const unsigned char *)graph_map;

	graph_signature = get_be32(data);
	if (graph_signature != GRAPH_SIGNATURE) {
		error("graph signature %X does not match signature %X",
		      graph_signature, GRAPH_SIGNATURE);
		goto cleanup_fail;
	}

	graph_version = *(unsigned char*)(data + 4);
	if (graph_version != GRAPH_VERSION) {
		error("graph version %X does not match version %X",
		      graph_version, GRAPH_VERSION);
		goto cleanup_fail;
	}

	hash_version = *(unsigned char*)(data + 5);
	if (hash_version != GRAPH_OID_VERSION) {
		error("hash version %X does not match version %X",
		      hash_version, GRAPH_OID_VERSION);
		goto cleanup_fail;
	}

	graph = alloc_commit_graph();

	graph->hash_len = GRAPH_OID_LEN;
	graph->num_chunks = *(unsigned char*)(data + 6);
	graph->graph_fd = fd;
	graph->data = graph_map;
	graph->data_len = graph_size;

	last_chunk_id = 0;
	last_chunk_offset = 8;
	chunk_lookup = data + 8;
	for (i = 0; i < graph->num_chunks; i++) {
		uint32_t chunk_id = get_be32(chunk_lookup + 0);
		uint64_t chunk_offset = get_be64(chunk_lookup + 4);
		int chunk_repeated = 0;

		chunk_lookup += GRAPH_CHUNKLOOKUP_WIDTH;

		if (chunk_offset > graph_size - GIT_MAX_RAWSZ) {
			error("improper chunk offset %08x%08x", (uint32_t)(chunk_offset >> 32),
			      (uint32_t)chunk_offset);
			goto cleanup_fail;
		}

		switch (chunk_id) {
		case GRAPH_CHUNKID_OIDFANOUT:
			if (graph->chunk_oid_fanout)
				chunk_repeated = 1;
			else
				graph->chunk_oid_fanout = (uint32_t*)(data + chunk_offset);
			break;

		case GRAPH_CHUNKID_OIDLOOKUP:
			if (graph->chunk_oid_lookup)
				chunk_repeated = 1;
			else
				graph->chunk_oid_lookup = data + chunk_offset;
			break;

		case GRAPH_CHUNKID_DATA:
			if (graph->chunk_commit_data)
				chunk_repeated = 1;
			else
				graph->chunk_commit_data = data + chunk_offset;
			break;

		case GRAPH_CHUNKID_LARGEEDGES:
			if (graph->chunk_large_edges)
				chunk_repeated = 1;
			else
				graph->chunk_large_edges = data + chunk_offset;
			break;
		}

		if (chunk_repeated) {
			error("chunk id %08x appears multiple times", chunk_id);
			goto cleanup_fail;
		}

		if (last_chunk_id == GRAPH_CHUNKID_OIDLOOKUP)
		{
			graph->num_commits = (chunk_offset - last_chunk_offset)
					     / graph->hash_len;
		}

		last_chunk_id = chunk_id;
		last_chunk_offset = chunk_offset;
	}

	return graph;

cleanup_fail:
	munmap(graph_map, graph_size);
	close(fd);
	exit(1);
}

/* global storage */
static struct commit_graph *commit_graph = NULL;

static void prepare_commit_graph_one(const char *obj_dir)
{
	char *graph_name;

	if (commit_graph)
		return;

	graph_name = get_commit_graph_filename(obj_dir);
	commit_graph = load_commit_graph_one(graph_name);

	FREE_AND_NULL(graph_name);
}

static int prepare_commit_graph_run_once = 0;
static void prepare_commit_graph(void)
{
	struct alternate_object_database *alt;
	char *obj_dir;

	if (prepare_commit_graph_run_once)
		return;
	prepare_commit_graph_run_once = 1;

	obj_dir = get_object_directory();
	prepare_commit_graph_one(obj_dir);
	prepare_alt_odb(the_repository);
	for (alt = the_repository->objects->alt_odb_list;
	     !commit_graph && alt;
	     alt = alt->next)
		prepare_commit_graph_one(alt->path);
}

static void close_commit_graph(void)
{
	if (!commit_graph)
		return;

	if (commit_graph->graph_fd >= 0) {
		munmap((void *)commit_graph->data, commit_graph->data_len);
		commit_graph->data = NULL;
		close(commit_graph->graph_fd);
	}

	FREE_AND_NULL(commit_graph);
}

static int bsearch_graph(struct commit_graph *g, struct object_id *oid, uint32_t *pos)
{
	return bsearch_hash(oid->hash, g->chunk_oid_fanout,
			    g->chunk_oid_lookup, g->hash_len, pos);
}

static struct commit_list **insert_parent_or_die(struct commit_graph *g,
						 uint64_t pos,
						 struct commit_list **pptr)
{
	struct commit *c;
	struct object_id oid;
	hashcpy(oid.hash, g->chunk_oid_lookup + g->hash_len * pos);
	c = lookup_commit(&oid);
	if (!c)
		die("could not find commit %s", oid_to_hex(&oid));
	c->graph_pos = pos;
	return &commit_list_insert(c, pptr)->next;
}

static int fill_commit_in_graph(struct commit *item, struct commit_graph *g, uint32_t pos)
{
	struct object_id oid;
	uint32_t edge_value;
	uint32_t *parent_data_ptr;
	uint64_t date_low, date_high;
	struct commit_list **pptr;
	const unsigned char *commit_data = g->chunk_commit_data + (g->hash_len + 16) * pos;

	item->object.parsed = 1;
	item->graph_pos = pos;

	hashcpy(oid.hash, commit_data);
	item->tree = lookup_tree(&oid);

	date_high = get_be32(commit_data + g->hash_len + 8) & 0x3;
	date_low = get_be32(commit_data + g->hash_len + 12);
	item->date = (timestamp_t)((date_high << 32) | date_low);

	pptr = &item->parents;

	edge_value = get_be32(commit_data + g->hash_len);
	if (edge_value == GRAPH_PARENT_NONE)
		return 1;
	pptr = insert_parent_or_die(g, edge_value, pptr);

	edge_value = get_be32(commit_data + g->hash_len + 4);
	if (edge_value == GRAPH_PARENT_NONE)
		return 1;
	if (!(edge_value & GRAPH_OCTOPUS_EDGES_NEEDED)) {
		pptr = insert_parent_or_die(g, edge_value, pptr);
		return 1;
	}

	parent_data_ptr = (uint32_t*)(g->chunk_large_edges +
			  4 * (uint64_t)(edge_value & GRAPH_EDGE_LAST_MASK));
	do {
		edge_value = get_be32(parent_data_ptr);
		pptr = insert_parent_or_die(g,
					    edge_value & GRAPH_EDGE_LAST_MASK,
					    pptr);
		parent_data_ptr++;
	} while (!(edge_value & GRAPH_LAST_EDGE));

	return 1;
}

int parse_commit_in_graph(struct commit *item)
{
	if (!core_commit_graph)
		return 0;
	if (item->object.parsed)
		return 1;

	prepare_commit_graph();
	if (commit_graph) {
		uint32_t pos;
		int found;
		if (item->graph_pos != COMMIT_NOT_FROM_GRAPH) {
			pos = item->graph_pos;
			found = 1;
		} else {
			found = bsearch_graph(commit_graph, &(item->object.oid), &pos);
		}

		if (found)
			return fill_commit_in_graph(item, commit_graph, pos);
	}

	return 0;
}

static void write_graph_chunk_fanout(struct hashfile *f,
				     struct commit **commits,
				     int nr_commits)
{
	int i, count = 0;
	struct commit **list = commits;

	/*
	 * Write the first-level table (the list is sorted,
	 * but we use a 256-entry lookup to be able to avoid
	 * having to do eight extra binary search iterations).
	 */
	for (i = 0; i < 256; i++) {
		while (count < nr_commits) {
			if ((*list)->object.oid.hash[0] != i)
				break;
			count++;
			list++;
		}

		hashwrite_be32(f, count);
	}
}

static void write_graph_chunk_oids(struct hashfile *f, int hash_len,
				   struct commit **commits, int nr_commits)
{
	struct commit **list = commits;
	int count;
	for (count = 0; count < nr_commits; count++, list++)
		hashwrite(f, (*list)->object.oid.hash, (int)hash_len);
}

static const unsigned char *commit_to_sha1(size_t index, void *table)
{
	struct commit **commits = table;
	return commits[index]->object.oid.hash;
}

static void write_graph_chunk_data(struct hashfile *f, int hash_len,
				   struct commit **commits, int nr_commits)
{
	struct commit **list = commits;
	struct commit **last = commits + nr_commits;
	uint32_t num_extra_edges = 0;

	while (list < last) {
		struct commit_list *parent;
		int edge_value;
		uint32_t packedDate[2];

		parse_commit(*list);
		hashwrite(f, (*list)->tree->object.oid.hash, hash_len);

		parent = (*list)->parents;

		if (!parent)
			edge_value = GRAPH_PARENT_NONE;
		else {
			edge_value = sha1_pos(parent->item->object.oid.hash,
					      commits,
					      nr_commits,
					      commit_to_sha1);

			if (edge_value < 0)
				edge_value = GRAPH_PARENT_MISSING;
		}

		hashwrite_be32(f, edge_value);

		if (parent)
			parent = parent->next;

		if (!parent)
			edge_value = GRAPH_PARENT_NONE;
		else if (parent->next)
			edge_value = GRAPH_OCTOPUS_EDGES_NEEDED | num_extra_edges;
		else {
			edge_value = sha1_pos(parent->item->object.oid.hash,
					      commits,
					      nr_commits,
					      commit_to_sha1);
			if (edge_value < 0)
				edge_value = GRAPH_PARENT_MISSING;
		}

		hashwrite_be32(f, edge_value);

		if (edge_value & GRAPH_OCTOPUS_EDGES_NEEDED) {
			do {
				num_extra_edges++;
				parent = parent->next;
			} while (parent);
		}

		if (sizeof((*list)->date) > 4)
			packedDate[0] = htonl(((*list)->date >> 32) & 0x3);
		else
			packedDate[0] = 0;

		packedDate[1] = htonl((*list)->date);
		hashwrite(f, packedDate, 8);

		list++;
	}
}

static void write_graph_chunk_large_edges(struct hashfile *f,
					  struct commit **commits,
					  int nr_commits)
{
	struct commit **list = commits;
	struct commit **last = commits + nr_commits;
	struct commit_list *parent;

	while (list < last) {
		int num_parents = 0;
		for (parent = (*list)->parents; num_parents < 3 && parent;
		     parent = parent->next)
			num_parents++;

		if (num_parents <= 2) {
			list++;
			continue;
		}

		/* Since num_parents > 2, this initializer is safe. */
		for (parent = (*list)->parents->next; parent; parent = parent->next) {
			int edge_value = sha1_pos(parent->item->object.oid.hash,
						  commits,
						  nr_commits,
						  commit_to_sha1);

			if (edge_value < 0)
				edge_value = GRAPH_PARENT_MISSING;
			else if (!parent->next)
				edge_value |= GRAPH_LAST_EDGE;

			hashwrite_be32(f, edge_value);
		}

		list++;
	}
}

static int commit_compare(const void *_a, const void *_b)
{
	const struct object_id *a = (const struct object_id *)_a;
	const struct object_id *b = (const struct object_id *)_b;
	return oidcmp(a, b);
}

struct packed_commit_list {
	struct commit **list;
	int nr;
	int alloc;
};

struct packed_oid_list {
	struct object_id *list;
	int nr;
	int alloc;
};

static int add_packed_commits(const struct object_id *oid,
			      struct packed_git *pack,
			      uint32_t pos,
			      void *data)
{
	struct packed_oid_list *list = (struct packed_oid_list*)data;
	enum object_type type;
	off_t offset = nth_packed_object_offset(pack, pos);
	struct object_info oi = OBJECT_INFO_INIT;

	oi.typep = &type;
	if (packed_object_info(pack, offset, &oi) < 0)
		die("unable to get type of object %s", oid_to_hex(oid));

	if (type != OBJ_COMMIT)
		return 0;

	ALLOC_GROW(list->list, list->nr + 1, list->alloc);
	oidcpy(&(list->list[list->nr]), oid);
	list->nr++;

	return 0;
}

static void add_missing_parents(struct packed_oid_list *oids, struct commit *commit)
{
	struct commit_list *parent;
	for (parent = commit->parents; parent; parent = parent->next) {
		if (!(parent->item->object.flags & UNINTERESTING)) {
			ALLOC_GROW(oids->list, oids->nr + 1, oids->alloc);
			oidcpy(&oids->list[oids->nr], &(parent->item->object.oid));
			oids->nr++;
			parent->item->object.flags |= UNINTERESTING;
		}
	}
}

static void close_reachable(struct packed_oid_list *oids)
{
	int i;
	struct commit *commit;

	for (i = 0; i < oids->nr; i++) {
		commit = lookup_commit(&oids->list[i]);
		if (commit)
			commit->object.flags |= UNINTERESTING;
	}

	/*
	 * As this loop runs, oids->nr may grow, but not more
	 * than the number of missing commits in the reachable
	 * closure.
	 */
	for (i = 0; i < oids->nr; i++) {
		commit = lookup_commit(&oids->list[i]);

		if (commit && !parse_commit(commit))
			add_missing_parents(oids, commit);
	}

	for (i = 0; i < oids->nr; i++) {
		commit = lookup_commit(&oids->list[i]);

		if (commit)
			commit->object.flags &= ~UNINTERESTING;
	}
}

void write_commit_graph(const char *obj_dir,
			const char **pack_indexes,
			int nr_packs,
			const char **commit_hex,
			int nr_commits,
			int append)
{
	struct packed_oid_list oids;
	struct packed_commit_list commits;
	struct hashfile *f;
	uint32_t i, count_distinct = 0;
	char *graph_name;
	int fd;
	struct lock_file lk = LOCK_INIT;
	uint32_t chunk_ids[5];
	uint64_t chunk_offsets[5];
	int num_chunks;
	int num_extra_edges;
	struct commit_list *parent;

	oids.nr = 0;
	oids.alloc = approximate_object_count() / 4;

	if (append) {
		prepare_commit_graph_one(obj_dir);
		if (commit_graph)
			oids.alloc += commit_graph->num_commits;
	}

	if (oids.alloc < 1024)
		oids.alloc = 1024;
	ALLOC_ARRAY(oids.list, oids.alloc);

	if (append && commit_graph) {
		for (i = 0; i < commit_graph->num_commits; i++) {
			const unsigned char *hash = commit_graph->chunk_oid_lookup +
				commit_graph->hash_len * i;
			hashcpy(oids.list[oids.nr++].hash, hash);
		}
	}

	if (pack_indexes) {
		struct strbuf packname = STRBUF_INIT;
		int dirlen;
		strbuf_addf(&packname, "%s/pack/", obj_dir);
		dirlen = packname.len;
		for (i = 0; i < nr_packs; i++) {
			struct packed_git *p;
			strbuf_setlen(&packname, dirlen);
			strbuf_addstr(&packname, pack_indexes[i]);
			p = add_packed_git(packname.buf, packname.len, 1);
			if (!p)
				die("error adding pack %s", packname.buf);
			if (open_pack_index(p))
				die("error opening index for %s", packname.buf);
			for_each_object_in_pack(p, add_packed_commits, &oids);
			close_pack(p);
		}
		strbuf_release(&packname);
	}

	if (commit_hex) {
		for (i = 0; i < nr_commits; i++) {
			const char *end;
			struct object_id oid;
			struct commit *result;

			if (commit_hex[i] && parse_oid_hex(commit_hex[i], &oid, &end))
				continue;

			result = lookup_commit_reference_gently(&oid, 1);

			if (result) {
				ALLOC_GROW(oids.list, oids.nr + 1, oids.alloc);
				oidcpy(&oids.list[oids.nr], &(result->object.oid));
				oids.nr++;
			}
		}
	}

	if (!pack_indexes && !commit_hex)
		for_each_packed_object(add_packed_commits, &oids, 0);

	close_reachable(&oids);

	QSORT(oids.list, oids.nr, commit_compare);

	count_distinct = 1;
	for (i = 1; i < oids.nr; i++) {
		if (oidcmp(&oids.list[i-1], &oids.list[i]))
			count_distinct++;
	}

	if (count_distinct >= GRAPH_PARENT_MISSING)
		die(_("the commit graph format cannot write %d commits"), count_distinct);

	commits.nr = 0;
	commits.alloc = count_distinct;
	ALLOC_ARRAY(commits.list, commits.alloc);

	num_extra_edges = 0;
	for (i = 0; i < oids.nr; i++) {
		int num_parents = 0;
		if (i > 0 && !oidcmp(&oids.list[i-1], &oids.list[i]))
			continue;

		commits.list[commits.nr] = lookup_commit(&oids.list[i]);
		parse_commit(commits.list[commits.nr]);

		for (parent = commits.list[commits.nr]->parents;
		     parent; parent = parent->next)
			num_parents++;

		if (num_parents > 2)
			num_extra_edges += num_parents - 1;

		commits.nr++;
	}
	num_chunks = num_extra_edges ? 4 : 3;

	if (commits.nr >= GRAPH_PARENT_MISSING)
		die(_("too many commits to write graph"));

	graph_name = get_commit_graph_filename(obj_dir);
	fd = hold_lock_file_for_update(&lk, graph_name, 0);

	if (fd < 0) {
		struct strbuf folder = STRBUF_INIT;
		strbuf_addstr(&folder, graph_name);
		strbuf_setlen(&folder, strrchr(folder.buf, '/') - folder.buf);

		if (mkdir(folder.buf, 0777) < 0)
			die_errno(_("cannot mkdir %s"), folder.buf);
		strbuf_release(&folder);

		fd = hold_lock_file_for_update(&lk, graph_name, LOCK_DIE_ON_ERROR);

		if (fd < 0)
			die_errno("unable to create '%s'", graph_name);
	}

	f = hashfd(lk.tempfile->fd, lk.tempfile->filename.buf);

	hashwrite_be32(f, GRAPH_SIGNATURE);

	hashwrite_u8(f, GRAPH_VERSION);
	hashwrite_u8(f, GRAPH_OID_VERSION);
	hashwrite_u8(f, num_chunks);
	hashwrite_u8(f, 0); /* unused padding byte */

	chunk_ids[0] = GRAPH_CHUNKID_OIDFANOUT;
	chunk_ids[1] = GRAPH_CHUNKID_OIDLOOKUP;
	chunk_ids[2] = GRAPH_CHUNKID_DATA;
	if (num_extra_edges)
		chunk_ids[3] = GRAPH_CHUNKID_LARGEEDGES;
	else
		chunk_ids[3] = 0;
	chunk_ids[4] = 0;

	chunk_offsets[0] = 8 + (num_chunks + 1) * GRAPH_CHUNKLOOKUP_WIDTH;
	chunk_offsets[1] = chunk_offsets[0] + GRAPH_FANOUT_SIZE;
	chunk_offsets[2] = chunk_offsets[1] + GRAPH_OID_LEN * commits.nr;
	chunk_offsets[3] = chunk_offsets[2] + (GRAPH_OID_LEN + 16) * commits.nr;
	chunk_offsets[4] = chunk_offsets[3] + 4 * num_extra_edges;

	for (i = 0; i <= num_chunks; i++) {
		uint32_t chunk_write[3];

		chunk_write[0] = htonl(chunk_ids[i]);
		chunk_write[1] = htonl(chunk_offsets[i] >> 32);
		chunk_write[2] = htonl(chunk_offsets[i] & 0xffffffff);
		hashwrite(f, chunk_write, 12);
	}

	write_graph_chunk_fanout(f, commits.list, commits.nr);
	write_graph_chunk_oids(f, GRAPH_OID_LEN, commits.list, commits.nr);
	write_graph_chunk_data(f, GRAPH_OID_LEN, commits.list, commits.nr);
	write_graph_chunk_large_edges(f, commits.list, commits.nr);

	close_commit_graph();
	finalize_hashfile(f, NULL, CSUM_HASH_IN_STREAM | CSUM_FSYNC);
	commit_lock_file(&lk);

	free(oids.list);
	oids.alloc = 0;
	oids.nr = 0;
}
