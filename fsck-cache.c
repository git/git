#include "cache.h"

#include <sys/types.h>
#include <dirent.h>

#include "commit.h"
#include "tree.h"
#include "blob.h"

#define REACHABLE 0x0001

static int show_root = 0;
static int show_tags = 0;
static int show_unreachable = 0;
static unsigned char head_sha1[20];

static void check_connectivity(void)
{
	int i;

	/* Look up all the requirements, warn about missing objects.. */
	for (i = 0; i < nr_objs; i++) {
		struct object *obj = objs[i];

		if (show_unreachable && !(obj->flags & REACHABLE)) {
			printf("unreachable %s %s\n", obj->type, sha1_to_hex(obj->sha1));
			continue;
		}

		if (!obj->parsed) {
			printf("missing %s %s\n", obj->type, 
			       sha1_to_hex(obj->sha1));
		}
		if (!obj->used) {
			printf("dangling %s %s\n", obj->type, 
			       sha1_to_hex(obj->sha1));
		}
	}
}

static int fsck_tree(unsigned char *sha1, void *data, unsigned long size)
{
	struct tree *item = lookup_tree(sha1);
	if (parse_tree(item))
		return -1;
	if (item->has_full_path) {
		fprintf(stderr, "warning: fsck-cache: tree %s "
			"has full pathnames in it\n", sha1_to_hex(sha1));
	}
	return 0;
}

static int fsck_commit(unsigned char *sha1, void *data, unsigned long size)
{
	struct commit *commit = lookup_commit(sha1);
	if (parse_commit(commit))
		return -1;
	if (!commit->tree)
		return -1;
	if (!commit->parents && show_root)
		printf("root %s\n", sha1_to_hex(sha1));
	if (!commit->date)
		printf("bad commit date in %s\n", sha1_to_hex(sha1));
	return 0;
}

static int fsck_blob(unsigned char *sha1, void *data, unsigned long size)
{
	struct blob *blob = lookup_blob(sha1);
	blob->object.parsed = 1;
	return 0;
}

static int fsck_tag(unsigned char *sha1, void *data, unsigned long size)
{
	int typelen, taglen;
	unsigned char object[20];
	char object_hex[60];
	const char *type_line, *tag_line, *sig_line;

	if (size < 64)
		return -1;
	if (memcmp("object ", data, 7) || get_sha1_hex(data + 7, object))
		return -1;

	type_line = data + 48;
	if (memcmp("\ntype ", type_line-1, 6))
		return -1;

	tag_line = strchr(type_line, '\n');
	if (!tag_line || memcmp("tag ", ++tag_line, 4))
		return -1;

	sig_line = strchr(tag_line, '\n');
	if (!sig_line)
		return -1;
	sig_line++;

	typelen = tag_line - type_line - strlen("type \n");
	if (typelen >= 20)
		return -1;
	taglen = sig_line - tag_line - strlen("tag \n");

	if (!show_tags)
		return 0;

	strcpy(object_hex, sha1_to_hex(object));
	printf("tagged %.*s %s (%.*s) in %s\n",
		typelen, type_line + 5,
		object_hex,
		taglen, tag_line + 4,
		sha1_to_hex(sha1));
	return 0;
}

static int fsck_entry(unsigned char *sha1, char *tag, void *data, 
		      unsigned long size)
{
	if (!strcmp(tag, "blob")) {
		if (fsck_blob(sha1, data, size) < 0)
			return -1;
	} else if (!strcmp(tag, "tree")) {
		if (fsck_tree(sha1, data, size) < 0)
			return -1;
	} else if (!strcmp(tag, "commit")) {
		if (fsck_commit(sha1, data, size) < 0)
			return -1;
	} else if (!strcmp(tag, "tag")) {
		if (fsck_tag(sha1, data, size) < 0)
			return -1;
	} else
		return -1;
	return 0;
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
			void *buffer = unpack_sha1_file(map, mapsize, type, &size);
			if (!buffer)
				return -1;
			if (check_sha1_signature(sha1, buffer, size, type) < 0)
				printf("sha1 mismatch %s\n", sha1_to_hex(sha1));
			munmap(map, mapsize);
			if (!fsck_entry(sha1, type, buffer, size))
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
		return error("missing sha1 directory '%s'", path);
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
	int i, heads;
	char *sha1_dir;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--unreachable")) {
			show_unreachable = 1;
			continue;
		}
		if (!strcmp(arg, "--tags")) {
			show_tags = 1;
			continue;
		}
		if (!strcmp(arg, "--root")) {
			show_root = 1;
			continue;
		}
		if (*arg == '-')
			usage("fsck-cache [--tags] [[--unreachable] <head-sha1>*]");
	}

	sha1_dir = getenv(DB_ENVIRONMENT) ? : DEFAULT_DB_ENVIRONMENT;
	for (i = 0; i < 256; i++) {
		static char dir[4096];
		sprintf(dir, "%s/%02x", sha1_dir, i);
		fsck_dir(i, dir);
	}

	heads = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i]; 

		if (*arg == '-')
			continue;

		if (!get_sha1_hex(arg, head_sha1)) {
			struct object *obj = &lookup_commit(head_sha1)->object;
			obj->used = 1;
			mark_reachable(obj, REACHABLE);
			heads++;
			continue;
		}
		error("expected sha1, got %s", arg);
	}

	if (!heads) {
		if (show_unreachable) {
			fprintf(stderr, "unable to do reachability without a head\n");
			show_unreachable = 0; 
		}
		fprintf(stderr, "expect dangling commits - potential heads - due to lack of head information\n");
	}

	check_connectivity();
	return 0;
}
