#include "git-compat-util.h"
#include "gettext.h"
#include "object-file.h"
#include "packfile.h"
#include "progress.h"
#include "prune-packed.h"
#include "repository.h"

struct prune_data {
	struct repository *r;
	int opts;
};

static struct progress *progress;

static int prune_subdir(unsigned int nr, const char *path, void *data)
{
	struct prune_data *d = data;
	display_progress(progress, nr + 1);
	if (!(d->opts & PRUNE_PACKED_DRY_RUN))
		rmdir(path);
	return 0;
}

static int prune_object(const struct object_id *oid, const char *path,
			void *data)
{
	struct prune_data *d = data;

	if (!has_object_pack(d->r, oid))
		return 0;

	if (d->opts & PRUNE_PACKED_DRY_RUN)
		printf("rm -f %s\n", path);
	else
		unlink_or_warn(path);
	return 0;
}

void prune_packed_objects(struct repository *r, int opts)
{
	struct prune_data cb_data = {
		.r = r,
		.opts = opts
	};

	if (opts & PRUNE_PACKED_VERBOSE)
		progress = start_delayed_progress(r,
						  _("Removing duplicate objects"), 256);

	for_each_loose_file_in_source(r->objects->source,
				      prune_object, NULL, prune_subdir, &cb_data);

	/* Ensure we show 100% before finishing progress */
	display_progress(progress, 256);
	stop_progress(&progress);
}
