#include "builtin.h"
#include "cache.h"

static const char prune_packed_usage[] =
"git-prune-packed [-n]";

#define DRY_RUN 01
#define VERBOSE 02

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
		if (!has_sha1_pack(sha1, NULL))
			continue;
		memcpy(pathname + len, de->d_name, 38);
		if (opts & DRY_RUN)
			printf("rm -f %s\n", pathname);
		else if (unlink(pathname) < 0)
			error("unable to unlink %s", pathname);
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

	if (len > PATH_MAX - 42)
		die("impossible object directory");
	memcpy(pathname, dir, len);
	if (len && pathname[len-1] != '/')
		pathname[len++] = '/';
	for (i = 0; i < 256; i++) {
		DIR *d;

		sprintf(pathname + len, "%02x/", i);
		d = opendir(pathname);
		if (opts == VERBOSE && (d || i == 255))
			fprintf(stderr, "Removing unused objects %d%%...\015",
				((i+1) * 100) / 256);
		if (!d)
			continue;
		prune_dir(i, d, pathname, len + 3, opts);
		closedir(d);
	}
	if (opts == VERBOSE)
		fprintf(stderr, "\nDone.\n");
}

int cmd_prune_packed(int argc, const char **argv, const char *prefix)
{
	int i;
	int opts = VERBOSE;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp(arg, "-n"))
				opts |= DRY_RUN;
			else if (!strcmp(arg, "-q"))
				opts &= ~VERBOSE;
			else
				usage(prune_packed_usage);
			continue;
		}
		/* Handle arguments here .. */
		usage(prune_packed_usage);
	}
	sync();
	prune_packed_objects(opts);
	return 0;
}
