#include <sys/types.h>
#include <dirent.h>

#include "cache.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tag.h"

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
		struct object_list *refs;

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
		for (refs = obj->refs; refs; refs = refs->next) {
			if (!refs->item->parsed) {
				printf("broken link from %s\n",
				       sha1_to_hex(obj->sha1));
				printf("              to %s\n",
				       sha1_to_hex(refs->item->sha1));
			}
		}
	}
}

static int fsck_tree(struct tree *item)
{
	if (item->has_full_path) {
		fprintf(stderr, "warning: fsck-cache: tree %s "
			"has full pathnames in it\n", 
			sha1_to_hex(item->object.sha1));
	}
	return 0;
}

static int fsck_commit(struct commit *commit)
{
	if (!commit->tree)
		return -1;
	if (!commit->parents && show_root)
		printf("root %s\n", sha1_to_hex(commit->object.sha1));
	if (!commit->date)
		printf("bad commit date in %s\n", 
		       sha1_to_hex(commit->object.sha1));
	return 0;
}

static int fsck_tag(struct tag *tag)
{
	if (!show_tags)
		return 0;

	printf("tagged %s %s",
	       tag->tagged->type,
	       sha1_to_hex(tag->tagged->sha1));
	printf(" (%s) in %s\n",
	       tag->tag, sha1_to_hex(tag->object.sha1));
	return 0;
}

static int fsck_name(char *hex)
{
	unsigned char sha1[20];
	if (!get_sha1_hex(hex, sha1)) {
		struct object *obj = parse_object(sha1);
		if (!obj)
			return -1;
		if (obj->type == blob_type)
			return 0;
		if (obj->type == tree_type)
			return fsck_tree((struct tree *) obj);
		if (obj->type == commit_type)
			return fsck_commit((struct commit *) obj);
		if (obj->type == tag_type)
			return fsck_tag((struct tag *) obj);
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
			struct commit *commit = lookup_commit(head_sha1);
			struct object *obj;

			/* Error is printed by lookup_commit(). */
			if (!commit)
				continue;

			obj = &commit->object;
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
