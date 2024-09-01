#include "builtin.h"
#include "git-compat-util.h"
#include "config.h"
#include "parse-options.h"
#include "repository.h"
#include "commit.h"
#include "hex.h"
#include "tree.h"
#include "tree-walk.h"
#include "object.h"
#include "object-store-ll.h"
#include "oid-array.h"
#include "oidset.h"
#include "promisor-remote.h"
#include "strmap.h"
#include "string-list.h"
#include "revision.h"
#include "trace2.h"
#include "progress.h"
#include "packfile.h"
#include "path-walk.h"

static const char * const builtin_backfill_usage[] = {
	N_("git backfill [--batch-size=<n>]"),
	NULL
};

struct backfill_context {
	struct repository *repo;
	struct oid_array current_batch;
	size_t batch_size;
};

static void clear_backfill_context(struct backfill_context *ctx)
{
	oid_array_clear(&ctx->current_batch);
}

static void download_batch(struct backfill_context *ctx)
{
	promisor_remote_get_direct(ctx->repo,
				   ctx->current_batch.oid,
				   ctx->current_batch.nr);
	oid_array_clear(&ctx->current_batch);

	/*
	 * We likely have a new packfile. Add it to the packed list to
	 * avoid possible duplicate downloads of the same objects.
	 */
	reprepare_packed_git(ctx->repo);
}

static int fill_missing_blobs(const char *path UNUSED,
			      struct oid_array *list,
			      enum object_type type,
			      void *data)
{
	struct backfill_context *ctx = data;

	if (type != OBJ_BLOB)
		return 0;

	for (size_t i = 0; i < list->nr; i++) {
		off_t size = 0;
		struct object_info info = OBJECT_INFO_INIT;
		info.disk_sizep = &size;
		if (oid_object_info_extended(ctx->repo,
					     &list->oid[i],
					     &info,
					     OBJECT_INFO_FOR_PREFETCH) ||
		    !size)
			oid_array_append(&ctx->current_batch, &list->oid[i]);
	}

	if (ctx->current_batch.nr >= ctx->batch_size)
		download_batch(ctx);

	return 0;
}

static int do_backfill(struct backfill_context *ctx)
{
	struct rev_info revs;
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	int ret;

	repo_init_revisions(ctx->repo, &revs, "");
	handle_revision_arg("HEAD", &revs, 0, 0);

	info.blobs = 1;
	info.tags = info.commits = info.trees = 0;

	info.revs = &revs;
	info.path_fn = fill_missing_blobs;
	info.path_fn_data = ctx;

	ret = walk_objects_by_path(&info);

	/* Download the objects that did not fill a batch. */
	if (!ret)
		download_batch(ctx);

	clear_backfill_context(ctx);
	return ret;
}

int cmd_backfill(int argc, const char **argv, const char *prefix, struct repository *repo)
{
	struct backfill_context ctx = {
		.repo = repo,
		.current_batch = OID_ARRAY_INIT,
		.batch_size = 16000,
	};
	struct option options[] = {
		OPT_INTEGER(0, "batch-size", &ctx.batch_size,
			    N_("Minimun number of objects to request at a time")),
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_backfill_usage, options);

	argc = parse_options(argc, argv, prefix, options, builtin_backfill_usage,
			     0);

	repo_config(repo, git_default_config, NULL);

	return do_backfill(&ctx);
}
