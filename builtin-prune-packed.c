#include "builtin.h"
#include "cache.h"
#include "progress.h"
#include "parse-options.h"

static const char * const prune_packed_usage[] = {
	"git prune-packed [-n|--dry-run] [-q|--quiet]",
	NULL
};

#define DRY_RUN 01
#define VERBOSE 02

static struct progress *progress;

static void prune_dir(int i, DIR *dir, char *pathname, int len, int opts)
{
	struct dirent *de;
	char hex[40];

	sprintf(hex, "%02x", i);
	while ((de = readdir(dir)) != NULL) {
		unsigned char sha1[20];
		if (strlen(de->d_name) != 38)
			continue;
		memcpy(hex+2, de->d_name, 38);
		if (get_sha1_hex(hex, sha1))
			continue;
		if (!has_sha1_pack(sha1))
			continue;
		memcpy(pathname + len, de->d_name, 38);
		if (opts & DRY_RUN)
			printf("rm -f %s\n", pathname);
		else
			unlink_or_warn(pathname);
		display_progress(progress, i + 1);
	}
	pathname[len] = 0;
	rmdir(pathname);
}

void prune_packed_objects(int opts)
{
	int i;
	static char pathname[PATH_MAX];
	const char *dir = get_object_directory();
	int len = strlen(dir);

	if (opts == VERBOSE)
		progress = start_progress_delay("Removing duplicate objects",
			256, 95, 2);

	if (len > PATH_MAX - 42)
		die("impossible object directory");
	memcpy(pathname, dir, len);
	if (len && pathname[len-1] != '/')
		pathname[len++] = '/';
	for (i = 0; i < 256; i++) {
		DIR *d;

		display_progress(progress, i + 1);
		sprintf(pathname + len, "%02x/", i);
		d = opendir(pathname);
		if (!d)
			continue;
		prune_dir(i, d, pathname, len + 3, opts);
		closedir(d);
	}
	stop_progress(&progress);
}

int cmd_prune_packed(int argc, const char **argv, const char *prefix)
{
	int opts = isatty(2) ? VERBOSE : 0;
	const struct option prune_packed_options[] = {
		OPT_BIT('n', "dry-run", &opts, "dry run", DRY_RUN),
		OPT_NEGBIT('q', "quiet", &opts, "be quiet", VERBOSE),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, prune_packed_options,
			     prune_packed_usage, 0);

	prune_packed_objects(opts);
	return 0;
}
