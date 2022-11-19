#include "../cache.h"
#include "../config.h"
#include "../refs.h"
#include "refs-internal.h"
#include "packed-backend.h"
#include "../iterator.h"
#include "../lockfile.h"
#include "../chdir-notify.h"
#include "../chunk-format.h"
#include "../csum-file.h"

#define OFFSET_IS_PEELED (((uint64_t)1) << 63)

#define PACKED_REFS_SIGNATURE          0x50524546 /* "PREF" */
#define CHREFS_CHUNKID_OFFSETS         0x524F4646 /* "ROFF" */
#define CHREFS_CHUNKID_REFS            0x52454653 /* "REFS" */
#define CHREFS_CHUNKID_PREFIX_DATA     0x50465844 /* "PFXD" */
#define CHREFS_CHUNKID_PREFIX_OFFSETS  0x5046584F /* "PFXO" */

static const char *get_nth_prefix(struct snapshot *snapshot,
				  size_t n, size_t *len)
{
	uint64_t offset, next_offset;

	if (n >= snapshot->prefixes_nr)
		BUG("asking for prefix %"PRIu64" outside of bounds (%"PRIu64")",
		    (uint64_t)n, (uint64_t)snapshot->prefixes_nr);

	if (n)
		offset = get_be32(snapshot->prefix_offsets_chunk +
				  2 * sizeof(uint32_t) * (n - 1));
	else
		offset = 0;

	if (len) {
		next_offset = get_be32(snapshot->prefix_offsets_chunk +
				       2 * sizeof(uint32_t) * n);

		/* Prefix includes null terminator. */
		*len = next_offset - offset - 1;
	}

	return snapshot->prefix_chunk + offset;
}

/*
 * Find the place in `snapshot->buf` where the start of the record for
 * `refname` starts. If `mustexist` is true and the reference doesn't
 * exist, then return NULL. If `mustexist` is false and the reference
 * doesn't exist, then return the point where that reference would be
 * inserted, or `snapshot->eof` (which might be NULL) if it would be
 * inserted at the end of the file. In the latter mode, `refname`
 * doesn't have to be a proper reference name; for example, one could
 * search for "refs/replace/" to find the start of any replace
 * references.
 *
 * The record is sought using a binary search, so `snapshot->buf` must
 * be sorted.
 */
static const char *find_prefix_location(struct snapshot *snapshot,
					const char *refname, size_t *pos)
{
	size_t lo = 0, hi = snapshot->prefixes_nr;

	while (lo != hi) {
		const char *rec;
		int cmp;
		size_t len;
		size_t mid = lo + (hi - lo) / 2;

		rec = get_nth_prefix(snapshot, mid, &len);
		cmp = strncmp(rec, refname, len);
		if (cmp < 0) {
			lo = mid + 1;
		} else if (cmp > 0) {
			hi = mid;
		} else {
			/* we have a prefix match! */
			*pos = mid;
			return rec;
		}
	}

	*pos = lo;
	if (lo < snapshot->prefixes_nr)
		return get_nth_prefix(snapshot, lo, NULL);
	else
		return NULL;
}

int detect_packed_format_v2_header(struct packed_ref_store *refs,
				   struct snapshot *snapshot)
{
	/*
	 * packed-refs v1 might not have a header, so check instead
	 * that the v2 signature is not present.
	 */
	return get_be32(snapshot->buf) == PACKED_REFS_SIGNATURE;
}

static const char *get_nth_ref(struct snapshot *snapshot,
			       size_t n)
{
	uint64_t offset;

	if (n >= snapshot->nr)
		BUG("asking for position %"PRIu64" outside of bounds (%"PRIu64")",
		    (uint64_t)n, (uint64_t)snapshot->nr);

	if (n)
		offset = get_be64(snapshot->offset_chunk + (n-1) * sizeof(uint64_t))
				  & ~OFFSET_IS_PEELED;
	else
		offset = 0;

	return snapshot->refs_chunk + offset;
}

/*
 * Find the place in `snapshot->buf` where the start of the record for
 * `refname` starts. If `mustexist` is true and the reference doesn't
 * exist, then return NULL. If `mustexist` is false and the reference
 * doesn't exist, then return the point where that reference would be
 * inserted, or `snapshot->eof` (which might be NULL) if it would be
 * inserted at the end of the file. In the latter mode, `refname`
 * doesn't have to be a proper reference name; for example, one could
 * search for "refs/replace/" to find the start of any replace
 * references.
 *
 * The record is sought using a binary search, so `snapshot->buf` must
 * be sorted.
 */
const char *find_reference_location_v2(struct snapshot *snapshot,
				       const char *refname, int mustexist,
				       size_t *pos)
{
	size_t lo = 0, hi = snapshot->nr;

	if (snapshot->prefix_chunk) {
		size_t prefix_row;
		const char *prefix;
		int found = 1;

		prefix = find_prefix_location(snapshot, refname, &prefix_row);

		if (!prefix || !starts_with(refname, prefix)) {
			if (mustexist)
				return NULL;
			found = 0;
		}

		/* The second 4-byte column of the prefix offsets */
		if (prefix_row) {
			/* if prefix_row == 0, then lo = 0, which is already true. */
			lo = get_be32(snapshot->prefix_offsets_chunk +
				2 * sizeof(uint32_t) * (prefix_row - 1) + sizeof(uint32_t));
		}

		if (!found) {
			const char *ret;
			/* Terminate early with this lo position as the insertion point. */
			if (pos)
				*pos = lo;

			if (lo >= snapshot->nr)
				return NULL;

			ret = get_nth_ref(snapshot, lo);
			return ret;
		}

		hi = get_be32(snapshot->prefix_offsets_chunk +
			      2 * sizeof(uint32_t) * prefix_row + sizeof(uint32_t));

		if (prefix)
			refname += strlen(prefix);
	}

	while (lo != hi) {
		const char *rec;
		int cmp;
		size_t mid = lo + (hi - lo) / 2;

		rec = get_nth_ref(snapshot, mid);
		cmp = strcmp(rec, refname);
		if (cmp < 0) {
			lo = mid + 1;
		} else if (cmp > 0) {
			hi = mid;
		} else {
			if (pos)
				*pos = mid;
			return rec;
		}
	}

	if (mustexist) {
		return NULL;
	} else {
		const char *ret;
		/*
		 * We are likely doing a prefix match, so use the current
		 * 'lo' position as the indicator.
		 */
		if (pos)
			*pos = lo;
		if (lo >= snapshot->nr)
			return NULL;

		ret = get_nth_ref(snapshot, lo);
		return ret;
	}
}

int packed_read_raw_ref_v2(struct packed_ref_store *refs, struct snapshot *snapshot,
			   const char *refname, struct object_id *oid,
			   unsigned int *type, int *failure_errno)
{
	const char *rec;

	*type = 0;

	rec = find_reference_location_v2(snapshot, refname, 1, NULL);

	if (!rec) {
		/* refname is not a packed reference. */
		*failure_errno = ENOENT;
		return -1;
	}

	hashcpy(oid->hash, (const unsigned char *)rec + strlen(rec) + 1);
	oid->algo = hash_algo_by_ptr(the_hash_algo);

	*type = REF_ISPACKED;
	return 0;
}

static int packed_refs_read_offsets(const unsigned char *chunk_start,
				     size_t chunk_size, void *data)
{
	struct snapshot *snapshot = data;

	snapshot->offset_chunk = chunk_start;
	snapshot->nr = chunk_size / sizeof(uint64_t);
	return 0;
}

static int packed_refs_read_prefix_offsets(const unsigned char *chunk_start,
					    size_t chunk_size, void *data)
{
	struct snapshot *snapshot = data;

	snapshot->prefix_offsets_chunk = chunk_start;
	snapshot->prefixes_nr = chunk_size / sizeof(uint64_t);
	return 0;
}

void fill_snapshot_v2(struct snapshot *snapshot)
{
	uint32_t file_signature, file_version, hash_version;
	struct chunkfile *cf;

	file_signature = get_be32(snapshot->buf);
	if (file_signature != PACKED_REFS_SIGNATURE)
		die(_("%s file signature %X does not match signature %X"),
		    "packed-ref", file_signature, PACKED_REFS_SIGNATURE);

	file_version = get_be32(snapshot->buf + sizeof(uint32_t));
	if (file_version != 2)
		die(_("format version %u does not match expected file version %u"),
		    file_version, 2);

	hash_version = get_be32(snapshot->buf + 2 * sizeof(uint32_t));
	if (hash_version != the_hash_algo->format_id)
		die(_("hash version %X does not match expected hash version %X"),
		    hash_version, the_hash_algo->format_id);

	cf = init_chunkfile(NULL);

	if (read_trailing_table_of_contents(cf, (const unsigned char *)snapshot->buf, snapshot->buflen)) {
		release_snapshot(snapshot);
		snapshot = NULL;
		goto cleanup;
	}

	read_chunk(cf, CHREFS_CHUNKID_OFFSETS, packed_refs_read_offsets, snapshot);
	pair_chunk(cf, CHREFS_CHUNKID_REFS, (const unsigned char**)&snapshot->refs_chunk);

	read_chunk(cf, CHREFS_CHUNKID_PREFIX_OFFSETS, packed_refs_read_prefix_offsets, snapshot);
	pair_chunk(cf, CHREFS_CHUNKID_PREFIX_DATA, (const unsigned char**)&snapshot->prefix_chunk);

	/* TODO: add error checks for invalid chunk combinations. */

cleanup:
	free_chunkfile(cf);
}

/*
 * Move the iterator to the next record in the snapshot, without
 * respect for whether the record is actually required by the current
 * iteration. Adjust the fields in `iter` and return `ITER_OK` or
 * `ITER_DONE`. This function does not free the iterator in the case
 * of `ITER_DONE`.
 */
int next_record_v2(struct packed_ref_iterator *iter)
{
	uint64_t offset;
	const char *pos = iter->pos;
	strbuf_reset(&iter->refname_buf);

	if (iter->row == iter->snapshot->nr)
		return ITER_DONE;

	iter->base.flags = REF_ISPACKED;

	if (iter->cur_prefix)
		strbuf_addstr(&iter->refname_buf, iter->cur_prefix);
	strbuf_addstr(&iter->refname_buf, pos);
	iter->base.refname = iter->refname_buf.buf;
	pos += strlen(pos) + 1;

	hashcpy(iter->oid.hash, (const unsigned char *)pos);
	iter->oid.algo = hash_algo_by_ptr(the_hash_algo);
	pos += the_hash_algo->rawsz;

	if (check_refname_format(iter->base.refname, REFNAME_ALLOW_ONELEVEL)) {
		if (!refname_is_safe(iter->base.refname))
			die("packed refname is dangerous: %s",
			    iter->base.refname);
		oidclr(&iter->oid);
		iter->base.flags |= REF_BAD_NAME | REF_ISBROKEN;
	}

	/* We always know the peeled value! */
	iter->base.flags |= REF_KNOWS_PEELED;

	offset = get_be64(iter->snapshot->offset_chunk + sizeof(uint64_t) * iter->row);
	if (offset & OFFSET_IS_PEELED) {
		hashcpy(iter->peeled.hash, (const unsigned char *)pos);
		iter->peeled.algo = hash_algo_by_ptr(the_hash_algo);
	} else {
		oidclr(&iter->peeled);
	}

	/* TODO: somehow all tags are getting OFFSET_IS_PEELED even though
	 * some are not annotated tags.
	 */
	iter->pos = iter->snapshot->refs_chunk + (offset & (~OFFSET_IS_PEELED));

	iter->row++;

	if (iter->row == iter->prefix_row_end && iter->snapshot->prefix_chunk) {
		size_t prefix_pos = get_be32(iter->snapshot->prefix_offsets_chunk +
					     2 * sizeof(uint32_t) * iter->prefix_i);
		iter->cur_prefix = iter->snapshot->prefix_chunk + prefix_pos;
		iter->prefix_i++;
		iter->prefix_row_end = get_be32(iter->snapshot->prefix_offsets_chunk +
						2 * sizeof(uint32_t) * iter->prefix_i + sizeof(uint32_t));
	}

	return ITER_OK;
}

void init_iterator_prefix_info(const char *prefix,
			       struct packed_ref_iterator *iter)
{
	struct snapshot *snapshot = iter->snapshot;

	if (snapshot->version != 2 || !snapshot->prefix_chunk) {
		iter->prefix_row_end = snapshot->nr;
		return;
	}

	if (prefix)
		iter->cur_prefix = find_prefix_location(snapshot, prefix, &iter->prefix_i);
	else {
		iter->cur_prefix = snapshot->prefix_chunk;
		iter->prefix_i = 0;
	}

	iter->prefix_row_end = get_be32(snapshot->prefix_offsets_chunk +
					2 * sizeof(uint32_t) * iter->prefix_i +
					sizeof(uint32_t));
}

struct write_packed_refs_v2_context {
	struct packed_ref_store *refs;
	struct string_list *updates;
	struct strbuf *err;

	struct hashfile *f;
	struct chunkfile *cf;

	/*
	 * As we stream the ref names to the refs chunk, store these
	 * values in-memory. These arrays are populated one for every ref.
	 */
	uint64_t *offsets;
	size_t nr;
	size_t offsets_alloc;

	int write_prefixes;
	const char *cur_prefix;
	size_t cur_prefix_len;

	char **prefixes;
	uint32_t *prefix_offsets;
	uint32_t *prefix_rows;
	size_t prefix_nr;
	size_t prefixes_alloc;
	size_t prefix_offsets_alloc;
	size_t prefix_rows_alloc;
};

struct write_packed_refs_v2_context *create_v2_context(struct packed_ref_store *refs,
						       struct string_list *updates,
						       struct strbuf *err)
{
	struct write_packed_refs_v2_context *ctx;
	int do_skip_hash;
	CALLOC_ARRAY(ctx, 1);

	ctx->refs = refs;
	ctx->updates = updates;
	ctx->err = err;

	if (!fdopen_tempfile(refs->tempfile, "w")) {
		strbuf_addf(err, "unable to fdopen packed-refs tempfile: %s",
			    strerror(errno));
		return ctx;
	}

	ctx->f = hashfd(refs->tempfile->fd, refs->tempfile->filename.buf);

	/* Default to true, so skip_hash if not set. */
	if (git_config_get_maybe_bool("refs.hashpackedrefs", &do_skip_hash) ||
	    do_skip_hash)
		ctx->f->skip_hash = 1;

	ctx->cf = init_chunkfile(ctx->f);

	return ctx;
}

static int write_packed_entry_v2(const char *refname,
				 const struct object_id *oid,
				 const struct object_id *peeled,
				 void *write_data)
{
	struct write_packed_refs_v2_context *ctx = write_data;
	size_t reflen = strlen(refname) + 1;
	size_t i = ctx->nr;

	ALLOC_GROW(ctx->offsets, i + 1, ctx->offsets_alloc);

	if (ctx->write_prefixes) {
		if (ctx->cur_prefix && starts_with(refname, ctx->cur_prefix)) {
			/* skip ahead! */
			refname += ctx->cur_prefix_len;
			reflen -= ctx->cur_prefix_len;
		} else {
			size_t len;
			const char *slash, *slashslash = NULL;
			if (ctx->prefix_nr) {
				/* close out the old prefix. */
				ctx->prefix_rows[ctx->prefix_nr - 1] = ctx->nr;
			}

			/* Find the new prefix. */
			slash = strchr(refname, '/');
			if (slash)
				slashslash = strchr(slash + 1, '/');
			/* If there are two slashes, use that. */
			slash = slashslash ? slashslash : slash;
			/*
			 * If there is at least one slash, use that,
			 * and include the slash in the string.
			 * Otherwise, use the end of the ref.
			 */
			slash = slash ? slash + 1 : refname + strlen(refname);

			len = slash - refname;
			ALLOC_GROW(ctx->prefixes, ctx->prefix_nr + 1, ctx->prefixes_alloc);
			ALLOC_GROW(ctx->prefix_offsets, ctx->prefix_nr + 1, ctx->prefix_offsets_alloc);
			ALLOC_GROW(ctx->prefix_rows, ctx->prefix_nr + 1, ctx->prefix_rows_alloc);

			if (ctx->prefix_nr)
				ctx->prefix_offsets[ctx->prefix_nr] = ctx->prefix_offsets[ctx->prefix_nr - 1] + len + 1;
			else
				ctx->prefix_offsets[ctx->prefix_nr] = len + 1;

			ctx->prefixes[ctx->prefix_nr] = xstrndup(refname, len);
			ctx->cur_prefix = ctx->prefixes[ctx->prefix_nr];
			ctx->prefix_nr++;

			refname += len;
			reflen -= len;
			ctx->cur_prefix_len = len;
		}

		/* Update the last row continually. */
		ctx->prefix_rows[ctx->prefix_nr - 1] = i + 1;
	}


	/* Write entire ref, including null terminator. */
	hashwrite(ctx->f, refname, reflen);
	hashwrite(ctx->f, oid->hash, the_hash_algo->rawsz);
	if (peeled)
		hashwrite(ctx->f, peeled->hash, the_hash_algo->rawsz);

	if (i)
		ctx->offsets[i] = (ctx->offsets[i - 1] & (~OFFSET_IS_PEELED));
	else
		ctx->offsets[i] = 0;
	ctx->offsets[i] += reflen + the_hash_algo->rawsz;

	if (peeled) {
		ctx->offsets[i] += the_hash_algo->rawsz;
		ctx->offsets[i] |= OFFSET_IS_PEELED;
	}

	ctx->nr++;
	return 0;
}

static int write_refs_chunk_refs(struct hashfile *f,
				 void *data)
{
	struct write_packed_refs_v2_context *ctx = data;
	int ok;

	trace2_region_enter("refs", "refs-chunk", the_repository);
	ok = merge_iterator_and_updates(ctx->refs, ctx->updates, ctx->err,
					write_packed_entry_v2, ctx);
	trace2_region_leave("refs", "refs-chunk", the_repository);

	return ok != ITER_DONE;
}

static int write_refs_chunk_offsets(struct hashfile *f,
				    void *data)
{
	struct write_packed_refs_v2_context *ctx = data;
	size_t i;

	trace2_region_enter("refs", "offsets", the_repository);
	for (i = 0; i < ctx->nr; i++)
		hashwrite_be64(f, ctx->offsets[i]);

	trace2_region_leave("refs", "offsets", the_repository);
	return 0;
}

static int write_refs_chunk_prefix_data(struct hashfile *f,
					void *data)
{
	struct write_packed_refs_v2_context *ctx = data;
	size_t i;

	trace2_region_enter("refs", "prefix-data", the_repository);
	for (i = 0; i < ctx->prefix_nr; i++) {
		size_t len = strlen(ctx->prefixes[i]) + 1;
		hashwrite(f, ctx->prefixes[i], len);

		/* TODO: assert the prefix lengths match the stored offsets? */
	}

	trace2_region_leave("refs", "prefix-data", the_repository);
	return 0;
}

static int write_refs_chunk_prefix_offsets(struct hashfile *f,
				    void *data)
{
	struct write_packed_refs_v2_context *ctx = data;
	size_t i;

	trace2_region_enter("refs", "prefix-offsets", the_repository);
	for (i = 0; i < ctx->prefix_nr; i++) {
		hashwrite_be32(f, ctx->prefix_offsets[i]);
		hashwrite_be32(f, ctx->prefix_rows[i]);
	}

	trace2_region_leave("refs", "prefix-offsets", the_repository);
	return 0;
}

int write_packed_refs_v2(struct write_packed_refs_v2_context *ctx)
{
	unsigned char file_hash[GIT_MAX_RAWSZ];

	ctx->write_prefixes = git_env_bool("GIT_TEST_WRITE_PACKED_REFS_PREFIXES", 1);

	add_chunk(ctx->cf, CHREFS_CHUNKID_REFS, 0, write_refs_chunk_refs);
	add_chunk(ctx->cf, CHREFS_CHUNKID_OFFSETS, 0, write_refs_chunk_offsets);

	if (ctx->write_prefixes) {
		add_chunk(ctx->cf, CHREFS_CHUNKID_PREFIX_DATA, 0, write_refs_chunk_prefix_data);
		add_chunk(ctx->cf, CHREFS_CHUNKID_PREFIX_OFFSETS, 0, write_refs_chunk_prefix_offsets);
	}

	hashwrite_be32(ctx->f, PACKED_REFS_SIGNATURE);
	hashwrite_be32(ctx->f, 2);
	hashwrite_be32(ctx->f, the_hash_algo->format_id);

	if (write_chunkfile(ctx->cf, CHUNKFILE_TRAILING_TOC, ctx))
		goto failure;

	finalize_hashfile(ctx->f, file_hash, FSYNC_COMPONENT_REFERENCE,
			  CSUM_HASH_IN_STREAM | CSUM_FSYNC);

	return 0;

failure:
	return -1;
}

void free_v2_context(struct write_packed_refs_v2_context *ctx)
{
	if (ctx->cf)
		free_chunkfile(ctx->cf);
	free(ctx);
}
