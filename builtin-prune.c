#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "builtin.h"
#include "reachable.h"

static const char prune_usage[] = "git-prune [-n]";
static int show_only;

static int prune_object(char *path, const char *filename, const unsigned char *sha1)
{
	if (show_only) {
		enum object_type type = sha1_object_info(sha1, NULL);
		printf("%s %s\n", sha1_to_hex(sha1),
		       (type > 0) ? typename(type) : "unknown");
		return 0;
	}
	unlink(mkpath("%s/%s", path, filename));
	rmdir(path);
	return 0;
}

static int prune_dir(int i, char *path)
{
	DIR *dir = opendir(path);
	struct dirent *de;

	if (!dir)
		return 0;

	while ((de = readdir(dir)) != NULL) {
		char name[100];
		unsigned char sha1[20];
		int len = strlen(de->d_name);

		switch (len) {
		case 2:
			if (de->d_name[1] != '.')
				break;
		case 1:
			if (de->d_name[0] != '.')
				break;
			continue;
		case 38:
			sprintf(name, "%02x", i);
			memcpy(name+2, de->d_name, len+1);
			if (get_sha1_hex(name, sha1) < 0)
				break;

			/*
			 * Do we know about this object?
			 * It must have been reachable
			 */
			if (lookup_object(sha1))
				continue;

			prune_object(path, de->d_name, sha1);
			continue;
		}
		fprintf(stderr, "bad sha1 file: %s/%s\n", path, de->d_name);
	}
	closedir(dir);
	return 0;
}

static void prune_object_dir(const char *path)
{
	int i;
	for (i = 0; i < 256; i++) {
		static char dir[4096];
		sprintf(dir, "%s/%02x", path, i);
		prune_dir(i, dir);
	}
}

int cmd_prune(int argc, const char **argv, const char *prefix)
{
	int i;
	struct rev_info revs;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "-n")) {
			show_only = 1;
			continue;
		}
		usage(prune_usage);
	}

	save_commit_buffer = 0;
	init_revisions(&revs, prefix);
	mark_reachable_objects(&revs, 1);

	prune_object_dir(get_object_directory());

	sync();
	prune_packed_objects(show_only);
	return 0;
}
