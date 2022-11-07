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
};

struct write_packed_refs_v2_context *create_v2_context(struct packed_ref_store *refs,
						       struct string_list *updates,
						       struct strbuf *err)
{
	struct write_packed_refs_v2_context *ctx;
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

int write_packed_refs_v2(struct write_packed_refs_v2_context *ctx)
{
	unsigned char file_hash[GIT_MAX_RAWSZ];

	add_chunk(ctx->cf, CHREFS_CHUNKID_REFS, 0, write_refs_chunk_refs);
	add_chunk(ctx->cf, CHREFS_CHUNKID_OFFSETS, 0, write_refs_chunk_offsets);

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
