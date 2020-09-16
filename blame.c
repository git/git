#include "cache.h"
#include "refs.h"
#include "object-store.h"
#include "cache-tree.h"
#include "mergesort.h"
#include "diff.h"
#include "diffcore.h"
#include "tag.h"
#include "blame.h"
#include "alloc.h"
#include "commit-slab.h"
#include "bloom.h"
#include "commit-graph.h"

define_commit_slab(blame_suspects, struct blame_origin *);
static struct blame_suspects blame_suspects;

struct blame_origin *get_blame_suspects(struct commit *commit)
{
	struct blame_origin **result;

	result = blame_suspects_peek(&blame_suspects, commit);

	return result ? *result : NULL;
}

static void set_blame_suspects(struct commit *commit, struct blame_origin *origin)
{
	*blame_suspects_at(&blame_suspects, commit) = origin;
}

void blame_origin_decref(struct blame_origin *o)
{
	if (o && --o->refcnt <= 0) {
		struct blame_origin *p, *l = NULL;
		if (o->previous)
			blame_origin_decref(o->previous);
		free(o->file.ptr);
		/* Should be present exactly once in commit chain */
		for (p = get_blame_suspects(o->commit); p; l = p, p = p->next) {
			if (p == o) {
				if (l)
					l->next = p->next;
				else
					set_blame_suspects(o->commit, p->next);
				free(o);
				return;
			}
		}
		die("internal error in blame_origin_decref");
	}
}

/*
 * Given a commit and a path in it, create a new origin structure.
 * The callers that add blame to the scoreboard should use
 * get_origin() to obtain shared, refcounted copy instead of calling
 * this function directly.
 */
static struct blame_origin *make_origin(struct commit *commit, const char *path)
{
	struct blame_origin *o;
	FLEX_ALLOC_STR(o, path, path);
	o->commit = commit;
	o->refcnt = 1;
	o->next = get_blame_suspects(commit);
	set_blame_suspects(commit, o);
	return o;
}

/*
 * Locate an existing origin or create a new one.
 * This moves the origin to front position in the commit util list.
 */
static struct blame_origin *get_origin(struct commit *commit, const char *path)
{
	struct blame_origin *o, *l;

	for (o = get_blame_suspects(commit), l = NULL; o; l = o, o = o->next) {
		if (!strcmp(o->path, path)) {
			/* bump to front */
			if (l) {
				l->next = o->next;
				o->next = get_blame_suspects(commit);
				set_blame_suspects(commit, o);
			}
			return blame_origin_incref(o);
		}
	}
	return make_origin(commit, path);
}



static void verify_working_tree_path(struct repository *r,
				     struct commit *work_tree, const char *path)
{
	struct commit_list *parents;
	int pos;

	for (parents = work_tree->parents; parents; parents = parents->next) {
		const struct object_id *commit_oid = &parents->item->object.oid;
		struct object_id blob_oid;
		unsigned short mode;

		if (!get_tree_entry(r, commit_oid, path, &blob_oid, &mode) &&
		    oid_object_info(r, &blob_oid, NULL) == OBJ_BLOB)
			return;
	}

	pos = index_name_pos(r->index, path, strlen(path));
	if (pos >= 0)
		; /* path is in the index */
	else if (-1 - pos < r->index->cache_nr &&
		 !strcmp(r->index->cache[-1 - pos]->name, path))
		; /* path is in the index, unmerged */
	else
		die("no such path '%s' in HEAD", path);
}

static struct commit_list **append_parent(struct repository *r,
					  struct commit_list **tail,
					  const struct object_id *oid)
{
	struct commit *parent;

	parent = lookup_commit_reference(r, oid);
	if (!parent)
		die("no such commit %s", oid_to_hex(oid));
	return &commit_list_insert(parent, tail)->next;
}

static void append_merge_parents(struct repository *r,
				 struct commit_list **tail)
{
	int merge_head;
	struct strbuf line = STRBUF_INIT;

	merge_head = open(git_path_merge_head(r), O_RDONLY);
	if (merge_head < 0) {
		if (errno == ENOENT)
			return;
		die("cannot open '%s' for reading",
		    git_path_merge_head(r));
	}

	while (!strbuf_getwholeline_fd(&line, merge_head, '\n')) {
		struct object_id oid;
		if (get_oid_hex(line.buf, &oid))
			die("unknown line in '%s': %s",
			    git_path_merge_head(r), line.buf);
		tail = append_parent(r, tail, &oid);
	}
	close(merge_head);
	strbuf_release(&line);
}

/*
 * This isn't as simple as passing sb->buf and sb->len, because we
 * want to transfer ownership of the buffer to the commit (so we
 * must use detach).
 */
static void set_commit_buffer_from_strbuf(struct repository *r,
					  struct commit *c,
					  struct strbuf *sb)
{
	size_t len;
	void *buf = strbuf_detach(sb, &len);
	set_commit_buffer(r, c, buf, len);
}

/*
 * Prepare a dummy commit that represents the work tree (or staged) item.
 * Note that annotating work tree item never works in the reverse.
 */
static struct commit *fake_working_tree_commit(struct repository *r,
					       struct diff_options *opt,
					       const char *path,
					       const char *contents_from)
{
	struct commit *commit;
	struct blame_origin *origin;
	struct commit_list **parent_tail, *parent;
	struct object_id head_oid;
	struct strbuf buf = STRBUF_INIT;
	const char *ident;
	time_t now;
	int len;
	struct cache_entry *ce;
	unsigned mode;
	struct strbuf msg = STRBUF_INIT;

	repo_read_index(r);
	time(&now);
	commit = alloc_commit_node(r);
	commit->object.parsed = 1;
	commit->date = now;
	parent_tail = &commit->parents;

	if (!resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, &head_oid, NULL))
		die("no such ref: HEAD");

	parent_tail = append_parent(r, parent_tail, &head_oid);
	append_merge_parents(r, parent_tail);
	verify_working_tree_path(r, commit, path);

	origin = make_origin(commit, path);

	ident = fmt_ident("Not Committed Yet", "not.committed.yet",
			WANT_BLANK_IDENT, NULL, 0);
	strbuf_addstr(&msg, "tree 0000000000000000000000000000000000000000\n");
	for (parent = commit->parents; parent; parent = parent->next)
		strbuf_addf(&msg, "parent %s\n",
			    oid_to_hex(&parent->item->object.oid));
	strbuf_addf(&msg,
		    "author %s\n"
		    "committer %s\n\n"
		    "Version of %s from %s\n",
		    ident, ident, path,
		    (!contents_from ? path :
		     (!strcmp(contents_from, "-") ? "standard input" : contents_from)));
	set_commit_buffer_from_strbuf(r, commit, &msg);

	if (!contents_from || strcmp("-", contents_from)) {
		struct stat st;
		const char *read_from;
		char *buf_ptr;
		unsigned long buf_len;

		if (contents_from) {
			if (stat(contents_from, &st) < 0)
				die_errno("Cannot stat '%s'", contents_from);
			read_from = contents_from;
		}
		else {
			if (lstat(path, &st) < 0)
				die_errno("Cannot lstat '%s'", path);
			read_from = path;
		}
		mode = canon_mode(st.st_mode);

		switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			if (opt->flags.allow_textconv &&
			    textconv_object(r, read_from, mode, &null_oid, 0, &buf_ptr, &buf_len))
				strbuf_attach(&buf, buf_ptr, buf_len, buf_len + 1);
			else if (strbuf_read_file(&buf, read_from, st.st_size) != st.st_size)
				die_errno("cannot open or read '%s'", read_from);
			break;
		case S_IFLNK:
			if (strbuf_readlink(&buf, read_from, st.st_size) < 0)
				die_errno("cannot readlink '%s'", read_from);
			break;
		default:
			die("unsupported file type %s", read_from);
		}
	}
	else {
		/* Reading from stdin */
		mode = 0;
		if (strbuf_read(&buf, 0, 0) < 0)
			die_errno("failed to read from stdin");
	}
	convert_to_git(r->index, path, buf.buf, buf.len, &buf, 0);
	origin->file.ptr = buf.buf;
	origin->file.size = buf.len;
	pretend_object_file(buf.buf, buf.len, OBJ_BLOB, &origin->blob_oid);

	/*
	 * Read the current index, replace the path entry with
	 * origin->blob_sha1 without mucking with its mode or type
	 * bits; we are not going to write this index out -- we just
	 * want to run "diff-index --cached".
	 */
	discard_index(r->index);
	repo_read_index(r);

	len = strlen(path);
	if (!mode) {
		int pos = index_name_pos(r->index, path, len);
		if (0 <= pos)
			mode = r->index->cache[pos]->ce_mode;
		else
			/* Let's not bother reading from HEAD tree */
			mode = S_IFREG | 0644;
	}
	ce = make_empty_cache_entry(r->index, len);
	oidcpy(&ce->oid, &origin->blob_oid);
	memcpy(ce->name, path, len);
	ce->ce_flags = create_ce_flags(0);
	ce->ce_namelen = len;
	ce->ce_mode = create_ce_mode(mode);
	add_index_entry(r->index, ce,
			ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE);

	cache_tree_invalidate_path(r->index, path);

	return commit;
}



static int diff_hunks(mmfile_t *file_a, mmfile_t *file_b,
		      xdl_emit_hunk_consume_func_t hunk_func, void *cb_data, int xdl_opts)
{
	xpparam_t xpp = {0};
	xdemitconf_t xecfg = {0};
	xdemitcb_t ecb = {NULL};

	xpp.flags = xdl_opts;
	xecfg.hunk_func = hunk_func;
	ecb.priv = cb_data;
	return xdi_diff(file_a, file_b, &xpp, &xecfg, &ecb);
}

static const char *get_next_line(const char *start, const char *end)
{
	const char *nl = memchr(start, '\n', end - start);

	return nl ? nl + 1 : end;
}

static int find_line_starts(int **line_starts, const char *buf,
			    unsigned long len)
{
	const char *end = buf + len;
	const char *p;
	int *lineno;
	int num = 0;

	for (p = buf; p < end; p = get_next_line(p, end))
		num++;

	ALLOC_ARRAY(*line_starts, num + 1);
	lineno = *line_starts;

	for (p = buf; p < end; p = get_next_line(p, end))
		*lineno++ = p - buf;

	*lineno = len;

	return num;
}

struct fingerprint_entry;

/* A fingerprint is intended to loosely represent a string, such that two
 * fingerprints can be quickly compared to give an indication of the similarity
 * of the strings that they represent.
 *
 * A fingerprint is represented as a multiset of the lower-cased byte pairs in
 * the string that it represents. Whitespace is added at each end of the
 * string. Whitespace pairs are ignored. Whitespace is converted to '\0'.
 * For example, the string "Darth   Radar" will be converted to the following
 * fingerprint:
 * {"\0d", "da", "da", "ar", "ar", "rt", "th", "h\0", "\0r", "ra", "ad", "r\0"}
 *
 * The similarity between two fingerprints is the size of the intersection of
 * their multisets, including repeated elements. See fingerprint_similarity for
 * examples.
 *
 * For ease of implementation, the fingerprint is implemented as a map
 * of byte pairs to the count of that byte pair in the string, instead of
 * allowing repeated elements in a set.
 */
struct fingerprint {
	struct hashmap map;
	/* As we know the maximum number of entries in advance, it's
	 * convenient to store the entries in a single array instead of having
	 * the hashmap manage the memory.
	 */
	struct fingerprint_entry *entries;
};

/* A byte pair in a fingerprint. Stores the number of times the byte pair
 * occurs in the string that the fingerprint represents.
 */
struct fingerprint_entry {
	/* The hashmap entry - the hash represents the byte pair in its
	 * entirety so we don't need to store the byte pair separately.
	 */
	struct hashmap_entry entry;
	/* The number of times the byte pair occurs in the string that the
	 * fingerprint represents.
	 */
	int count;
};

/* See `struct fingerprint` for an explanation of what a fingerprint is.
 * \param result the fingerprint of the string is stored here. This must be
 * 		 freed later using free_fingerprint.
 * \param line_begin the start of the string
 * \param line_end the end of the string
 */
static void get_fingerprint(struct fingerprint *result,
			    const char *line_begin,
			    const char *line_end)
{
	unsigned int hash, c0 = 0, c1;
	const char *p;
	int max_map_entry_count = 1 + line_end - line_begin;
	struct fingerprint_entry *entry = xcalloc(max_map_entry_count,
		sizeof(struct fingerprint_entry));
	struct fingerprint_entry *found_entry;

	hashmap_init(&result->map, NULL, NULL, max_map_entry_count);
	result->entries = entry;
	for (p = line_begin; p <= line_end; ++p, c0 = c1) {
		/* Always terminate the string with whitespace.
		 * Normalise whitespace to 0, and normalise letters to
		 * lower case. This won't work for multibyte characters but at
		 * worst will match some unrelated characters.
		 */
		if ((p == line_end) || isspace(*p))
			c1 = 0;
		else
			c1 = tolower(*p);
		hash = c0 | (c1 << 8);
		/* Ignore whitespace pairs */
		if (hash == 0)
			continue;
		hashmap_entry_init(&entry->entry, hash);

		found_entry = hashmap_get_entry(&result->map, entry,
						/* member name */ entry, NULL);
		if (found_entry) {
			found_entry->count += 1;
		} else {
			entry->count = 1;
			hashmap_add(&result->map, &entry->entry);
			++entry;
		}
	}
}

static void free_fingerprint(struct fingerprint *f)
{
	hashmap_free(&f->map);
	free(f->entries);
}

/* Calculates the similarity between two fingerprints as the size of the
 * intersection of their multisets, including repeated elements. See
 * `struct fingerprint` for an explanation of the fingerprint representation.
 * The similarity between "cat mat" and "father rather" is 2 because "at" is
 * present twice in both strings while the similarity between "tim" and "mit"
 * is 0.
 */
static int fingerprint_similarity(struct fingerprint *a, struct fingerprint *b)
{
	int intersection = 0;
	struct hashmap_iter iter;
	const struct fingerprint_entry *entry_a, *entry_b;

	hashmap_for_each_entry(&b->map, &iter, entry_b,
				entry /* member name */) {
		entry_a = hashmap_get_entry(&a->map, entry_b, entry, NULL);
		if (entry_a) {
			intersection += entry_a->count < entry_b->count ?
					entry_a->count : entry_b->count;
		}
	}
	return intersection;
}

/* Subtracts byte-pair elements in B from A, modifying A in place.
 */
static void fingerprint_subtract(struct fingerprint *a, struct fingerprint *b)
{
	struct hashmap_iter iter;
	struct fingerprint_entry *entry_a;
	const struct fingerprint_entry *entry_b;

	hashmap_iter_init(&b->map, &iter);

	hashmap_for_each_entry(&b->map, &iter, entry_b,
				entry /* member name */) {
		entry_a = hashmap_get_entry(&a->map, entry_b, entry, NULL);
		if (entry_a) {
			if (entry_a->count <= entry_b->count)
				hashmap_remove(&a->map, &entry_b->entry, NULL);
			else
				entry_a->count -= entry_b->count;
		}
	}
}

/* Calculate fingerprints for a series of lines.
 * Puts the fingerprints in the fingerprints array, which must have been
 * preallocated to allow storing line_count elements.
 */
static void get_line_fingerprints(struct fingerprint *fingerprints,
				  const char *content, const int *line_starts,
				  long first_line, long line_count)
{
	int i;
	const char *linestart, *lineend;

	line_starts += first_line;
	for (i = 0; i < line_count; ++i) {
		linestart = content + line_starts[i];
		lineend = content + line_starts[i + 1];
		get_fingerprint(fingerprints + i, linestart, lineend);
	}
}

static void free_line_fingerprints(struct fingerprint *fingerprints,
				   int nr_fingerprints)
{
	int i;

	for (i = 0; i < nr_fingerprints; i++)
		free_fingerprint(&fingerprints[i]);
}

/* This contains the data necessary to linearly map a line number in one half
 * of a diff chunk to the line in the other half of the diff chunk that is
 * closest in terms of its position as a fraction of the length of the chunk.
 */
struct line_number_mapping {
	int destination_start, destination_length,
		source_start, source_length;
};

/* Given a line number in one range, offset and scale it to map it onto the
 * other range.
 * Essentially this mapping is a simple linear equation but the calculation is
 * more complicated to allow performing it with integer operations.
 * Another complication is that if a line could map onto many lines in the
 * destination range then we want to choose the line at the center of those
 * possibilities.
 * Example: if the chunk is 2 lines long in A and 10 lines long in B then the
 * first 5 lines in B will map onto the first line in the A chunk, while the
 * last 5 lines will all map onto the second line in the A chunk.
 * Example: if the chunk is 10 lines long in A and 2 lines long in B then line
 * 0 in B will map onto line 2 in A, and line 1 in B will map onto line 7 in A.
 */
static int map_line_number(int line_number,
	const struct line_number_mapping *mapping)
{
	return ((line_number - mapping->source_start) * 2 + 1) *
	       mapping->destination_length /
	       (mapping->source_length * 2) +
	       mapping->destination_start;
}

/* Get a pointer to the element storing the similarity between a line in A
 * and a line in B.
 *
 * The similarities are stored in a 2-dimensional array. Each "row" in the
 * array contains the similarities for a line in B. The similarities stored in
 * a row are the similarities between the line in B and the nearby lines in A.
 * To keep the length of each row the same, it is padded out with values of -1
 * where the search range extends beyond the lines in A.
 * For example, if max_search_distance_a is 2 and the two sides of a diff chunk
 * look like this:
 * a | m
 * b | n
 * c | o
 * d | p
 * e | q
 * Then the similarity array will contain:
 * [-1, -1, am, bm, cm,
 *  -1, an, bn, cn, dn,
 *  ao, bo, co, do, eo,
 *  bp, cp, dp, ep, -1,
 *  cq, dq, eq, -1, -1]
 * Where similarities are denoted either by -1 for invalid, or the
 * concatenation of the two lines in the diff being compared.
 *
 * \param similarities array of similarities between lines in A and B
 * \param line_a the index of the line in A, in the same frame of reference as
 *	closest_line_a.
 * \param local_line_b the index of the line in B, relative to the first line
 *		       in B that similarities represents.
 * \param closest_line_a the index of the line in A that is deemed to be
 *			 closest to local_line_b. This must be in the same
 *			 frame of reference as line_a. This value defines
 *			 where similarities is centered for the line in B.
 * \param max_search_distance_a maximum distance in lines from the closest line
 * 				in A for other lines in A for which
 * 				similarities may be calculated.
 */
static int *get_similarity(int *similarities,
			   int line_a, int local_line_b,
			   int closest_line_a, int max_search_distance_a)
{
	assert(abs(line_a - closest_line_a) <=
	       max_search_distance_a);
	return similarities + line_a - closest_line_a +
	       max_search_distance_a +
	       local_line_b * (max_search_distance_a * 2 + 1);
}

#define CERTAIN_NOTHING_MATCHES -2
#define CERTAINTY_NOT_CALCULATED -1

/* Given a line in B, first calculate its similarities with nearby lines in A
 * if not already calculated, then identify the most similar and second most
 * similar lines. The "certainty" is calculated based on those two
 * similarities.
 *
 * \param start_a the index of the first line of the chunk in A
 * \param length_a the length in lines of the chunk in A
 * \param local_line_b the index of the line in B, relative to the first line
 * 		       in the chunk.
 * \param fingerprints_a array of fingerprints for the chunk in A
 * \param fingerprints_b array of fingerprints for the chunk in B
 * \param similarities 2-dimensional array of similarities between lines in A
 * 		       and B. See get_similarity() for more details.
 * \param certainties array of values indicating how strongly a line in B is
 * 		      matched with some line in A.
 * \param second_best_result array of absolute indices in A for the second
 * 			     closest match of a line in B.
 * \param result array of absolute indices in A for the closest match of a line
 * 		 in B.
 * \param max_search_distance_a maximum distance in lines from the closest line
 * 				in A for other lines in A for which
 * 				similarities may be calculated.
 * \param map_line_number_in_b_to_a parameter to map_line_number().
 */
static void find_best_line_matches(
	int start_a,
	int length_a,
	int start_b,
	int local_line_b,
	struct fingerprint *fingerprints_a,
	struct fingerprint *fingerprints_b,
	int *similarities,
	int *certainties,
	int *second_best_result,
	int *result,
	const int max_search_distance_a,
	const struct line_number_mapping *map_line_number_in_b_to_a)
{

	int i, search_start, search_end, closest_local_line_a, *similarity,
		best_similarity = 0, second_best_similarity = 0,
		best_similarity_index = 0, second_best_similarity_index = 0;

	/* certainty has already been calculated so no need to redo the work */
	if (certainties[local_line_b] != CERTAINTY_NOT_CALCULATED)
		return;

	closest_local_line_a = map_line_number(
		local_line_b + start_b, map_line_number_in_b_to_a) - start_a;

	search_start = closest_local_line_a - max_search_distance_a;
	if (search_start < 0)
		search_start = 0;

	search_end = closest_local_line_a + max_search_distance_a + 1;
	if (search_end > length_a)
		search_end = length_a;

	for (i = search_start; i < search_end; ++i) {
		similarity = get_similarity(similarities,
					    i, local_line_b,
					    closest_local_line_a,
					    max_search_distance_a);
		if (*similarity == -1) {
			/* This value will never exceed 10 but assert just in
			 * case
			 */
			assert(abs(i - closest_local_line_a) < 1000);
			/* scale the similarity by (1000 - distance from
			 * closest line) to act as a tie break between lines
			 * that otherwise are equally similar.
			 */
			*similarity = fingerprint_similarity(
				fingerprints_b + local_line_b,
				fingerprints_a + i) *
				(1000 - abs(i - closest_local_line_a));
		}
		if (*similarity > best_similarity) {
			second_best_similarity = best_similarity;
			second_best_similarity_index = best_similarity_index;
			best_similarity = *similarity;
			best_similarity_index = i;
		} else if (*similarity > second_best_similarity) {
			second_best_similarity = *similarity;
			second_best_similarity_index = i;
		}
	}

	if (best_similarity == 0) {
		/* this line definitely doesn't match with anything. Mark it
		 * with this special value so it doesn't get invalidated and
		 * won't be recalculated.
		 */
		certainties[local_line_b] = CERTAIN_NOTHING_MATCHES;
		result[local_line_b] = -1;
	} else {
		/* Calculate the certainty with which this line matches.
		 * If the line matches well with two lines then that reduces
		 * the certainty. However we still want to prioritise matching
		 * a line that matches very well with two lines over matching a
		 * line that matches poorly with one line, hence doubling
		 * best_similarity.
		 * This means that if we have
		 * line X that matches only one line with a score of 3,
		 * line Y that matches two lines equally with a score of 5,
		 * and line Z that matches only one line with a score or 2,
		 * then the lines in order of certainty are X, Y, Z.
		 */
		certainties[local_line_b] = best_similarity * 2 -
			second_best_similarity;

		/* We keep both the best and second best results to allow us to
		 * check at a later stage of the matching process whether the
		 * result needs to be invalidated.
		 */
		result[local_line_b] = start_a + best_similarity_index;
		second_best_result[local_line_b] =
			start_a + second_best_similarity_index;
	}
}

/*
 * This finds the line that we can match with the most confidence, and
 * uses it as a partition. It then calls itself on the lines on either side of
 * that partition. In this way we avoid lines appearing out of order, and
 * retain a sensible line ordering.
 * \param start_a index of the first line in A with which lines in B may be
 * 		  compared.
 * \param start_b index of the first line in B for which matching should be
 * 		  done.
 * \param length_a number of lines in A with which lines in B may be compared.
 * \param length_b number of lines in B for which matching should be done.
 * \param fingerprints_a mutable array of fingerprints in A. The first element
 * 			 corresponds to the line at start_a.
 * \param fingerprints_b array of fingerprints in B. The first element
 * 			 corresponds to the line at start_b.
 * \param similarities 2-dimensional array of similarities between lines in A
 * 		       and B. See get_similarity() for more details.
 * \param certainties array of values indicating how strongly a line in B is
 * 		      matched with some line in A.
 * \param second_best_result array of absolute indices in A for the second
 * 			     closest match of a line in B.
 * \param result array of absolute indices in A for the closest match of a line
 * 		 in B.
 * \param max_search_distance_a maximum distance in lines from the closest line
 * 			      in A for other lines in A for which
 * 			      similarities may be calculated.
 * \param max_search_distance_b an upper bound on the greatest possible
 * 			      distance between lines in B such that they will
 *                              both be compared with the same line in A
 * 			      according to max_search_distance_a.
 * \param map_line_number_in_b_to_a parameter to map_line_number().
 */
static void fuzzy_find_matching_lines_recurse(
	int start_a, int start_b,
	int length_a, int length_b,
	struct fingerprint *fingerprints_a,
	struct fingerprint *fingerprints_b,
	int *similarities,
	int *certainties,
	int *second_best_result,
	int *result,
	int max_search_distance_a,
	int max_search_distance_b,
	const struct line_number_mapping *map_line_number_in_b_to_a)
{
	int i, invalidate_min, invalidate_max, offset_b,
		second_half_start_a, second_half_start_b,
		second_half_length_a, second_half_length_b,
		most_certain_line_a, most_certain_local_line_b = -1,
		most_certain_line_certainty = -1,
		closest_local_line_a;

	for (i = 0; i < length_b; ++i) {
		find_best_line_matches(start_a,
				       length_a,
				       start_b,
				       i,
				       fingerprints_a,
				       fingerprints_b,
				       similarities,
				       certainties,
				       second_best_result,
				       result,
				       max_search_distance_a,
				       map_line_number_in_b_to_a);

		if (certainties[i] > most_certain_line_certainty) {
			most_certain_line_certainty = certainties[i];
			most_certain_local_line_b = i;
		}
	}

	/* No matches. */
	if (most_certain_local_line_b == -1)
		return;

	most_certain_line_a = result[most_certain_local_line_b];

	/*
	 * Subtract the most certain line's fingerprint in B from the matched
	 * fingerprint in A. This means that other lines in B can't also match
	 * the same parts of the line in A.
	 */
	fingerprint_subtract(fingerprints_a + most_certain_line_a - start_a,
			     fingerprints_b + most_certain_local_line_b);

	/* Invalidate results that may be affected by the choice of most
	 * certain line.
	 */
	invalidate_min = most_certain_local_line_b - max_search_distance_b;
	invalidate_max = most_certain_local_line_b + max_search_distance_b + 1;
	if (invalidate_min < 0)
		invalidate_min = 0;
	if (invalidate_max > length_b)
		invalidate_max = length_b;

	/* As the fingerprint in A has changed, discard previously calculated
	 * similarity values with that fingerprint.
	 */
	for (i = invalidate_min; i < invalidate_max; ++i) {
		closest_local_line_a = map_line_number(
			i + start_b, map_line_number_in_b_to_a) - start_a;

		/* Check that the lines in A and B are close enough that there
		 * is a similarity value for them.
		 */
		if (abs(most_certain_line_a - start_a - closest_local_line_a) >
			max_search_distance_a) {
			continue;
		}

		*get_similarity(similarities, most_certain_line_a - start_a,
				i, closest_local_line_a,
				max_search_distance_a) = -1;
	}

	/* More invalidating of results that may be affected by the choice of
	 * most certain line.
	 * Discard the matches for lines in B that are currently matched with a
	 * line in A such that their ordering contradicts the ordering imposed
	 * by the choice of most certain line.
	 */
	for (i = most_certain_local_line_b - 1; i >= invalidate_min; --i) {
		/* In this loop we discard results for lines in B that are
		 * before most-certain-line-B but are matched with a line in A
		 * that is after most-certain-line-A.
		 */
		if (certainties[i] >= 0 &&
		    (result[i] >= most_certain_line_a ||
		     second_best_result[i] >= most_certain_line_a)) {
			certainties[i] = CERTAINTY_NOT_CALCULATED;
		}
	}
	for (i = most_certain_local_line_b + 1; i < invalidate_max; ++i) {
		/* In this loop we discard results for lines in B that are
		 * after most-certain-line-B but are matched with a line in A
		 * that is before most-certain-line-A.
		 */
		if (certainties[i] >= 0 &&
		    (result[i] <= most_certain_line_a ||
		     second_best_result[i] <= most_certain_line_a)) {
			certainties[i] = CERTAINTY_NOT_CALCULATED;
		}
	}

	/* Repeat the matching process for lines before the most certain line.
	 */
	if (most_certain_local_line_b > 0) {
		fuzzy_find_matching_lines_recurse(
			start_a, start_b,
			most_certain_line_a + 1 - start_a,
			most_certain_local_line_b,
			fingerprints_a, fingerprints_b, similarities,
			certainties, second_best_result, result,
			max_search_distance_a,
			max_search_distance_b,
			map_line_number_in_b_to_a);
	}
	/* Repeat the matching process for lines after the most certain line.
	 */
	if (most_certain_local_line_b + 1 < length_b) {
		second_half_start_a = most_certain_line_a;
		offset_b = most_certain_local_line_b + 1;
		second_half_start_b = start_b + offset_b;
		second_half_length_a =
			length_a + start_a - second_half_start_a;
		second_half_length_b =
			length_b + start_b - second_half_start_b;
		fuzzy_find_matching_lines_recurse(
			second_half_start_a, second_half_start_b,
			second_half_length_a, second_half_length_b,
			fingerprints_a + second_half_start_a - start_a,
			fingerprints_b + offset_b,
			similarities +
				offset_b * (max_search_distance_a * 2 + 1),
			certainties + offset_b,
			second_best_result + offset_b, result + offset_b,
			max_search_distance_a,
			max_search_distance_b,
			map_line_number_in_b_to_a);
	}
}

/* Find the lines in the parent line range that most closely match the lines in
 * the target line range. This is accomplished by matching fingerprints in each
 * blame_origin, and choosing the best matches that preserve the line ordering.
 * See struct fingerprint for details of fingerprint matching, and
 * fuzzy_find_matching_lines_recurse for details of preserving line ordering.
 *
 * The performance is believed to be O(n log n) in the typical case and O(n^2)
 * in a pathological case, where n is the number of lines in the target range.
 */
static int *fuzzy_find_matching_lines(struct blame_origin *parent,
				      struct blame_origin *target,
				      int tlno, int parent_slno, int same,
				      int parent_len)
{
	/* We use the terminology "A" for the left hand side of the diff AKA
	 * parent, and "B" for the right hand side of the diff AKA target. */
	int start_a = parent_slno;
	int length_a = parent_len;
	int start_b = tlno;
	int length_b = same - tlno;

	struct line_number_mapping map_line_number_in_b_to_a = {
		start_a, length_a, start_b, length_b
	};

	struct fingerprint *fingerprints_a = parent->fingerprints;
	struct fingerprint *fingerprints_b = target->fingerprints;

	int i, *result, *second_best_result,
		*certainties, *similarities, similarity_count;

	/*
	 * max_search_distance_a means that given a line in B, compare it to
	 * the line in A that is closest to its position, and the lines in A
	 * that are no greater than max_search_distance_a lines away from the
	 * closest line in A.
	 *
	 * max_search_distance_b is an upper bound on the greatest possible
	 * distance between lines in B such that they will both be compared
	 * with the same line in A according to max_search_distance_a.
	 */
	int max_search_distance_a = 10, max_search_distance_b;

	if (length_a <= 0)
		return NULL;

	if (max_search_distance_a >= length_a)
		max_search_distance_a = length_a ? length_a - 1 : 0;

	max_search_distance_b = ((2 * max_search_distance_a + 1) * length_b
				 - 1) / length_a;

	result = xcalloc(sizeof(int), length_b);
	second_best_result = xcalloc(sizeof(int), length_b);
	certainties = xcalloc(sizeof(int), length_b);

	/* See get_similarity() for details of similarities. */
	similarity_count = length_b * (max_search_distance_a * 2 + 1);
	similarities = xcalloc(sizeof(int), similarity_count);

	for (i = 0; i < length_b; ++i) {
		result[i] = -1;
		second_best_result[i] = -1;
		certainties[i] = CERTAINTY_NOT_CALCULATED;
	}

	for (i = 0; i < similarity_count; ++i)
		similarities[i] = -1;

	fuzzy_find_matching_lines_recurse(start_a, start_b,
					  length_a, length_b,
					  fingerprints_a + start_a,
					  fingerprints_b + start_b,
					  similarities,
					  certainties,
					  second_best_result,
					  result,
					  max_search_distance_a,
					  max_search_distance_b,
					  &map_line_number_in_b_to_a);

	free(similarities);
	free(certainties);
	free(second_best_result);

	return result;
}

static void fill_origin_fingerprints(struct blame_origin *o)
{
	int *line_starts;

	if (o->fingerprints)
		return;
	o->num_lines = find_line_starts(&line_starts, o->file.ptr,
					o->file.size);
	o->fingerprints = xcalloc(sizeof(struct fingerprint), o->num_lines);
	get_line_fingerprints(o->fingerprints, o->file.ptr, line_starts,
			      0, o->num_lines);
	free(line_starts);
}

static void drop_origin_fingerprints(struct blame_origin *o)
{
	if (o->fingerprints) {
		free_line_fingerprints(o->fingerprints, o->num_lines);
		o->num_lines = 0;
		FREE_AND_NULL(o->fingerprints);
	}
}

/*
 * Given an origin, prepare mmfile_t structure to be used by the
 * diff machinery
 */
static void fill_origin_blob(struct diff_options *opt,
			     struct blame_origin *o, mmfile_t *file,
			     int *num_read_blob, int fill_fingerprints)
{
	if (!o->file.ptr) {
		enum object_type type;
		unsigned long file_size;

		(*num_read_blob)++;
		if (opt->flags.allow_textconv &&
		    textconv_object(opt->repo, o->path, o->mode,
				    &o->blob_oid, 1, &file->ptr, &file_size))
			;
		else
			file->ptr = read_object_file(&o->blob_oid, &type,
						     &file_size);
		file->size = file_size;

		if (!file->ptr)
			die("Cannot read blob %s for path %s",
			    oid_to_hex(&o->blob_oid),
			    o->path);
		o->file = *file;
	}
	else
		*file = o->file;
	if (fill_fingerprints)
		fill_origin_fingerprints(o);
}

static void drop_origin_blob(struct blame_origin *o)
{
	FREE_AND_NULL(o->file.ptr);
	drop_origin_fingerprints(o);
}

/*
 * Any merge of blames happens on lists of blames that arrived via
 * different parents in a single suspect.  In this case, we want to
 * sort according to the suspect line numbers as opposed to the final
 * image line numbers.  The function body is somewhat longish because
 * it avoids unnecessary writes.
 */

static struct blame_entry *blame_merge(struct blame_entry *list1,
				       struct blame_entry *list2)
{
	struct blame_entry *p1 = list1, *p2 = list2,
		**tail = &list1;

	if (!p1)
		return p2;
	if (!p2)
		return p1;

	if (p1->s_lno <= p2->s_lno) {
		do {
			tail = &p1->next;
			if ((p1 = *tail) == NULL) {
				*tail = p2;
				return list1;
			}
		} while (p1->s_lno <= p2->s_lno);
	}
	for (;;) {
		*tail = p2;
		do {
			tail = &p2->next;
			if ((p2 = *tail) == NULL)  {
				*tail = p1;
				return list1;
			}
		} while (p1->s_lno > p2->s_lno);
		*tail = p1;
		do {
			tail = &p1->next;
			if ((p1 = *tail) == NULL) {
				*tail = p2;
				return list1;
			}
		} while (p1->s_lno <= p2->s_lno);
	}
}

static void *get_next_blame(const void *p)
{
	return ((struct blame_entry *)p)->next;
}

static void set_next_blame(void *p1, void *p2)
{
	((struct blame_entry *)p1)->next = p2;
}

/*
 * Final image line numbers are all different, so we don't need a
 * three-way comparison here.
 */

static int compare_blame_final(const void *p1, const void *p2)
{
	return ((struct blame_entry *)p1)->lno > ((struct blame_entry *)p2)->lno
		? 1 : -1;
}

static int compare_blame_suspect(const void *p1, const void *p2)
{
	const struct blame_entry *s1 = p1, *s2 = p2;
	/*
	 * to allow for collating suspects, we sort according to the
	 * respective pointer value as the primary sorting criterion.
	 * The actual relation is pretty unimportant as long as it
	 * establishes a total order.  Comparing as integers gives us
	 * that.
	 */
	if (s1->suspect != s2->suspect)
		return (intptr_t)s1->suspect > (intptr_t)s2->suspect ? 1 : -1;
	if (s1->s_lno == s2->s_lno)
		return 0;
	return s1->s_lno > s2->s_lno ? 1 : -1;
}

void blame_sort_final(struct blame_scoreboard *sb)
{
	sb->ent = llist_mergesort(sb->ent, get_next_blame, set_next_blame,
				  compare_blame_final);
}

static int compare_commits_by_reverse_commit_date(const void *a,
						  const void *b,
						  void *c)
{
	return -compare_commits_by_commit_date(a, b, c);
}

/*
 * For debugging -- origin is refcounted, and this asserts that
 * we do not underflow.
 */
static void sanity_check_refcnt(struct blame_scoreboard *sb)
{
	int baa = 0;
	struct blame_entry *ent;

	for (ent = sb->ent; ent; ent = ent->next) {
		/* Nobody should have zero or negative refcnt */
		if (ent->suspect->refcnt <= 0) {
			fprintf(stderr, "%s in %s has negative refcnt %d\n",
				ent->suspect->path,
				oid_to_hex(&ent->suspect->commit->object.oid),
				ent->suspect->refcnt);
			baa = 1;
		}
	}
	if (baa)
		sb->on_sanity_fail(sb, baa);
}

/*
 * If two blame entries that are next to each other came from
 * contiguous lines in the same origin (i.e. <commit, path> pair),
 * merge them together.
 */
void blame_coalesce(struct blame_scoreboard *sb)
{
	struct blame_entry *ent, *next;

	for (ent = sb->ent; ent && (next = ent->next); ent = next) {
		if (ent->suspect == next->suspect &&
		    ent->s_lno + ent->num_lines == next->s_lno &&
		    ent->ignored == next->ignored &&
		    ent->unblamable == next->unblamable) {
			ent->num_lines += next->num_lines;
			ent->next = next->next;
			blame_origin_decref(next->suspect);
			free(next);
			ent->score = 0;
			next = ent; /* again */
		}
	}

	if (sb->debug) /* sanity */
		sanity_check_refcnt(sb);
}

/*
 * Merge the given sorted list of blames into a preexisting origin.
 * If there were no previous blames to that commit, it is entered into
 * the commit priority queue of the score board.
 */

static void queue_blames(struct blame_scoreboard *sb, struct blame_origin *porigin,
			 struct blame_entry *sorted)
{
	if (porigin->suspects)
		porigin->suspects = blame_merge(porigin->suspects, sorted);
	else {
		struct blame_origin *o;
		for (o = get_blame_suspects(porigin->commit); o; o = o->next) {
			if (o->suspects) {
				porigin->suspects = sorted;
				return;
			}
		}
		porigin->suspects = sorted;
		prio_queue_put(&sb->commits, porigin->commit);
	}
}

/*
 * Fill the blob_sha1 field of an origin if it hasn't, so that later
 * call to fill_origin_blob() can use it to locate the data.  blob_sha1
 * for an origin is also used to pass the blame for the entire file to
 * the parent to detect the case where a child's blob is identical to
 * that of its parent's.
 *
 * This also fills origin->mode for corresponding tree path.
 */
static int fill_blob_sha1_and_mode(struct repository *r,
				   struct blame_origin *origin)
{
	if (!is_null_oid(&origin->blob_oid))
		return 0;
	if (get_tree_entry(r, &origin->commit->object.oid, origin->path, &origin->blob_oid, &origin->mode))
		goto error_out;
	if (oid_object_info(r, &origin->blob_oid, NULL) != OBJ_BLOB)
		goto error_out;
	return 0;
 error_out:
	oidclr(&origin->blob_oid);
	origin->mode = S_IFINVALID;
	return -1;
}

struct blame_bloom_data {
	/*
	 * Changed-path Bloom filter keys. These can help prevent
	 * computing diffs against first parents, but we need to
	 * expand the list as code is moved or files are renamed.
	 */
	struct bloom_filter_settings *settings;
	struct bloom_key **keys;
	int nr;
	int alloc;
};

static int bloom_count_queries = 0;
static int bloom_count_no = 0;
static int maybe_changed_path(struct repository *r,
			      struct blame_origin *origin,
			      struct blame_bloom_data *bd)
{
	int i;
	struct bloom_filter *filter;

	if (!bd)
		return 1;

	if (commit_graph_generation(origin->commit) == GENERATION_NUMBER_INFINITY)
		return 1;

	filter = get_bloom_filter(r, origin->commit);

	if (!filter)
		return 1;

	bloom_count_queries++;
	for (i = 0; i < bd->nr; i++) {
		if (bloom_filter_contains(filter,
					  bd->keys[i],
					  bd->settings))
			return 1;
	}

	bloom_count_no++;
	return 0;
}

static void add_bloom_key(struct blame_bloom_data *bd,
			  const char *path)
{
	if (!bd)
		return;

	if (bd->nr >= bd->alloc) {
		bd->alloc *= 2;
		REALLOC_ARRAY(bd->keys, bd->alloc);
	}

	bd->keys[bd->nr] = xmalloc(sizeof(struct bloom_key));
	fill_bloom_key(path, strlen(path), bd->keys[bd->nr], bd->settings);
	bd->nr++;
}

/*
 * We have an origin -- check if the same path exists in the
 * parent and return an origin structure to represent it.
 */
static struct blame_origin *find_origin(struct repository *r,
					struct commit *parent,
					struct blame_origin *origin,
					struct blame_bloom_data *bd)
{
	struct blame_origin *porigin;
	struct diff_options diff_opts;
	const char *paths[2];

	/* First check any existing origins */
	for (porigin = get_blame_suspects(parent); porigin; porigin = porigin->next)
		if (!strcmp(porigin->path, origin->path)) {
			/*
			 * The same path between origin and its parent
			 * without renaming -- the most common case.
			 */
			return blame_origin_incref (porigin);
		}

	/* See if the origin->path is different between parent
	 * and origin first.  Most of the time they are the
	 * same and diff-tree is fairly efficient about this.
	 */
	repo_diff_setup(r, &diff_opts);
	diff_opts.flags.recursive = 1;
	diff_opts.detect_rename = 0;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	paths[0] = origin->path;
	paths[1] = NULL;

	parse_pathspec(&diff_opts.pathspec,
		       PATHSPEC_ALL_MAGIC & ~PATHSPEC_LITERAL,
		       PATHSPEC_LITERAL_PATH, "", paths);
	diff_setup_done(&diff_opts);

	if (is_null_oid(&origin->commit->object.oid))
		do_diff_cache(get_commit_tree_oid(parent), &diff_opts);
	else {
		int compute_diff = 1;
		if (origin->commit->parents &&
		    !oidcmp(&parent->object.oid,
			    &origin->commit->parents->item->object.oid))
			compute_diff = maybe_changed_path(r, origin, bd);

		if (compute_diff)
			diff_tree_oid(get_commit_tree_oid(parent),
				      get_commit_tree_oid(origin->commit),
				      "", &diff_opts);
	}
	diffcore_std(&diff_opts);

	if (!diff_queued_diff.nr) {
		/* The path is the same as parent */
		porigin = get_origin(parent, origin->path);
		oidcpy(&porigin->blob_oid, &origin->blob_oid);
		porigin->mode = origin->mode;
	} else {
		/*
		 * Since origin->path is a pathspec, if the parent
		 * commit had it as a directory, we will see a whole
		 * bunch of deletion of files in the directory that we
		 * do not care about.
		 */
		int i;
		struct diff_filepair *p = NULL;
		for (i = 0; i < diff_queued_diff.nr; i++) {
			const char *name;
			p = diff_queued_diff.queue[i];
			name = p->one->path ? p->one->path : p->two->path;
			if (!strcmp(name, origin->path))
				break;
		}
		if (!p)
			die("internal error in blame::find_origin");
		switch (p->status) {
		default:
			die("internal error in blame::find_origin (%c)",
			    p->status);
		case 'M':
			porigin = get_origin(parent, origin->path);
			oidcpy(&porigin->blob_oid, &p->one->oid);
			porigin->mode = p->one->mode;
			break;
		case 'A':
		case 'T':
			/* Did not exist in parent, or type changed */
			break;
		}
	}
	diff_flush(&diff_opts);
	clear_pathspec(&diff_opts.pathspec);
	return porigin;
}

/*
 * We have an origin -- find the path that corresponds to it in its
 * parent and return an origin structure to represent it.
 */
static struct blame_origin *find_rename(struct repository *r,
					struct commit *parent,
					struct blame_origin *origin,
					struct blame_bloom_data *bd)
{
	struct blame_origin *porigin = NULL;
	struct diff_options diff_opts;
	int i;

	repo_diff_setup(r, &diff_opts);
	diff_opts.flags.recursive = 1;
	diff_opts.detect_rename = DIFF_DETECT_RENAME;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_opts.single_follow = origin->path;
	diff_setup_done(&diff_opts);

	if (is_null_oid(&origin->commit->object.oid))
		do_diff_cache(get_commit_tree_oid(parent), &diff_opts);
	else
		diff_tree_oid(get_commit_tree_oid(parent),
			      get_commit_tree_oid(origin->commit),
			      "", &diff_opts);
	diffcore_std(&diff_opts);

	for (i = 0; i < diff_queued_diff.nr; i++) {
		struct diff_filepair *p = diff_queued_diff.queue[i];
		if ((p->status == 'R' || p->status == 'C') &&
		    !strcmp(p->two->path, origin->path)) {
			add_bloom_key(bd, p->one->path);
			porigin = get_origin(parent, p->one->path);
			oidcpy(&porigin->blob_oid, &p->one->oid);
			porigin->mode = p->one->mode;
			break;
		}
	}
	diff_flush(&diff_opts);
	clear_pathspec(&diff_opts.pathspec);
	return porigin;
}

/*
 * Append a new blame entry to a given output queue.
 */
static void add_blame_entry(struct blame_entry ***queue,
			    const struct blame_entry *src)
{
	struct blame_entry *e = xmalloc(sizeof(*e));
	memcpy(e, src, sizeof(*e));
	blame_origin_incref(e->suspect);

	e->next = **queue;
	**queue = e;
	*queue = &e->next;
}

/*
 * src typically is on-stack; we want to copy the information in it to
 * a malloced blame_entry that gets added to the given queue.  The
 * origin of dst loses a refcnt.
 */
static void dup_entry(struct blame_entry ***queue,
		      struct blame_entry *dst, struct blame_entry *src)
{
	blame_origin_incref(src->suspect);
	blame_origin_decref(dst->suspect);
	memcpy(dst, src, sizeof(*src));
	dst->next = **queue;
	**queue = dst;
	*queue = &dst->next;
}

const char *blame_nth_line(struct blame_scoreboard *sb, long lno)
{
	return sb->final_buf + sb->lineno[lno];
}

/*
 * It is known that lines between tlno to same came from parent, and e
 * has an overlap with that range.  it also is known that parent's
 * line plno corresponds to e's line tlno.
 *
 *                <---- e ----->
 *                   <------>
 *                   <------------>
 *             <------------>
 *             <------------------>
 *
 * Split e into potentially three parts; before this chunk, the chunk
 * to be blamed for the parent, and after that portion.
 */
static void split_overlap(struct blame_entry *split,
			  struct blame_entry *e,
			  int tlno, int plno, int same,
			  struct blame_origin *parent)
{
	int chunk_end_lno;
	int i;
	memset(split, 0, sizeof(struct blame_entry [3]));

	for (i = 0; i < 3; i++) {
		split[i].ignored = e->ignored;
		split[i].unblamable = e->unblamable;
	}

	if (e->s_lno < tlno) {
		/* there is a pre-chunk part not blamed on parent */
		split[0].suspect = blame_origin_incref(e->suspect);
		split[0].lno = e->lno;
		split[0].s_lno = e->s_lno;
		split[0].num_lines = tlno - e->s_lno;
		split[1].lno = e->lno + tlno - e->s_lno;
		split[1].s_lno = plno;
	}
	else {
		split[1].lno = e->lno;
		split[1].s_lno = plno + (e->s_lno - tlno);
	}

	if (same < e->s_lno + e->num_lines) {
		/* there is a post-chunk part not blamed on parent */
		split[2].suspect = blame_origin_incref(e->suspect);
		split[2].lno = e->lno + (same - e->s_lno);
		split[2].s_lno = e->s_lno + (same - e->s_lno);
		split[2].num_lines = e->s_lno + e->num_lines - same;
		chunk_end_lno = split[2].lno;
	}
	else
		chunk_end_lno = e->lno + e->num_lines;
	split[1].num_lines = chunk_end_lno - split[1].lno;

	/*
	 * if it turns out there is nothing to blame the parent for,
	 * forget about the splitting.  !split[1].suspect signals this.
	 */
	if (split[1].num_lines < 1)
		return;
	split[1].suspect = blame_origin_incref(parent);
}

/*
 * split_overlap() divided an existing blame e into up to three parts
 * in split.  Any assigned blame is moved to queue to
 * reflect the split.
 */
static void split_blame(struct blame_entry ***blamed,
			struct blame_entry ***unblamed,
			struct blame_entry *split,
			struct blame_entry *e)
{
	if (split[0].suspect && split[2].suspect) {
		/* The first part (reuse storage for the existing entry e) */
		dup_entry(unblamed, e, &split[0]);

		/* The last part -- me */
		add_blame_entry(unblamed, &split[2]);

		/* ... and the middle part -- parent */
		add_blame_entry(blamed, &split[1]);
	}
	else if (!split[0].suspect && !split[2].suspect)
		/*
		 * The parent covers the entire area; reuse storage for
		 * e and replace it with the parent.
		 */
		dup_entry(blamed, e, &split[1]);
	else if (split[0].suspect) {
		/* me and then parent */
		dup_entry(unblamed, e, &split[0]);
		add_blame_entry(blamed, &split[1]);
	}
	else {
		/* parent and then me */
		dup_entry(blamed, e, &split[1]);
		add_blame_entry(unblamed, &split[2]);
	}
}

/*
 * After splitting the blame, the origins used by the
 * on-stack blame_entry should lose one refcnt each.
 */
static void decref_split(struct blame_entry *split)
{
	int i;

	for (i = 0; i < 3; i++)
		blame_origin_decref(split[i].suspect);
}

/*
 * reverse_blame reverses the list given in head, appending tail.
 * That allows us to build lists in reverse order, then reverse them
 * afterwards.  This can be faster than building the list in proper
 * order right away.  The reason is that building in proper order
 * requires writing a link in the _previous_ element, while building
 * in reverse order just requires placing the list head into the
 * _current_ element.
 */

static struct blame_entry *reverse_blame(struct blame_entry *head,
					 struct blame_entry *tail)
{
	while (head) {
		struct blame_entry *next = head->next;
		head->next = tail;
		tail = head;
		head = next;
	}
	return tail;
}

/*
 * Splits a blame entry into two entries at 'len' lines.  The original 'e'
 * consists of len lines, i.e. [e->lno, e->lno + len), and the second part,
 * which is returned, consists of the remainder: [e->lno + len, e->lno +
 * e->num_lines).  The caller needs to sort out the reference counting for the
 * new entry's suspect.
 */
static struct blame_entry *split_blame_at(struct blame_entry *e, int len,
					  struct blame_origin *new_suspect)
{
	struct blame_entry *n = xcalloc(1, sizeof(struct blame_entry));

	n->suspect = new_suspect;
	n->ignored = e->ignored;
	n->unblamable = e->unblamable;
	n->lno = e->lno + len;
	n->s_lno = e->s_lno + len;
	n->num_lines = e->num_lines - len;
	e->num_lines = len;
	e->score = 0;
	return n;
}

struct blame_line_tracker {
	int is_parent;
	int s_lno;
};

static int are_lines_adjacent(struct blame_line_tracker *first,
			      struct blame_line_tracker *second)
{
	return first->is_parent == second->is_parent &&
	       first->s_lno + 1 == second->s_lno;
}

static int scan_parent_range(struct fingerprint *p_fps,
			     struct fingerprint *t_fps, int t_idx,
			     int from, int nr_lines)
{
	int sim, p_idx;
	#define FINGERPRINT_FILE_THRESHOLD	10
	int best_sim_val = FINGERPRINT_FILE_THRESHOLD;
	int best_sim_idx = -1;

	for (p_idx = from; p_idx < from + nr_lines; p_idx++) {
		sim = fingerprint_similarity(&t_fps[t_idx], &p_fps[p_idx]);
		if (sim < best_sim_val)
			continue;
		/* Break ties with the closest-to-target line number */
		if (sim == best_sim_val && best_sim_idx != -1 &&
		    abs(best_sim_idx - t_idx) < abs(p_idx - t_idx))
			continue;
		best_sim_val = sim;
		best_sim_idx = p_idx;
	}
	return best_sim_idx;
}

/*
 * The first pass checks the blame entry (from the target) against the parent's
 * diff chunk.  If that fails for a line, the second pass tries to match that
 * line to any part of parent file.  That catches cases where a change was
 * broken into two chunks by 'context.'
 */
static void guess_line_blames(struct blame_origin *parent,
			      struct blame_origin *target,
			      int tlno, int offset, int same, int parent_len,
			      struct blame_line_tracker *line_blames)
{
	int i, best_idx, target_idx;
	int parent_slno = tlno + offset;
	int *fuzzy_matches;

	fuzzy_matches = fuzzy_find_matching_lines(parent, target,
						  tlno, parent_slno, same,
						  parent_len);
	for (i = 0; i < same - tlno; i++) {
		target_idx = tlno + i;
		if (fuzzy_matches && fuzzy_matches[i] >= 0) {
			best_idx = fuzzy_matches[i];
		} else {
			best_idx = scan_parent_range(parent->fingerprints,
						     target->fingerprints,
						     target_idx, 0,
						     parent->num_lines);
		}
		if (best_idx >= 0) {
			line_blames[i].is_parent = 1;
			line_blames[i].s_lno = best_idx;
		} else {
			line_blames[i].is_parent = 0;
			line_blames[i].s_lno = target_idx;
		}
	}
	free(fuzzy_matches);
}

/*
 * This decides which parts of a blame entry go to the parent (added to the
 * ignoredp list) and which stay with the target (added to the diffp list).  The
 * actual decision was made in a separate heuristic function, and those answers
 * for the lines in 'e' are in line_blames.  This consumes e, essentially
 * putting it on a list.
 *
 * Note that the blame entries on the ignoredp list are not necessarily sorted
 * with respect to the parent's line numbers yet.
 */
static void ignore_blame_entry(struct blame_entry *e,
			       struct blame_origin *parent,
			       struct blame_entry **diffp,
			       struct blame_entry **ignoredp,
			       struct blame_line_tracker *line_blames)
{
	int entry_len, nr_lines, i;

	/*
	 * We carve new entries off the front of e.  Each entry comes from a
	 * contiguous chunk of lines: adjacent lines from the same origin
	 * (either the parent or the target).
	 */
	entry_len = 1;
	nr_lines = e->num_lines;	/* e changes in the loop */
	for (i = 0; i < nr_lines; i++) {
		struct blame_entry *next = NULL;

		/*
		 * We are often adjacent to the next line - only split the blame
		 * entry when we have to.
		 */
		if (i + 1 < nr_lines) {
			if (are_lines_adjacent(&line_blames[i],
					       &line_blames[i + 1])) {
				entry_len++;
				continue;
			}
			next = split_blame_at(e, entry_len,
					      blame_origin_incref(e->suspect));
		}
		if (line_blames[i].is_parent) {
			e->ignored = 1;
			blame_origin_decref(e->suspect);
			e->suspect = blame_origin_incref(parent);
			e->s_lno = line_blames[i - entry_len + 1].s_lno;
			e->next = *ignoredp;
			*ignoredp = e;
		} else {
			e->unblamable = 1;
			/* e->s_lno is already in the target's address space. */
			e->next = *diffp;
			*diffp = e;
		}
		assert(e->num_lines == entry_len);
		e = next;
		entry_len = 1;
	}
	assert(!e);
}

/*
 * Process one hunk from the patch between the current suspect for
 * blame_entry e and its parent.  This first blames any unfinished
 * entries before the chunk (which is where target and parent start
 * differing) on the parent, and then splits blame entries at the
 * start and at the end of the difference region.  Since use of -M and
 * -C options may lead to overlapping/duplicate source line number
 * ranges, all we can rely on from sorting/merging is the order of the
 * first suspect line number.
 *
 * tlno: line number in the target where this chunk begins
 * same: line number in the target where this chunk ends
 * offset: add to tlno to get the chunk starting point in the parent
 * parent_len: number of lines in the parent chunk
 */
static void blame_chunk(struct blame_entry ***dstq, struct blame_entry ***srcq,
			int tlno, int offset, int same, int parent_len,
			struct blame_origin *parent,
			struct blame_origin *target, int ignore_diffs)
{
	struct blame_entry *e = **srcq;
	struct blame_entry *samep = NULL, *diffp = NULL, *ignoredp = NULL;
	struct blame_line_tracker *line_blames = NULL;

	while (e && e->s_lno < tlno) {
		struct blame_entry *next = e->next;
		/*
		 * current record starts before differing portion.  If
		 * it reaches into it, we need to split it up and
		 * examine the second part separately.
		 */
		if (e->s_lno + e->num_lines > tlno) {
			/* Move second half to a new record */
			struct blame_entry *n;

			n = split_blame_at(e, tlno - e->s_lno, e->suspect);
			/* Push new record to diffp */
			n->next = diffp;
			diffp = n;
		} else
			blame_origin_decref(e->suspect);
		/* Pass blame for everything before the differing
		 * chunk to the parent */
		e->suspect = blame_origin_incref(parent);
		e->s_lno += offset;
		e->next = samep;
		samep = e;
		e = next;
	}
	/*
	 * As we don't know how much of a common stretch after this
	 * diff will occur, the currently blamed parts are all that we
	 * can assign to the parent for now.
	 */

	if (samep) {
		**dstq = reverse_blame(samep, **dstq);
		*dstq = &samep->next;
	}
	/*
	 * Prepend the split off portions: everything after e starts
	 * after the blameable portion.
	 */
	e = reverse_blame(diffp, e);

	/*
	 * Now retain records on the target while parts are different
	 * from the parent.
	 */
	samep = NULL;
	diffp = NULL;

	if (ignore_diffs && same - tlno > 0) {
		line_blames = xcalloc(sizeof(struct blame_line_tracker),
				      same - tlno);
		guess_line_blames(parent, target, tlno, offset, same,
				  parent_len, line_blames);
	}

	while (e && e->s_lno < same) {
		struct blame_entry *next = e->next;

		/*
		 * If current record extends into sameness, need to split.
		 */
		if (e->s_lno + e->num_lines > same) {
			/*
			 * Move second half to a new record to be
			 * processed by later chunks
			 */
			struct blame_entry *n;

			n = split_blame_at(e, same - e->s_lno,
					   blame_origin_incref(e->suspect));
			/* Push new record to samep */
			n->next = samep;
			samep = n;
		}
		if (ignore_diffs) {
			ignore_blame_entry(e, parent, &diffp, &ignoredp,
					   line_blames + e->s_lno - tlno);
		} else {
			e->next = diffp;
			diffp = e;
		}
		e = next;
	}
	free(line_blames);
	if (ignoredp) {
		/*
		 * Note ignoredp is not sorted yet, and thus neither is dstq.
		 * That list must be sorted before we queue_blames().  We defer
		 * sorting until after all diff hunks are processed, so that
		 * guess_line_blames() can pick *any* line in the parent.  The
		 * slight drawback is that we end up sorting all blame entries
		 * passed to the parent, including those that are unrelated to
		 * changes made by the ignored commit.
		 */
		**dstq = reverse_blame(ignoredp, **dstq);
		*dstq = &ignoredp->next;
	}
	**srcq = reverse_blame(diffp, reverse_blame(samep, e));
	/* Move across elements that are in the unblamable portion */
	if (diffp)
		*srcq = &diffp->next;
}

struct blame_chunk_cb_data {
	struct blame_origin *parent;
	struct blame_origin *target;
	long offset;
	int ignore_diffs;
	struct blame_entry **dstq;
	struct blame_entry **srcq;
};

/* diff chunks are from parent to target */
static int blame_chunk_cb(long start_a, long count_a,
			  long start_b, long count_b, void *data)
{
	struct blame_chunk_cb_data *d = data;
	if (start_a - start_b != d->offset)
		die("internal error in blame::blame_chunk_cb");
	blame_chunk(&d->dstq, &d->srcq, start_b, start_a - start_b,
		    start_b + count_b, count_a, d->parent, d->target,
		    d->ignore_diffs);
	d->offset = start_a + count_a - (start_b + count_b);
	return 0;
}

/*
 * We are looking at the origin 'target' and aiming to pass blame
 * for the lines it is suspected to its parent.  Run diff to find
 * which lines came from parent and pass blame for them.
 */
static void pass_blame_to_parent(struct blame_scoreboard *sb,
				 struct blame_origin *target,
				 struct blame_origin *parent, int ignore_diffs)
{
	mmfile_t file_p, file_o;
	struct blame_chunk_cb_data d;
	struct blame_entry *newdest = NULL;

	if (!target->suspects)
		return; /* nothing remains for this target */

	d.parent = parent;
	d.target = target;
	d.offset = 0;
	d.ignore_diffs = ignore_diffs;
	d.dstq = &newdest; d.srcq = &target->suspects;

	fill_origin_blob(&sb->revs->diffopt, parent, &file_p,
			 &sb->num_read_blob, ignore_diffs);
	fill_origin_blob(&sb->revs->diffopt, target, &file_o,
			 &sb->num_read_blob, ignore_diffs);
	sb->num_get_patch++;

	if (diff_hunks(&file_p, &file_o, blame_chunk_cb, &d, sb->xdl_opts))
		die("unable to generate diff (%s -> %s)",
		    oid_to_hex(&parent->commit->object.oid),
		    oid_to_hex(&target->commit->object.oid));
	/* The rest are the same as the parent */
	blame_chunk(&d.dstq, &d.srcq, INT_MAX, d.offset, INT_MAX, 0,
		    parent, target, 0);
	*d.dstq = NULL;
	if (ignore_diffs)
		newdest = llist_mergesort(newdest, get_next_blame,
					  set_next_blame,
					  compare_blame_suspect);
	queue_blames(sb, parent, newdest);

	return;
}

/*
 * The lines in blame_entry after splitting blames many times can become
 * very small and trivial, and at some point it becomes pointless to
 * blame the parents.  E.g. "\t\t}\n\t}\n\n" appears everywhere in any
 * ordinary C program, and it is not worth to say it was copied from
 * totally unrelated file in the parent.
 *
 * Compute how trivial the lines in the blame_entry are.
 */
unsigned blame_entry_score(struct blame_scoreboard *sb, struct blame_entry *e)
{
	unsigned score;
	const char *cp, *ep;

	if (e->score)
		return e->score;

	score = 1;
	cp = blame_nth_line(sb, e->lno);
	ep = blame_nth_line(sb, e->lno + e->num_lines);
	while (cp < ep) {
		unsigned ch = *((unsigned char *)cp);
		if (isalnum(ch))
			score++;
		cp++;
	}
	e->score = score;
	return score;
}

/*
 * best_so_far[] and potential[] are both a split of an existing blame_entry
 * that passes blame to the parent.  Maintain best_so_far the best split so
 * far, by comparing potential and best_so_far and copying potential into
 * bst_so_far as needed.
 */
static void copy_split_if_better(struct blame_scoreboard *sb,
				 struct blame_entry *best_so_far,
				 struct blame_entry *potential)
{
	int i;

	if (!potential[1].suspect)
		return;
	if (best_so_far[1].suspect) {
		if (blame_entry_score(sb, &potential[1]) <
		    blame_entry_score(sb, &best_so_far[1]))
			return;
	}

	for (i = 0; i < 3; i++)
		blame_origin_incref(potential[i].suspect);
	decref_split(best_so_far);
	memcpy(best_so_far, potential, sizeof(struct blame_entry[3]));
}

/*
 * We are looking at a part of the final image represented by
 * ent (tlno and same are offset by ent->s_lno).
 * tlno is where we are looking at in the final image.
 * up to (but not including) same match preimage.
 * plno is where we are looking at in the preimage.
 *
 * <-------------- final image ---------------------->
 *       <------ent------>
 *         ^tlno ^same
 *    <---------preimage----->
 *         ^plno
 *
 * All line numbers are 0-based.
 */
static void handle_split(struct blame_scoreboard *sb,
			 struct blame_entry *ent,
			 int tlno, int plno, int same,
			 struct blame_origin *parent,
			 struct blame_entry *split)
{
	if (ent->num_lines <= tlno)
		return;
	if (tlno < same) {
		struct blame_entry potential[3];
		tlno += ent->s_lno;
		same += ent->s_lno;
		split_overlap(potential, ent, tlno, plno, same, parent);
		copy_split_if_better(sb, split, potential);
		decref_split(potential);
	}
}

struct handle_split_cb_data {
	struct blame_scoreboard *sb;
	struct blame_entry *ent;
	struct blame_origin *parent;
	struct blame_entry *split;
	long plno;
	long tlno;
};

static int handle_split_cb(long start_a, long count_a,
			   long start_b, long count_b, void *data)
{
	struct handle_split_cb_data *d = data;
	handle_split(d->sb, d->ent, d->tlno, d->plno, start_b, d->parent,
		     d->split);
	d->plno = start_a + count_a;
	d->tlno = start_b + count_b;
	return 0;
}

/*
 * Find the lines from parent that are the same as ent so that
 * we can pass blames to it.  file_p has the blob contents for
 * the parent.
 */
static void find_copy_in_blob(struct blame_scoreboard *sb,
			      struct blame_entry *ent,
			      struct blame_origin *parent,
			      struct blame_entry *split,
			      mmfile_t *file_p)
{
	const char *cp;
	mmfile_t file_o;
	struct handle_split_cb_data d;

	memset(&d, 0, sizeof(d));
	d.sb = sb; d.ent = ent; d.parent = parent; d.split = split;
	/*
	 * Prepare mmfile that contains only the lines in ent.
	 */
	cp = blame_nth_line(sb, ent->lno);
	file_o.ptr = (char *) cp;
	file_o.size = blame_nth_line(sb, ent->lno + ent->num_lines) - cp;

	/*
	 * file_o is a part of final image we are annotating.
	 * file_p partially may match that image.
	 */
	memset(split, 0, sizeof(struct blame_entry [3]));
	if (diff_hunks(file_p, &file_o, handle_split_cb, &d, sb->xdl_opts))
		die("unable to generate diff (%s)",
		    oid_to_hex(&parent->commit->object.oid));
	/* remainder, if any, all match the preimage */
	handle_split(sb, ent, d.tlno, d.plno, ent->num_lines, parent, split);
}

/* Move all blame entries from list *source that have a score smaller
 * than score_min to the front of list *small.
 * Returns a pointer to the link pointing to the old head of the small list.
 */

static struct blame_entry **filter_small(struct blame_scoreboard *sb,
					 struct blame_entry **small,
					 struct blame_entry **source,
					 unsigned score_min)
{
	struct blame_entry *p = *source;
	struct blame_entry *oldsmall = *small;
	while (p) {
		if (blame_entry_score(sb, p) <= score_min) {
			*small = p;
			small = &p->next;
			p = *small;
		} else {
			*source = p;
			source = &p->next;
			p = *source;
		}
	}
	*small = oldsmall;
	*source = NULL;
	return small;
}

/*
 * See if lines currently target is suspected for can be attributed to
 * parent.
 */
static void find_move_in_parent(struct blame_scoreboard *sb,
				struct blame_entry ***blamed,
				struct blame_entry **toosmall,
				struct blame_origin *target,
				struct blame_origin *parent)
{
	struct blame_entry *e, split[3];
	struct blame_entry *unblamed = target->suspects;
	struct blame_entry *leftover = NULL;
	mmfile_t file_p;

	if (!unblamed)
		return; /* nothing remains for this target */

	fill_origin_blob(&sb->revs->diffopt, parent, &file_p,
			 &sb->num_read_blob, 0);
	if (!file_p.ptr)
		return;

	/* At each iteration, unblamed has a NULL-terminated list of
	 * entries that have not yet been tested for blame.  leftover
	 * contains the reversed list of entries that have been tested
	 * without being assignable to the parent.
	 */
	do {
		struct blame_entry **unblamedtail = &unblamed;
		struct blame_entry *next;
		for (e = unblamed; e; e = next) {
			next = e->next;
			find_copy_in_blob(sb, e, parent, split, &file_p);
			if (split[1].suspect &&
			    sb->move_score < blame_entry_score(sb, &split[1])) {
				split_blame(blamed, &unblamedtail, split, e);
			} else {
				e->next = leftover;
				leftover = e;
			}
			decref_split(split);
		}
		*unblamedtail = NULL;
		toosmall = filter_small(sb, toosmall, &unblamed, sb->move_score);
	} while (unblamed);
	target->suspects = reverse_blame(leftover, NULL);
}

struct blame_list {
	struct blame_entry *ent;
	struct blame_entry split[3];
};

/*
 * Count the number of entries the target is suspected for,
 * and prepare a list of entry and the best split.
 */
static struct blame_list *setup_blame_list(struct blame_entry *unblamed,
					   int *num_ents_p)
{
	struct blame_entry *e;
	int num_ents, i;
	struct blame_list *blame_list = NULL;

	for (e = unblamed, num_ents = 0; e; e = e->next)
		num_ents++;
	if (num_ents) {
		blame_list = xcalloc(num_ents, sizeof(struct blame_list));
		for (e = unblamed, i = 0; e; e = e->next)
			blame_list[i++].ent = e;
	}
	*num_ents_p = num_ents;
	return blame_list;
}

/*
 * For lines target is suspected for, see if we can find code movement
 * across file boundary from the parent commit.  porigin is the path
 * in the parent we already tried.
 */
static void find_copy_in_parent(struct blame_scoreboard *sb,
				struct blame_entry ***blamed,
				struct blame_entry **toosmall,
				struct blame_origin *target,
				struct commit *parent,
				struct blame_origin *porigin,
				int opt)
{
	struct diff_options diff_opts;
	int i, j;
	struct blame_list *blame_list;
	int num_ents;
	struct blame_entry *unblamed = target->suspects;
	struct blame_entry *leftover = NULL;

	if (!unblamed)
		return; /* nothing remains for this target */

	repo_diff_setup(sb->repo, &diff_opts);
	diff_opts.flags.recursive = 1;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;

	diff_setup_done(&diff_opts);

	/* Try "find copies harder" on new path if requested;
	 * we do not want to use diffcore_rename() actually to
	 * match things up; find_copies_harder is set only to
	 * force diff_tree_oid() to feed all filepairs to diff_queue,
	 * and this code needs to be after diff_setup_done(), which
	 * usually makes find-copies-harder imply copy detection.
	 */
	if ((opt & PICKAXE_BLAME_COPY_HARDEST)
	    || ((opt & PICKAXE_BLAME_COPY_HARDER)
		&& (!porigin || strcmp(target->path, porigin->path))))
		diff_opts.flags.find_copies_harder = 1;

	if (is_null_oid(&target->commit->object.oid))
		do_diff_cache(get_commit_tree_oid(parent), &diff_opts);
	else
		diff_tree_oid(get_commit_tree_oid(parent),
			      get_commit_tree_oid(target->commit),
			      "", &diff_opts);

	if (!diff_opts.flags.find_copies_harder)
		diffcore_std(&diff_opts);

	do {
		struct blame_entry **unblamedtail = &unblamed;
		blame_list = setup_blame_list(unblamed, &num_ents);

		for (i = 0; i < diff_queued_diff.nr; i++) {
			struct diff_filepair *p = diff_queued_diff.queue[i];
			struct blame_origin *norigin;
			mmfile_t file_p;
			struct blame_entry potential[3];

			if (!DIFF_FILE_VALID(p->one))
				continue; /* does not exist in parent */
			if (S_ISGITLINK(p->one->mode))
				continue; /* ignore git links */
			if (porigin && !strcmp(p->one->path, porigin->path))
				/* find_move already dealt with this path */
				continue;

			norigin = get_origin(parent, p->one->path);
			oidcpy(&norigin->blob_oid, &p->one->oid);
			norigin->mode = p->one->mode;
			fill_origin_blob(&sb->revs->diffopt, norigin, &file_p,
					 &sb->num_read_blob, 0);
			if (!file_p.ptr)
				continue;

			for (j = 0; j < num_ents; j++) {
				find_copy_in_blob(sb, blame_list[j].ent,
						  norigin, potential, &file_p);
				copy_split_if_better(sb, blame_list[j].split,
						     potential);
				decref_split(potential);
			}
			blame_origin_decref(norigin);
		}

		for (j = 0; j < num_ents; j++) {
			struct blame_entry *split = blame_list[j].split;
			if (split[1].suspect &&
			    sb->copy_score < blame_entry_score(sb, &split[1])) {
				split_blame(blamed, &unblamedtail, split,
					    blame_list[j].ent);
			} else {
				blame_list[j].ent->next = leftover;
				leftover = blame_list[j].ent;
			}
			decref_split(split);
		}
		free(blame_list);
		*unblamedtail = NULL;
		toosmall = filter_small(sb, toosmall, &unblamed, sb->copy_score);
	} while (unblamed);
	target->suspects = reverse_blame(leftover, NULL);
	diff_flush(&diff_opts);
	clear_pathspec(&diff_opts.pathspec);
}

/*
 * The blobs of origin and porigin exactly match, so everything
 * origin is suspected for can be blamed on the parent.
 */
static void pass_whole_blame(struct blame_scoreboard *sb,
			     struct blame_origin *origin, struct blame_origin *porigin)
{
	struct blame_entry *e, *suspects;

	if (!porigin->file.ptr && origin->file.ptr) {
		/* Steal its file */
		porigin->file = origin->file;
		origin->file.ptr = NULL;
	}
	suspects = origin->suspects;
	origin->suspects = NULL;
	for (e = suspects; e; e = e->next) {
		blame_origin_incref(porigin);
		blame_origin_decref(e->suspect);
		e->suspect = porigin;
	}
	queue_blames(sb, porigin, suspects);
}

/*
 * We pass blame from the current commit to its parents.  We keep saying
 * "parent" (and "porigin"), but what we mean is to find scapegoat to
 * exonerate ourselves.
 */
static struct commit_list *first_scapegoat(struct rev_info *revs, struct commit *commit,
					int reverse)
{
	if (!reverse) {
		if (revs->first_parent_only &&
		    commit->parents &&
		    commit->parents->next) {
			free_commit_list(commit->parents->next);
			commit->parents->next = NULL;
		}
		return commit->parents;
	}
	return lookup_decoration(&revs->children, &commit->object);
}

static int num_scapegoats(struct rev_info *revs, struct commit *commit, int reverse)
{
	struct commit_list *l = first_scapegoat(revs, commit, reverse);
	return commit_list_count(l);
}

/* Distribute collected unsorted blames to the respected sorted lists
 * in the various origins.
 */
static void distribute_blame(struct blame_scoreboard *sb, struct blame_entry *blamed)
{
	blamed = llist_mergesort(blamed, get_next_blame, set_next_blame,
				 compare_blame_suspect);
	while (blamed)
	{
		struct blame_origin *porigin = blamed->suspect;
		struct blame_entry *suspects = NULL;
		do {
			struct blame_entry *next = blamed->next;
			blamed->next = suspects;
			suspects = blamed;
			blamed = next;
		} while (blamed && blamed->suspect == porigin);
		suspects = reverse_blame(suspects, NULL);
		queue_blames(sb, porigin, suspects);
	}
}

#define MAXSG 16

typedef struct blame_origin *(*blame_find_alg)(struct repository *,
					       struct commit *,
					       struct blame_origin *,
					       struct blame_bloom_data *);

static void pass_blame(struct blame_scoreboard *sb, struct blame_origin *origin, int opt)
{
	struct rev_info *revs = sb->revs;
	int i, pass, num_sg;
	struct commit *commit = origin->commit;
	struct commit_list *sg;
	struct blame_origin *sg_buf[MAXSG];
	struct blame_origin *porigin, **sg_origin = sg_buf;
	struct blame_entry *toosmall = NULL;
	struct blame_entry *blames, **blametail = &blames;

	num_sg = num_scapegoats(revs, commit, sb->reverse);
	if (!num_sg)
		goto finish;
	else if (num_sg < ARRAY_SIZE(sg_buf))
		memset(sg_buf, 0, sizeof(sg_buf));
	else
		sg_origin = xcalloc(num_sg, sizeof(*sg_origin));

	/*
	 * The first pass looks for unrenamed path to optimize for
	 * common cases, then we look for renames in the second pass.
	 */
	for (pass = 0; pass < 2 - sb->no_whole_file_rename; pass++) {
		blame_find_alg find = pass ? find_rename : find_origin;

		for (i = 0, sg = first_scapegoat(revs, commit, sb->reverse);
		     i < num_sg && sg;
		     sg = sg->next, i++) {
			struct commit *p = sg->item;
			int j, same;

			if (sg_origin[i])
				continue;
			if (parse_commit(p))
				continue;
			porigin = find(sb->repo, p, origin, sb->bloom_data);
			if (!porigin)
				continue;
			if (oideq(&porigin->blob_oid, &origin->blob_oid)) {
				pass_whole_blame(sb, origin, porigin);
				blame_origin_decref(porigin);
				goto finish;
			}
			for (j = same = 0; j < i; j++)
				if (sg_origin[j] &&
				    oideq(&sg_origin[j]->blob_oid, &porigin->blob_oid)) {
					same = 1;
					break;
				}
			if (!same)
				sg_origin[i] = porigin;
			else
				blame_origin_decref(porigin);
		}
	}

	sb->num_commits++;
	for (i = 0, sg = first_scapegoat(revs, commit, sb->reverse);
	     i < num_sg && sg;
	     sg = sg->next, i++) {
		struct blame_origin *porigin = sg_origin[i];
		if (!porigin)
			continue;
		if (!origin->previous) {
			blame_origin_incref(porigin);
			origin->previous = porigin;
		}
		pass_blame_to_parent(sb, origin, porigin, 0);
		if (!origin->suspects)
			goto finish;
	}

	/*
	 * Pass remaining suspects for ignored commits to their parents.
	 */
	if (oidset_contains(&sb->ignore_list, &commit->object.oid)) {
		for (i = 0, sg = first_scapegoat(revs, commit, sb->reverse);
		     i < num_sg && sg;
		     sg = sg->next, i++) {
			struct blame_origin *porigin = sg_origin[i];

			if (!porigin)
				continue;
			pass_blame_to_parent(sb, origin, porigin, 1);
			/*
			 * Preemptively drop porigin so we can refresh the
			 * fingerprints if we use the parent again, which can
			 * occur if you ignore back-to-back commits.
			 */
			drop_origin_blob(porigin);
			if (!origin->suspects)
				goto finish;
		}
	}

	/*
	 * Optionally find moves in parents' files.
	 */
	if (opt & PICKAXE_BLAME_MOVE) {
		filter_small(sb, &toosmall, &origin->suspects, sb->move_score);
		if (origin->suspects) {
			for (i = 0, sg = first_scapegoat(revs, commit, sb->reverse);
			     i < num_sg && sg;
			     sg = sg->next, i++) {
				struct blame_origin *porigin = sg_origin[i];
				if (!porigin)
					continue;
				find_move_in_parent(sb, &blametail, &toosmall, origin, porigin);
				if (!origin->suspects)
					break;
			}
		}
	}

	/*
	 * Optionally find copies from parents' files.
	 */
	if (opt & PICKAXE_BLAME_COPY) {
		if (sb->copy_score > sb->move_score)
			filter_small(sb, &toosmall, &origin->suspects, sb->copy_score);
		else if (sb->copy_score < sb->move_score) {
			origin->suspects = blame_merge(origin->suspects, toosmall);
			toosmall = NULL;
			filter_small(sb, &toosmall, &origin->suspects, sb->copy_score);
		}
		if (!origin->suspects)
			goto finish;

		for (i = 0, sg = first_scapegoat(revs, commit, sb->reverse);
		     i < num_sg && sg;
		     sg = sg->next, i++) {
			struct blame_origin *porigin = sg_origin[i];
			find_copy_in_parent(sb, &blametail, &toosmall,
					    origin, sg->item, porigin, opt);
			if (!origin->suspects)
				goto finish;
		}
	}

finish:
	*blametail = NULL;
	distribute_blame(sb, blames);
	/*
	 * prepend toosmall to origin->suspects
	 *
	 * There is no point in sorting: this ends up on a big
	 * unsorted list in the caller anyway.
	 */
	if (toosmall) {
		struct blame_entry **tail = &toosmall;
		while (*tail)
			tail = &(*tail)->next;
		*tail = origin->suspects;
		origin->suspects = toosmall;
	}
	for (i = 0; i < num_sg; i++) {
		if (sg_origin[i]) {
			if (!sg_origin[i]->suspects)
				drop_origin_blob(sg_origin[i]);
			blame_origin_decref(sg_origin[i]);
		}
	}
	drop_origin_blob(origin);
	if (sg_buf != sg_origin)
		free(sg_origin);
}

/*
 * The main loop -- while we have blobs with lines whose true origin
 * is still unknown, pick one blob, and allow its lines to pass blames
 * to its parents. */
void assign_blame(struct blame_scoreboard *sb, int opt)
{
	struct rev_info *revs = sb->revs;
	struct commit *commit = prio_queue_get(&sb->commits);

	while (commit) {
		struct blame_entry *ent;
		struct blame_origin *suspect = get_blame_suspects(commit);

		/* find one suspect to break down */
		while (suspect && !suspect->suspects)
			suspect = suspect->next;

		if (!suspect) {
			commit = prio_queue_get(&sb->commits);
			continue;
		}

		assert(commit == suspect->commit);

		/*
		 * We will use this suspect later in the loop,
		 * so hold onto it in the meantime.
		 */
		blame_origin_incref(suspect);
		parse_commit(commit);
		if (sb->reverse ||
		    (!(commit->object.flags & UNINTERESTING) &&
		     !(revs->max_age != -1 && commit->date < revs->max_age)))
			pass_blame(sb, suspect, opt);
		else {
			commit->object.flags |= UNINTERESTING;
			if (commit->object.parsed)
				mark_parents_uninteresting(commit);
		}
		/* treat root commit as boundary */
		if (!commit->parents && !sb->show_root)
			commit->object.flags |= UNINTERESTING;

		/* Take responsibility for the remaining entries */
		ent = suspect->suspects;
		if (ent) {
			suspect->guilty = 1;
			for (;;) {
				struct blame_entry *next = ent->next;
				if (sb->found_guilty_entry)
					sb->found_guilty_entry(ent, sb->found_guilty_entry_data);
				if (next) {
					ent = next;
					continue;
				}
				ent->next = sb->ent;
				sb->ent = suspect->suspects;
				suspect->suspects = NULL;
				break;
			}
		}
		blame_origin_decref(suspect);

		if (sb->debug) /* sanity */
			sanity_check_refcnt(sb);
	}
}

/*
 * To allow quick access to the contents of nth line in the
 * final image, prepare an index in the scoreboard.
 */
static int prepare_lines(struct blame_scoreboard *sb)
{
	sb->num_lines = find_line_starts(&sb->lineno, sb->final_buf,
					 sb->final_buf_size);
	return sb->num_lines;
}

static struct commit *find_single_final(struct rev_info *revs,
					const char **name_p)
{
	int i;
	struct commit *found = NULL;
	const char *name = NULL;

	for (i = 0; i < revs->pending.nr; i++) {
		struct object *obj = revs->pending.objects[i].item;
		if (obj->flags & UNINTERESTING)
			continue;
		obj = deref_tag(revs->repo, obj, NULL, 0);
		if (obj->type != OBJ_COMMIT)
			die("Non commit %s?", revs->pending.objects[i].name);
		if (found)
			die("More than one commit to dig from %s and %s?",
			    revs->pending.objects[i].name, name);
		found = (struct commit *)obj;
		name = revs->pending.objects[i].name;
	}
	if (name_p)
		*name_p = xstrdup_or_null(name);
	return found;
}

static struct commit *dwim_reverse_initial(struct rev_info *revs,
					   const char **name_p)
{
	/*
	 * DWIM "git blame --reverse ONE -- PATH" as
	 * "git blame --reverse ONE..HEAD -- PATH" but only do so
	 * when it makes sense.
	 */
	struct object *obj;
	struct commit *head_commit;
	struct object_id head_oid;

	if (revs->pending.nr != 1)
		return NULL;

	/* Is that sole rev a committish? */
	obj = revs->pending.objects[0].item;
	obj = deref_tag(revs->repo, obj, NULL, 0);
	if (obj->type != OBJ_COMMIT)
		return NULL;

	/* Do we have HEAD? */
	if (!resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, &head_oid, NULL))
		return NULL;
	head_commit = lookup_commit_reference_gently(revs->repo,
						     &head_oid, 1);
	if (!head_commit)
		return NULL;

	/* Turn "ONE" into "ONE..HEAD" then */
	obj->flags |= UNINTERESTING;
	add_pending_object(revs, &head_commit->object, "HEAD");

	if (name_p)
		*name_p = revs->pending.objects[0].name;
	return (struct commit *)obj;
}

static struct commit *find_single_initial(struct rev_info *revs,
					  const char **name_p)
{
	int i;
	struct commit *found = NULL;
	const char *name = NULL;

	/*
	 * There must be one and only one negative commit, and it must be
	 * the boundary.
	 */
	for (i = 0; i < revs->pending.nr; i++) {
		struct object *obj = revs->pending.objects[i].item;
		if (!(obj->flags & UNINTERESTING))
			continue;
		obj = deref_tag(revs->repo, obj, NULL, 0);
		if (obj->type != OBJ_COMMIT)
			die("Non commit %s?", revs->pending.objects[i].name);
		if (found)
			die("More than one commit to dig up from, %s and %s?",
			    revs->pending.objects[i].name, name);
		found = (struct commit *) obj;
		name = revs->pending.objects[i].name;
	}

	if (!name)
		found = dwim_reverse_initial(revs, &name);
	if (!name)
		die("No commit to dig up from?");

	if (name_p)
		*name_p = xstrdup(name);
	return found;
}

void init_scoreboard(struct blame_scoreboard *sb)
{
	memset(sb, 0, sizeof(struct blame_scoreboard));
	sb->move_score = BLAME_DEFAULT_MOVE_SCORE;
	sb->copy_score = BLAME_DEFAULT_COPY_SCORE;
}

void setup_scoreboard(struct blame_scoreboard *sb,
		      const char *path,
		      struct blame_origin **orig)
{
	const char *final_commit_name = NULL;
	struct blame_origin *o;
	struct commit *final_commit = NULL;
	enum object_type type;

	init_blame_suspects(&blame_suspects);

	if (sb->reverse && sb->contents_from)
		die(_("--contents and --reverse do not blend well."));

	if (!sb->repo)
		BUG("repo is NULL");

	if (!sb->reverse) {
		sb->final = find_single_final(sb->revs, &final_commit_name);
		sb->commits.compare = compare_commits_by_commit_date;
	} else {
		sb->final = find_single_initial(sb->revs, &final_commit_name);
		sb->commits.compare = compare_commits_by_reverse_commit_date;
	}

	if (sb->final && sb->contents_from)
		die(_("cannot use --contents with final commit object name"));

	if (sb->reverse && sb->revs->first_parent_only)
		sb->revs->children.name = NULL;

	if (!sb->final) {
		/*
		 * "--not A B -- path" without anything positive;
		 * do not default to HEAD, but use the working tree
		 * or "--contents".
		 */
		setup_work_tree();
		sb->final = fake_working_tree_commit(sb->repo,
						     &sb->revs->diffopt,
						     path, sb->contents_from);
		add_pending_object(sb->revs, &(sb->final->object), ":");
	}

	if (sb->reverse && sb->revs->first_parent_only) {
		final_commit = find_single_final(sb->revs, NULL);
		if (!final_commit)
			die(_("--reverse and --first-parent together require specified latest commit"));
	}

	/*
	 * If we have bottom, this will mark the ancestors of the
	 * bottom commits we would reach while traversing as
	 * uninteresting.
	 */
	if (prepare_revision_walk(sb->revs))
		die(_("revision walk setup failed"));

	if (sb->reverse && sb->revs->first_parent_only) {
		struct commit *c = final_commit;

		sb->revs->children.name = "children";
		while (c->parents &&
		       !oideq(&c->object.oid, &sb->final->object.oid)) {
			struct commit_list *l = xcalloc(1, sizeof(*l));

			l->item = c;
			if (add_decoration(&sb->revs->children,
					   &c->parents->item->object, l))
				BUG("not unique item in first-parent chain");
			c = c->parents->item;
		}

		if (!oideq(&c->object.oid, &sb->final->object.oid))
			die(_("--reverse --first-parent together require range along first-parent chain"));
	}

	if (is_null_oid(&sb->final->object.oid)) {
		o = get_blame_suspects(sb->final);
		sb->final_buf = xmemdupz(o->file.ptr, o->file.size);
		sb->final_buf_size = o->file.size;
	}
	else {
		o = get_origin(sb->final, path);
		if (fill_blob_sha1_and_mode(sb->repo, o))
			die(_("no such path %s in %s"), path, final_commit_name);

		if (sb->revs->diffopt.flags.allow_textconv &&
		    textconv_object(sb->repo, path, o->mode, &o->blob_oid, 1, (char **) &sb->final_buf,
				    &sb->final_buf_size))
			;
		else
			sb->final_buf = read_object_file(&o->blob_oid, &type,
							 &sb->final_buf_size);

		if (!sb->final_buf)
			die(_("cannot read blob %s for path %s"),
			    oid_to_hex(&o->blob_oid),
			    path);
	}
	sb->num_read_blob++;
	prepare_lines(sb);

	if (orig)
		*orig = o;

	free((char *)final_commit_name);
}



struct blame_entry *blame_entry_prepend(struct blame_entry *head,
					long start, long end,
					struct blame_origin *o)
{
	struct blame_entry *new_head = xcalloc(1, sizeof(struct blame_entry));
	new_head->lno = start;
	new_head->num_lines = end - start;
	new_head->suspect = o;
	new_head->s_lno = start;
	new_head->next = head;
	blame_origin_incref(o);
	return new_head;
}

void setup_blame_bloom_data(struct blame_scoreboard *sb,
			    const char *path)
{
	struct blame_bloom_data *bd;
	struct bloom_filter_settings *bs;

	if (!sb->repo->objects->commit_graph)
		return;

	bs = get_bloom_filter_settings(sb->repo);
	if (!bs)
		return;

	bd = xmalloc(sizeof(struct blame_bloom_data));

	bd->settings = bs;

	bd->alloc = 4;
	bd->nr = 0;
	ALLOC_ARRAY(bd->keys, bd->alloc);

	add_bloom_key(bd, path);

	sb->bloom_data = bd;
}

void cleanup_scoreboard(struct blame_scoreboard *sb)
{
	if (sb->bloom_data) {
		int i;
		for (i = 0; i < sb->bloom_data->nr; i++) {
			free(sb->bloom_data->keys[i]->hashes);
			free(sb->bloom_data->keys[i]);
		}
		free(sb->bloom_data->keys);
		FREE_AND_NULL(sb->bloom_data);

		trace2_data_intmax("blame", sb->repo,
				   "bloom/queries", bloom_count_queries);
		trace2_data_intmax("blame", sb->repo,
				   "bloom/response-no", bloom_count_no);
	}
}
