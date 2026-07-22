#include "git-compat-util.h"
#include "hash.h"
#include "hex.h"

static const struct object_id empty_tree_oid = {
	.hash = {
		0x4b, 0x82, 0x5d, 0xc6, 0x42, 0xcb, 0x6e, 0xb9, 0xa0, 0x60,
		0xe5, 0x4b, 0xf8, 0xd6, 0x92, 0x88, 0xfb, 0xee, 0x49, 0x04
	},
	.algo = GIT_HASH_SHA1,
};
static const struct object_id empty_blob_oid = {
	.hash = {
		0xe6, 0x9d, 0xe2, 0x9b, 0xb2, 0xd1, 0xd6, 0x43, 0x4b, 0x8b,
		0x29, 0xae, 0x77, 0x5a, 0xd8, 0xc2, 0xe4, 0x8c, 0x53, 0x91
	},
	.algo = GIT_HASH_SHA1,
};
static const struct object_id null_oid_sha1 = {
	.hash = {0},
	.algo = GIT_HASH_SHA1,
};
static const struct object_id empty_tree_oid_sha256 = {
	.hash = {
		0x6e, 0xf1, 0x9b, 0x41, 0x22, 0x5c, 0x53, 0x69, 0xf1, 0xc1,
		0x04, 0xd4, 0x5d, 0x8d, 0x85, 0xef, 0xa9, 0xb0, 0x57, 0xb5,
		0x3b, 0x14, 0xb4, 0xb9, 0xb9, 0x39, 0xdd, 0x74, 0xde, 0xcc,
		0x53, 0x21
	},
	.algo = GIT_HASH_SHA256,
};
static const struct object_id empty_blob_oid_sha256 = {
	.hash = {
		0x47, 0x3a, 0x0f, 0x4c, 0x3b, 0xe8, 0xa9, 0x36, 0x81, 0xa2,
		0x67, 0xe3, 0xb1, 0xe9, 0xa7, 0xdc, 0xda, 0x11, 0x85, 0x43,
		0x6f, 0xe1, 0x41, 0xf7, 0x74, 0x91, 0x20, 0xa3, 0x03, 0x72,
		0x18, 0x13
	},
	.algo = GIT_HASH_SHA256,
};
static const struct object_id null_oid_sha256 = {
	.hash = {0},
	.algo = GIT_HASH_SHA256,
};

static void git_hash_sha1_init(struct git_hash_ctx *ctx)
{
	ctx->algop = &hash_algos[GIT_HASH_SHA1];
	git_SHA1_Init(&ctx->state.sha1);
}

static void git_hash_sha1_clone(struct git_hash_ctx *dst, const struct git_hash_ctx *src)
{
	dst->algop = src->algop;
	git_SHA1_Clone(&dst->state.sha1, &src->state.sha1);
}

static void git_hash_sha1_update(struct git_hash_ctx *ctx, const void *data, size_t len)
{
	git_SHA1_Update(&ctx->state.sha1, data, len);
}

static void git_hash_sha1_final(unsigned char *hash, struct git_hash_ctx *ctx)
{
	git_SHA1_Final(hash, &ctx->state.sha1);
}

static void git_hash_sha1_final_oid(struct object_id *oid, struct git_hash_ctx *ctx)
{
	git_SHA1_Final(oid->hash, &ctx->state.sha1);
	memset(oid->hash + GIT_SHA1_RAWSZ, 0, GIT_MAX_RAWSZ - GIT_SHA1_RAWSZ);
	oid->algo = GIT_HASH_SHA1;
}

static void git_hash_sha1_init_unsafe(struct git_hash_ctx *ctx)
{
	ctx->algop = unsafe_hash_algo(&hash_algos[GIT_HASH_SHA1]);
	git_SHA1_Init_unsafe(&ctx->state.sha1_unsafe);
}

static void git_hash_sha1_clone_unsafe(struct git_hash_ctx *dst, const struct git_hash_ctx *src)
{
	dst->algop = src->algop;
	git_SHA1_Clone_unsafe(&dst->state.sha1_unsafe, &src->state.sha1_unsafe);
}

static void git_hash_sha1_update_unsafe(struct git_hash_ctx *ctx, const void *data,
				      size_t len)
{
	git_SHA1_Update_unsafe(&ctx->state.sha1_unsafe, data, len);
}

static void git_hash_sha1_final_unsafe(unsigned char *hash, struct git_hash_ctx *ctx)
{
	git_SHA1_Final_unsafe(hash, &ctx->state.sha1_unsafe);
}

static void git_hash_sha1_final_oid_unsafe(struct object_id *oid, struct git_hash_ctx *ctx)
{
	git_SHA1_Final_unsafe(oid->hash, &ctx->state.sha1_unsafe);
	memset(oid->hash + GIT_SHA1_RAWSZ, 0, GIT_MAX_RAWSZ - GIT_SHA1_RAWSZ);
	oid->algo = GIT_HASH_SHA1;
}

static void git_hash_sha256_init(struct git_hash_ctx *ctx)
{
	ctx->algop = unsafe_hash_algo(&hash_algos[GIT_HASH_SHA256]);
	git_SHA256_Init(&ctx->state.sha256);
}

static void git_hash_sha256_clone(struct git_hash_ctx *dst, const struct git_hash_ctx *src)
{
	dst->algop = src->algop;
	git_SHA256_Clone(&dst->state.sha256, &src->state.sha256);
}

static void git_hash_sha256_update(struct git_hash_ctx *ctx, const void *data, size_t len)
{
	git_SHA256_Update(&ctx->state.sha256, data, len);
}

static void git_hash_sha256_final(unsigned char *hash, struct git_hash_ctx *ctx)
{
	git_SHA256_Final(hash, &ctx->state.sha256);
}

static void git_hash_sha256_final_oid(struct object_id *oid, struct git_hash_ctx *ctx)
{
	git_SHA256_Final(oid->hash, &ctx->state.sha256);
	/*
	 * This currently does nothing, so the compiler should optimize it out,
	 * but keep it in case we extend the hash size again.
	 */
	memset(oid->hash + GIT_SHA256_RAWSZ, 0, GIT_MAX_RAWSZ - GIT_SHA256_RAWSZ);
	oid->algo = GIT_HASH_SHA256;
}

static void git_hash_unknown_init(struct git_hash_ctx *ctx UNUSED)
{
	BUG("trying to init unknown hash");
}

static void git_hash_unknown_clone(struct git_hash_ctx *dst UNUSED,
				   const struct git_hash_ctx *src UNUSED)
{
	BUG("trying to clone unknown hash");
}

static void git_hash_unknown_update(struct git_hash_ctx *ctx UNUSED,
				    const void *data UNUSED,
				    size_t len UNUSED)
{
	BUG("trying to update unknown hash");
}

static void git_hash_unknown_final(unsigned char *hash UNUSED,
				   struct git_hash_ctx *ctx UNUSED)
{
	BUG("trying to finalize unknown hash");
}

static void git_hash_unknown_final_oid(struct object_id *oid UNUSED,
				       struct git_hash_ctx *ctx UNUSED)
{
	BUG("trying to finalize unknown hash");
}

static const struct git_hash_algo sha1_unsafe_algo = {
	.name = "sha1",
	.format_id = GIT_SHA1_FORMAT_ID,
	.rawsz = GIT_SHA1_RAWSZ,
	.hexsz = GIT_SHA1_HEXSZ,
	.blksz = GIT_SHA1_BLKSZ,
	.init_fn = git_hash_sha1_init_unsafe,
	.clone_fn = git_hash_sha1_clone_unsafe,
	.update_fn = git_hash_sha1_update_unsafe,
	.final_fn = git_hash_sha1_final_unsafe,
	.final_oid_fn = git_hash_sha1_final_oid_unsafe,
	.empty_tree = &empty_tree_oid,
	.empty_blob = &empty_blob_oid,
	.null_oid = &null_oid_sha1,
};

const struct git_hash_algo hash_algos[GIT_HASH_NALGOS] = {
	{
		.name = NULL,
		.format_id = 0x00000000,
		.rawsz = 0,
		.hexsz = 0,
		.blksz = 0,
		.init_fn = git_hash_unknown_init,
		.clone_fn = git_hash_unknown_clone,
		.update_fn = git_hash_unknown_update,
		.final_fn = git_hash_unknown_final,
		.final_oid_fn = git_hash_unknown_final_oid,
		.empty_tree = NULL,
		.empty_blob = NULL,
		.null_oid = NULL,
	},
	{
		.name = "sha1",
		.format_id = GIT_SHA1_FORMAT_ID,
		.rawsz = GIT_SHA1_RAWSZ,
		.hexsz = GIT_SHA1_HEXSZ,
		.blksz = GIT_SHA1_BLKSZ,
		.init_fn = git_hash_sha1_init,
		.clone_fn = git_hash_sha1_clone,
		.update_fn = git_hash_sha1_update,
		.final_fn = git_hash_sha1_final,
		.final_oid_fn = git_hash_sha1_final_oid,
		.unsafe = &sha1_unsafe_algo,
		.empty_tree = &empty_tree_oid,
		.empty_blob = &empty_blob_oid,
		.null_oid = &null_oid_sha1,
	},
	{
		.name = "sha256",
		.format_id = GIT_SHA256_FORMAT_ID,
		.rawsz = GIT_SHA256_RAWSZ,
		.hexsz = GIT_SHA256_HEXSZ,
		.blksz = GIT_SHA256_BLKSZ,
		.init_fn = git_hash_sha256_init,
		.clone_fn = git_hash_sha256_clone,
		.update_fn = git_hash_sha256_update,
		.final_fn = git_hash_sha256_final,
		.final_oid_fn = git_hash_sha256_final_oid,
		.empty_tree = &empty_tree_oid_sha256,
		.empty_blob = &empty_blob_oid_sha256,
		.null_oid = &null_oid_sha256,
	}
};

const struct object_id *null_oid(const struct git_hash_algo *algop)
{
	return algop->null_oid;
}

const char *empty_tree_oid_hex(const struct git_hash_algo *algop)
{
	static char buf[GIT_MAX_HEXSZ + 1];
	return oid_to_hex_r(buf, algop->empty_tree);
}

const struct git_hash_algo *hash_algo_ptr_by_number(uint32_t algo)
{
	if (algo >= GIT_HASH_NALGOS)
		return NULL;
	return &hash_algos[algo];
}

struct git_hash_ctx *git_hash_alloc(void)
{
	return xmalloc(sizeof(struct git_hash_ctx));
}

void git_hash_free(struct git_hash_ctx *ctx)
{
	free(ctx);
}

void git_hash_init(struct git_hash_ctx *ctx, const struct git_hash_algo *algop)
{
	algop->init_fn(ctx);
}

void git_hash_clone(struct git_hash_ctx *dst, const struct git_hash_ctx *src)
{
	src->algop->clone_fn(dst, src);
}

void git_hash_update(struct git_hash_ctx *ctx, const void *in, size_t len)
{
	ctx->algop->update_fn(ctx, in, len);
}

void git_hash_final(unsigned char *hash, struct git_hash_ctx *ctx)
{
	ctx->algop->final_fn(hash, ctx);
}

void git_hash_final_oid(struct object_id *oid, struct git_hash_ctx *ctx)
{
	ctx->algop->final_oid_fn(oid, ctx);
}

uint32_t hash_algo_by_name(const char *name)
{
	if (!name)
		return GIT_HASH_UNKNOWN;
	for (size_t i = 1; i < GIT_HASH_NALGOS; i++)
		if (!strcmp(name, hash_algos[i].name))
			return i;
	return GIT_HASH_UNKNOWN;
}

uint32_t hash_algo_by_id(uint32_t format_id)
{
	for (size_t i = 1; i < GIT_HASH_NALGOS; i++)
		if (format_id == hash_algos[i].format_id)
			return i;
	return GIT_HASH_UNKNOWN;
}

uint32_t hash_algo_by_length(size_t len)
{
	for (size_t i = 1; i < GIT_HASH_NALGOS; i++)
		if (len == hash_algos[i].rawsz)
			return i;
	return GIT_HASH_UNKNOWN;
}

const struct git_hash_algo *unsafe_hash_algo(const struct git_hash_algo *algop)
{
	/* If we have a faster "unsafe" implementation, use that. */
	if (algop->unsafe)
		return algop->unsafe;
	/* Otherwise use the default one. */
	return algop;
}
