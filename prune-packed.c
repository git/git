#include "git-compat-util.h"
#include "environment.h"
#include "gettext.h"
#include "object-store-ll.h"
#include "packfile.h"
#include "progress.h"
#include "prune-packed.h"

static struct progress *progress;

static int prune_subdir(unsigned int nr, const char *path, void *data)
{
	int *opts = data;
	display_progress(progress, nr + 1);
	if (!(*opts & PRUNE_PACKED_DRY_RUN))
		rmdir(path);
	return 0;
}

static int prune_object(const struct object_id *oid, const char *path,
			 void *data)
{
	int *opts = data;

	if (!has_object_pack(oid))
		return 0;

	if (*opts & PRUNE_PACKED_DRY_RUN)
		printf("rm -f %s\n", path);
	else
		unlink_or_warn(path);
	return 0;
}

void prune_packed_objects(int opts)
{
	if (opts & PRUNE_PACKED_VERBOSE)
		progress = start_delayed_progress(_("Removing duplicate objects"), 256);

	for_each_loose_file_in_objdir(get_object_directory(),
				      prune_object, NULL, prune_subdir, &opts);

	/* Ensure we show 100% before finishing progress */
	display_progress(progress, 256);
	stop_progress(&progress);
}
