#include "cache.h"

#include <sys/types.h>
#include <dirent.h>

/*
 * These three functions should build up a graph in memory about
 * what objects we've referenced, and found, and types..
 *
 * Right now we don't do that kind of reachability checking. Yet.
 */
static void fsck_tree(void *data, unsigned long size)
{
}

static void fsck_commit(void *data, unsigned long size)
{
}

static int mark_sha1_seen(unsigned char *sha1, char *tag)
{
	return 0;
}

static int fsck_entry(unsigned char *sha1, char *tag, void *data, unsigned long size)
{
	if (!strcmp(tag, "blob")) 
		/* Nothing to check */;
	else if (!strcmp(tag, "tree"))
		fsck_tree(data, size);
	else if (!strcmp(tag, "commit"))
		fsck_commit(data, size);
	else
		return -1;
	return mark_sha1_seen(sha1, tag);
}

static int fsck_name(char *hex)
{
	unsigned char sha1[20];
	if (!get_sha1_hex(hex, sha1)) {
		unsigned long mapsize;
		void *map = map_sha1_file(sha1, &mapsize);
		if (map) {
			char type[100];
			unsigned long size;
			void *buffer = NULL;
			if (!check_sha1_signature(sha1, map, mapsize))
				buffer = unpack_sha1_file(map, mapsize, type, &size);
			munmap(map, mapsize);
			if (buffer && !fsck_entry(sha1, type, buffer, size))
				return 0;
		}
	}
	return -1;
}

static int fsck_dir(int i, char *path)
{
	DIR *dir = opendir(path);
	struct dirent *de;

	if (!dir) {
		fprintf(stderr, "missing sha1 directory '%s'", path);
		return -1;
	}

	while ((de = readdir(dir)) != NULL) {
		char name[100];
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
			if (!fsck_name(name))
				continue;
		}
		fprintf(stderr, "bad sha1 file: %s/%s\n", path, de->d_name);
	}
	closedir(dir);
	return 0;
}

int main(int argc, char **argv)
{
	int i;
	char *sha1_dir;

	if (argc != 1)
		usage("fsck-cache");
	sha1_dir = getenv(DB_ENVIRONMENT) ? : DEFAULT_DB_ENVIRONMENT;
	for (i = 0; i < 256; i++) {
		static char dir[4096];
		sprintf(dir, "%s/%02x", sha1_dir, i);
		fsck_dir(i, dir);
	}
	return 0;
}
