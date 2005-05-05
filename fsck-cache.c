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

		if (!obj->parsed) {
			printf("missing %s %s\n", obj->type, sha1_to_hex(obj->sha1));
			continue;
		}

		for (refs = obj->refs; refs; refs = refs->next) {
			if (refs->item->parsed)
				continue;
			printf("broken link from %7s %s\n",
			       obj->type, sha1_to_hex(obj->sha1));
			printf("              to %7s %s\n",
			       refs->item->type, sha1_to_hex(refs->item->sha1));
		}

		/* Don't bother with tag reachability. */
		if (obj->type == tag_type)
			continue;

		if (show_unreachable && !(obj->flags & REACHABLE)) {
			printf("unreachable %s %s\n", obj->type, sha1_to_hex(obj->sha1));
			continue;
		}

		if (!obj->used) {
			printf("dangling %s %s\n", obj->type, 
			       sha1_to_hex(obj->sha1));
		}
	}
}

/*
 * The entries in a tree are ordered in the _path_ order,
 * which means that a directory entry is ordered by adding
 * a slash to the end of it.
 *
 * So a directory called "a" is ordered _after_ a file
 * called "a.c", because "a/" sorts after "a.c".
 */
static int verify_ordered(struct tree_entry_list *a, struct tree_entry_list *b)
{
	int len1 = strlen(a->name);
	int len2 = strlen(b->name);
	int len = len1 < len2 ? len1 : len2;
	unsigned char c1, c2;
	int cmp;

	cmp = memcmp(a->name, b->name, len);
	if (cmp < 0)
		return 0;
	if (cmp > 0)
		return -1;

	/*
	 * Ok, the first <len> characters are the same.
	 * Now we need to order the next one, but turn
	 * a '\0' into a '/' for a directory entry.
	 */
	c1 = a->name[len];
	c2 = b->name[len];
	if (!c1 && a->directory)
		c1 = '/';
	if (!c2 && b->directory)
		c2 = '/';
	return c1 < c2 ? 0 : -1;
}

static int fsck_tree(struct tree *item)
{
	int has_full_path = 0;
	struct tree_entry_list *entry, *last;

	last = NULL;
	for (entry = item->entries; entry; entry = entry->next) {
		if (strchr(entry->name, '/'))
			has_full_path = 1;

		if (last) {
			if (verify_ordered(last, entry) < 0) {
				fprintf(stderr, "tree %s not ordered\n",
					sha1_to_hex(item->object.sha1));
				return -1;
			}
		}

		last = entry;
	}

	if (has_full_path) {
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
	struct object *tagged = tag->tagged;

	if (!tagged) {
		printf("bad object in tag %s\n", sha1_to_hex(tag->object.sha1));
		return -1;
	}
	if (!show_tags)
		return 0;

	printf("tagged %s %s", tagged->type, sha1_to_hex(tagged->sha1));
	printf(" (%s) in %s\n", tag->tag, sha1_to_hex(tag->object.sha1));
	return 0;
}

static int fsck_sha1(unsigned char *sha1)
{
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
	return -1;
}

/*
 * This is the sorting chunk size: make it reasonably
 * big so that we can sort well..
 */
#define MAX_SHA1_ENTRIES (1024)

struct sha1_entry {
	unsigned long ino;
	unsigned char sha1[20];
};

static struct {
	unsigned long nr;
	struct sha1_entry *entry[MAX_SHA1_ENTRIES];
} sha1_list;

static int ino_compare(const void *_a, const void *_b)
{
	const struct sha1_entry *a = _a, *b = _b;
	unsigned long ino1 = a->ino, ino2 = b->ino;
	return ino1 < ino2 ? -1 : ino1 > ino2 ? 1 : 0;
}

static void fsck_sha1_list(void)
{
	int i, nr = sha1_list.nr;

	qsort(sha1_list.entry, nr, sizeof(struct sha1_entry *), ino_compare);
	for (i = 0; i < nr; i++) {
		struct sha1_entry *entry = sha1_list.entry[i];
		unsigned char *sha1 = entry->sha1;

		sha1_list.entry[i] = NULL;
		if (fsck_sha1(sha1) < 0)
			fprintf(stderr, "bad sha1 entry '%s'\n", sha1_to_hex(sha1));
		free(entry);
	}
	sha1_list.nr = 0;
}

static void add_sha1_list(unsigned char *sha1, unsigned long ino)
{
	struct sha1_entry *entry = xmalloc(sizeof(*entry));
	int nr;

	entry->ino = ino;
	memcpy(entry->sha1, sha1, 20);
	nr = sha1_list.nr;
	if (nr == MAX_SHA1_ENTRIES) {
		fsck_sha1_list();
		nr = 0;
	}
	sha1_list.entry[nr] = entry;
	sha1_list.nr = ++nr;
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
			add_sha1_list(sha1, de->d_ino);
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
	fsck_sha1_list();

	heads = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i]; 

		if (*arg == '-')
			continue;

		if (!get_sha1(arg, head_sha1)) {
			struct object *obj = lookup_object(head_sha1);

			/* Error is printed by lookup_object(). */
			if (!obj)
				continue;

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
