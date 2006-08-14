#include <sys/types.h>
#include <dirent.h>

#include "cache.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tag.h"
#include "refs.h"
#include "pack.h"
#include "cache-tree.h"
#include "tree-walk.h"

#define REACHABLE 0x0001
#define SEEN      0x0002

static int show_root = 0;
static int show_tags = 0;
static int show_unreachable = 0;
static int check_full = 0;
static int check_strict = 0;
static int keep_cache_objects = 0;
static unsigned char head_sha1[20];

#ifdef NO_D_INO_IN_DIRENT
#define SORT_DIRENT 0
#define DIRENT_SORT_HINT(de) 0
#else
#define SORT_DIRENT 1
#define DIRENT_SORT_HINT(de) ((de)->d_ino)
#endif

static void objreport(struct object *obj, const char *severity,
                      const char *err, va_list params)
{
	fprintf(stderr, "%s in %s %s: ",
	        severity, typename(obj->type), sha1_to_hex(obj->sha1));
	vfprintf(stderr, err, params);
	fputs("\n", stderr);
}

static int objerror(struct object *obj, const char *err, ...)
{
	va_list params;
	va_start(params, err);
	objreport(obj, "error", err, params);
	va_end(params);
	return -1;
}

static int objwarning(struct object *obj, const char *err, ...)
{
	va_list params;
	va_start(params, err);
	objreport(obj, "warning", err, params);
	va_end(params);
	return -1;
}


static void check_connectivity(void)
{
	int i, max;

	/* Look up all the requirements, warn about missing objects.. */
	max = get_max_object_index();
	for (i = 0; i < max; i++) {
		const struct object_refs *refs;
		struct object *obj = get_indexed_object(i);

		if (!obj)
			continue;

		if (!obj->parsed) {
			if (has_sha1_file(obj->sha1))
				; /* it is in pack */
			else
				printf("missing %s %s\n",
				       typename(obj->type), sha1_to_hex(obj->sha1));
			continue;
		}

		refs = lookup_object_refs(obj);
		if (refs) {
			unsigned j;
			for (j = 0; j < refs->count; j++) {
				struct object *ref = refs->ref[j];
				if (ref->parsed ||
				    (has_sha1_file(ref->sha1)))
					continue;
				printf("broken link from %7s %s\n",
				       typename(obj->type), sha1_to_hex(obj->sha1));
				printf("              to %7s %s\n",
				       typename(ref->type), sha1_to_hex(ref->sha1));
			}
		}

		if (show_unreachable && !(obj->flags & REACHABLE)) {
			printf("unreachable %s %s\n",
			       typename(obj->type), sha1_to_hex(obj->sha1));
			continue;
		}

		if (!obj->used) {
			printf("dangling %s %s\n", typename(obj->type),
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
#define TREE_UNORDERED (-1)
#define TREE_HAS_DUPS  (-2)

static int verify_ordered(unsigned mode1, const char *name1, unsigned mode2, const char *name2)
{
	int len1 = strlen(name1);
	int len2 = strlen(name2);
	int len = len1 < len2 ? len1 : len2;
	unsigned char c1, c2;
	int cmp;

	cmp = memcmp(name1, name2, len);
	if (cmp < 0)
		return 0;
	if (cmp > 0)
		return TREE_UNORDERED;

	/*
	 * Ok, the first <len> characters are the same.
	 * Now we need to order the next one, but turn
	 * a '\0' into a '/' for a directory entry.
	 */
	c1 = name1[len];
	c2 = name2[len];
	if (!c1 && !c2)
		/*
		 * git-write-tree used to write out a nonsense tree that has
		 * entries with the same name, one blob and one tree.  Make
		 * sure we do not have duplicate entries.
		 */
		return TREE_HAS_DUPS;
	if (!c1 && S_ISDIR(mode1))
		c1 = '/';
	if (!c2 && S_ISDIR(mode2))
		c2 = '/';
	return c1 < c2 ? 0 : TREE_UNORDERED;
}

static int fsck_tree(struct tree *item)
{
	int retval;
	int has_full_path = 0;
	int has_zero_pad = 0;
	int has_bad_modes = 0;
	int has_dup_entries = 0;
	int not_properly_sorted = 0;
	struct tree_desc desc;
	unsigned o_mode;
	const char *o_name;
	const unsigned char *o_sha1;

	desc.buf = item->buffer;
	desc.size = item->size;

	o_mode = 0;
	o_name = NULL;
	o_sha1 = NULL;
	while (desc.size) {
		unsigned mode;
		const char *name;
		const unsigned char *sha1;

		sha1 = tree_entry_extract(&desc, &name, &mode);

		if (strchr(name, '/'))
			has_full_path = 1;
		has_zero_pad |= *(char *)desc.buf == '0';
		update_tree_entry(&desc);

		switch (mode) {
		/*
		 * Standard modes..
		 */
		case S_IFREG | 0755:
		case S_IFREG | 0644:
		case S_IFLNK:
		case S_IFDIR:
			break;
		/*
		 * This is nonstandard, but we had a few of these
		 * early on when we honored the full set of mode
		 * bits..
		 */
		case S_IFREG | 0664:
			if (!check_strict)
				break;
		default:
			has_bad_modes = 1;
		}

		if (o_name) {
			switch (verify_ordered(o_mode, o_name, mode, name)) {
			case TREE_UNORDERED:
				not_properly_sorted = 1;
				break;
			case TREE_HAS_DUPS:
				has_dup_entries = 1;
				break;
			default:
				break;
			}
		}

		o_mode = mode;
		o_name = name;
		o_sha1 = sha1;
	}
	free(item->buffer);
	item->buffer = NULL;

	retval = 0;
	if (has_full_path) {
		objwarning(&item->object, "contains full pathnames");
	}
	if (has_zero_pad) {
		objwarning(&item->object, "contains zero-padded file modes");
	}
	if (has_bad_modes) {
		objwarning(&item->object, "contains bad file modes");
	}
	if (has_dup_entries) {
		retval = objerror(&item->object, "contains duplicate file entries");
	}
	if (not_properly_sorted) {
		retval = objerror(&item->object, "not properly sorted");
	}
	return retval;
}

static int fsck_commit(struct commit *commit)
{
	char *buffer = commit->buffer;
	unsigned char tree_sha1[20], sha1[20];

	if (memcmp(buffer, "tree ", 5))
		return objerror(&commit->object, "invalid format - expected 'tree' line");
	if (get_sha1_hex(buffer+5, tree_sha1) || buffer[45] != '\n')
		return objerror(&commit->object, "invalid 'tree' line format - bad sha1");
	buffer += 46;
	while (!memcmp(buffer, "parent ", 7)) {
		if (get_sha1_hex(buffer+7, sha1) || buffer[47] != '\n')
			return objerror(&commit->object, "invalid 'parent' line format - bad sha1");
		buffer += 48;
	}
	if (memcmp(buffer, "author ", 7))
		return objerror(&commit->object, "invalid format - expected 'author' line");
	free(commit->buffer);
	commit->buffer = NULL;
	if (!commit->tree)
		return objerror(&commit->object, "could not load commit's tree %s", tree_sha1);
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
		return objerror(&tag->object, "could not load tagged object");
	}
	if (!show_tags)
		return 0;

	printf("tagged %s %s", typename(tagged->type), sha1_to_hex(tagged->sha1));
	printf(" (%s) in %s\n", tag->tag, sha1_to_hex(tag->object.sha1));
	return 0;
}

static int fsck_sha1(unsigned char *sha1)
{
	struct object *obj = parse_object(sha1);
	if (!obj)
		return error("%s: object not found", sha1_to_hex(sha1));
	if (obj->flags & SEEN)
		return 0;
	obj->flags |= SEEN;
	if (obj->type == OBJ_BLOB)
		return 0;
	if (obj->type == OBJ_TREE)
		return fsck_tree((struct tree *) obj);
	if (obj->type == OBJ_COMMIT)
		return fsck_commit((struct commit *) obj);
	if (obj->type == OBJ_TAG)
		return fsck_tag((struct tag *) obj);
	/* By now, parse_object() would've returned NULL instead. */
	return objerror(obj, "unknown type '%d' (internal fsck error)", obj->type);
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

	if (SORT_DIRENT)
		qsort(sha1_list.entry, nr,
		      sizeof(struct sha1_entry *), ino_compare);
	for (i = 0; i < nr; i++) {
		struct sha1_entry *entry = sha1_list.entry[i];
		unsigned char *sha1 = entry->sha1;

		sha1_list.entry[i] = NULL;
		fsck_sha1(sha1);
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

static void fsck_dir(int i, char *path)
{
	DIR *dir = opendir(path);
	struct dirent *de;

	if (!dir)
		return;

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
			add_sha1_list(sha1, DIRENT_SORT_HINT(de));
			continue;
		}
		fprintf(stderr, "bad sha1 file: %s/%s\n", path, de->d_name);
	}
	closedir(dir);
}

static int default_refs = 0;

static int fsck_handle_ref(const char *refname, const unsigned char *sha1)
{
	struct object *obj;

	obj = lookup_object(sha1);
	if (!obj) {
		if (has_sha1_file(sha1)) {
			default_refs++;
			return 0; /* it is in a pack */
		}
		error("%s: invalid sha1 pointer %s", refname, sha1_to_hex(sha1));
		/* We'll continue with the rest despite the error.. */
		return 0;
	}
	default_refs++;
	obj->used = 1;
	mark_reachable(obj, REACHABLE);
	return 0;
}

static void get_default_heads(void)
{
	for_each_ref(fsck_handle_ref);
	if (!default_refs)
		die("No default references");
}

static void fsck_object_dir(const char *path)
{
	int i;
	for (i = 0; i < 256; i++) {
		static char dir[4096];
		sprintf(dir, "%s/%02x", path, i);
		fsck_dir(i, dir);
	}
	fsck_sha1_list();
}

static int fsck_head_link(void)
{
	unsigned char sha1[20];
	const char *git_HEAD = strdup(git_path("HEAD"));
	const char *git_refs_heads_master = resolve_ref(git_HEAD, sha1, 1);
	int pfxlen = strlen(git_HEAD) - 4; /* strip .../.git/ part */

	if (!git_refs_heads_master)
		return error("HEAD is not a symbolic ref");
	if (strncmp(git_refs_heads_master + pfxlen, "refs/heads/", 11))
		return error("HEAD points to something strange (%s)",
			     git_refs_heads_master + pfxlen);
	if (!memcmp(null_sha1, sha1, 20))
		return error("HEAD: not a valid git pointer");
	return 0;
}

static int fsck_cache_tree(struct cache_tree *it)
{
	int i;
	int err = 0;

	if (0 <= it->entry_count) {
		struct object *obj = parse_object(it->sha1);
		if (!obj) {
			error("%s: invalid sha1 pointer in cache-tree",
			      sha1_to_hex(it->sha1));
			return 1;
		}
		mark_reachable(obj, REACHABLE);
		obj->used = 1;
		if (obj->type != OBJ_TREE)
			err |= objerror(obj, "non-tree in cache-tree");
	}
	for (i = 0; i < it->subtree_nr; i++)
		err |= fsck_cache_tree(it->down[i]->cache_tree);
	return err;
}

int main(int argc, char **argv)
{
	int i, heads;

	track_object_refs = 1;
	setup_git_directory();

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
		if (!strcmp(arg, "--cache")) {
			keep_cache_objects = 1;
			continue;
		}
		if (!strcmp(arg, "--full")) {
			check_full = 1;
			continue;
		}
		if (!strcmp(arg, "--strict")) {
			check_strict = 1;
			continue;
		}
		if (*arg == '-')
			usage("git-fsck-objects [--tags] [--root] [[--unreachable] [--cache] [--full] [--strict] <head-sha1>*]");
	}

	fsck_head_link();
	fsck_object_dir(get_object_directory());
	if (check_full) {
		struct alternate_object_database *alt;
		struct packed_git *p;
		prepare_alt_odb();
		for (alt = alt_odb_list; alt; alt = alt->next) {
			char namebuf[PATH_MAX];
			int namelen = alt->name - alt->base;
			memcpy(namebuf, alt->base, namelen);
			namebuf[namelen - 1] = 0;
			fsck_object_dir(namebuf);
		}
		prepare_packed_git();
		for (p = packed_git; p; p = p->next)
			/* verify gives error messages itself */
			verify_pack(p, 0);

		for (p = packed_git; p; p = p->next) {
			int num = num_packed_objects(p);
			for (i = 0; i < num; i++) {
				unsigned char sha1[20];
				nth_packed_object_sha1(p, i, sha1);
				fsck_sha1(sha1);
			}
		}
	}

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
		error("invalid parameter: expected sha1, got '%s'", arg);
	}

	/*
	 * If we've not been given any explicit head information, do the
	 * default ones from .git/refs. We also consider the index file
	 * in this case (ie this implies --cache).
	 */
	if (!heads) {
		get_default_heads();
		keep_cache_objects = 1;
	}

	if (keep_cache_objects) {
		int i;
		read_cache();
		for (i = 0; i < active_nr; i++) {
			struct blob *blob = lookup_blob(active_cache[i]->sha1);
			struct object *obj;
			if (!blob)
				continue;
			obj = &blob->object;
			obj->used = 1;
			mark_reachable(obj, REACHABLE);
		}
		if (active_cache_tree)
			fsck_cache_tree(active_cache_tree);
	}

	check_connectivity();
	return 0;
}
