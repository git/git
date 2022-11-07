#include "../cache.h"
#include "../config.h"
#include "../refs.h"
#include "refs-internal.h"
#include "packed-backend.h"
#include "../iterator.h"
#include "../lockfile.h"
#include "../chdir-notify.h"

struct write_packed_refs_v2_context {
	struct packed_ref_store *refs;
	struct string_list *updates;
	struct strbuf *err;
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

	return ctx;
}

int write_packed_refs_v2(struct write_packed_refs_v2_context *ctx)
{
	return 0;
}

void free_v2_context(struct write_packed_refs_v2_context *ctx)
{
	free(ctx);
}
