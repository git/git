#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tag.h"
#include "refs.h"
#include "pack.h"
#include "cache-tree.h"
#include "tree-walk.h"
#include "fsck.h"
#include "parse-options.h"
#include "dir.h"
#include "progress.h"
#include "streaming.h"

#define REACHABLE 0x0001
#define SEEN      0x0002
#define HAS_OBJ   0x0004

static int show_root;
static int show_tags;
static int show_unreachable;
static int include_reflogs = 1;
static int check_full = 1;
static int check_strict;
static int keep_cache_objects;
static unsigned char head_sha1[20];
static const char *head_points_at;
static int errors_found;
static int write_lost_and_found;
static int verbose;
static int show_progress = -1;
static int show_dangling = 1;
#define ERROR_OBJECT 01
#define ERROR_REACHABLE 02
#define ERROR_PACK 04

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

__attribute__((format (printf, 2, 3)))
static int objerror(struct object *obj, const char *err, ...)
{
	va_list params;
	va_start(params, err);
	errors_found |= ERROR_OBJECT;
	objreport(obj, "error", err, params);
	va_end(params);
	return -1;
}

__attribute__((format (printf, 3, 4)))
static int fsck_error_func(struct object *obj, int type, const char *err, ...)
{
	va_list params;
	va_start(params, err);
	objreport(obj, (type == FSCK_WARN) ? "warning" : "error", err, params);
	va_end(params);
	return (type == FSCK_WARN) ? 0 : 1;
}

static struct object_array pending;

static int mark_object(struct object *obj, int type, void *data)
{
	struct object *parent = data;

	/*
	 * The only case data is NULL or type is OBJ_ANY is when
	 * mark_object_reachable() calls us.  All the callers of
	 * that function has non-NULL obj hence ...
	 */
	if (!obj) {
		/* ... these references to parent->fld are safe here */
		printf("broken link from %7s %s\n",
			   typename(parent->type), sha1_to_hex(parent->sha1));
		printf("broken link from %7s %s\n",
			   (type == OBJ_ANY ? "unknown" : typename(type)), "unknown");
		errors_found |= ERROR_REACHABLE;
		return 1;
	}

	if (type != OBJ_ANY && obj->type != type)
		/* ... and the reference to parent is safe here */
		objerror(parent, "wrong object type in link");

	if (obj->flags & REACHABLE)
		return 0;
	obj->flags |= REACHABLE;
	if (!(obj->flags & HAS_OBJ)) {
		if (parent && !has_sha1_file(obj->sha1)) {
			printf("broken link from %7s %s\n",
				 typename(parent->type), sha1_to_hex(parent->sha1));
			printf("              to %7s %s\n",
				 typename(obj->type), sha1_to_hex(obj->sha1));
			errors_found |= ERROR_REACHABLE;
		}
		return 1;
	}

	add_object_array(obj, NULL, &pending);
	return 0;
}

static void mark_object_reachable(struct object *obj)
{
	mark_object(obj, OBJ_ANY, NULL);
}

static int traverse_one_object(struct object *obj)
{
	int result;
	struct tree *tree = NULL;

	if (obj->type == OBJ_TREE) {
		tree = (struct tree *)obj;
		if (parse_tree(tree) < 0)
			return 1; /* error already displayed */
	}
	result = fsck_walk(obj, mark_object, obj);
	if (tree)
		free_tree_buffer(tree);
	return result;
}

static int traverse_reachable(void)
{
	struct progress *progress = NULL;
	unsigned int nr = 0;
	int result = 0;
	if (show_progress)
		progress = start_progress_delay(_("Checking connectivity"), 0, 0, 2);
	while (pending.nr) {
		struct object_array_entry *entry;
		struct object *obj;

		entry = pending.objects + --pending.nr;
		obj = entry->item;
		result |= traverse_one_object(obj);
		display_progress(progress, ++nr);
	}
	stop_progress(&progress);
	return !!result;
}

static int mark_used(struct object *obj, int type, void *data)
{
	if (!obj)
		return 1;
	obj->used = 1;
	return 0;
}

/*
 * Check a single reachable object
 */
static void check_reachable_object(struct object *obj)
{
	/*
	 * We obviously want the object to be parsed,
	 * except if it was in a pack-file and we didn't
	 * do a full fsck
	 */
	if (!(obj->flags & HAS_OBJ)) {
		if (has_sha1_pack(obj->sha1))
			return; /* it is in pack - forget about it */
		printf("missing %s %s\n", typename(obj->type), sha1_to_hex(obj->sha1));
		errors_found |= ERROR_REACHABLE;
		return;
	}
}

/*
 * Check a single unreachable object
 */
static void check_unreachable_object(struct object *obj)
{
	/*
	 * Missing unreachable object? Ignore it. It's not like
	 * we miss it (since it can't be reached), nor do we want
	 * to complain about it being unreachable (since it does
	 * not exist).
	 */
	if (!obj->parsed)
		return;

	/*
	 * Unreachable object that exists? Show it if asked to,
	 * since this is something that is prunable.
	 */
	if (show_unreachable) {
		printf("unreachable %s %s\n", typename(obj->type), sha1_to_hex(obj->sha1));
		return;
	}

	/*
	 * "!used" means that nothing at all points to it, including
	 * other unreachable objects. In other words, it's the "tip"
	 * of some set of unreachable objects, usually a commit that
	 * got dropped.
	 *
	 * Such starting points are more interesting than some random
	 * set of unreachable objects, so we show them even if the user
	 * hasn't asked for _all_ unreachable objects. If you have
	 * deleted a branch by mistake, this is a prime candidate to
	 * start looking at, for example.
	 */
	if (!obj->used) {
		if (show_dangling)
			printf("dangling %s %s\n", typename(obj->type),
			       sha1_to_hex(obj->sha1));
		if (write_lost_and_found) {
			char *filename = git_path("lost-found/%s/%s",
				obj->type == OBJ_COMMIT ? "commit" : "other",
				sha1_to_hex(obj->sha1));
			FILE *f;

			if (safe_create_leading_directories(filename)) {
				error("Could not create lost-found");
				return;
			}
			if (!(f = fopen(filename, "w")))
				die_errno("Could not open '%s'", filename);
			if (obj->type == OBJ_BLOB) {
				if (stream_blob_to_fd(fileno(f), obj->sha1, NULL, 1))
					die_errno("Could not write '%s'", filename);
			} else
				fprintf(f, "%s\n", sha1_to_hex(obj->sha1));
			if (fclose(f))
				die_errno("Could not finish '%s'",
					  filename);
		}
		return;
	}

	/*
	 * Otherwise? It's there, it's unreachable, and some other unreachable
	 * object points to it. Ignore it - it's not interesting, and we showed
	 * all the interesting cases above.
	 */
}

static void check_object(struct object *obj)
{
	if (verbose)
		fprintf(stderr, "Checking %s\n", sha1_to_hex(obj->sha1));

	if (obj->flags & REACHABLE)
		check_reachable_object(obj);
	else
		check_unreachable_object(obj);
}

static void check_connectivity(void)
{
	int i, max;

	/* Traverse the pending reachable objects */
	traverse_reachable();

	/* Look up all the requirements, warn about missing objects.. */
	max = get_max_object_index();
	if (verbose)
		fprintf(stderr, "Checking connectivity (%d objects)\n", max);

	for (i = 0; i < max; i++) {
		struct object *obj = get_indexed_object(i);

		if (obj)
			check_object(obj);
	}
}

static int fsck_obj(struct object *obj)
{
	if (obj->flags & SEEN)
		return 0;
	obj->flags |= SEEN;

	if (verbose)
		fprintf(stderr, "Checking %s %s\n",
			typename(obj->type), sha1_to_hex(obj->sha1));

	if (fsck_walk(obj, mark_used, NULL))
		objerror(obj, "broken links");
	if (fsck_object(obj, check_strict, fsck_error_func))
		return -1;

	if (obj->type == OBJ_TREE) {
		struct tree *item = (struct tree *) obj;

		free_tree_buffer(item);
	}

	if (obj->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *) obj;

		free_commit_buffer(commit);

		if (!commit->parents && show_root)
			printf("root %s\n", sha1_to_hex(commit->object.sha1));
	}

	if (obj->type == OBJ_TAG) {
		struct tag *tag = (struct tag *) obj;

		if (show_tags && tag->tagged) {
			printf("tagged %s %s", typename(tag->tagged->type), sha1_to_hex(tag->tagged->sha1));
			printf(" (%s) in %s\n", tag->tag, sha1_to_hex(tag->object.sha1));
		}
	}

	return 0;
}

static int fsck_sha1(const unsigned char *sha1)
{
	struct object *obj = parse_object(sha1);
	if (!obj) {
		errors_found |= ERROR_OBJECT;
		return error("%s: object corrupt or missing",
			     sha1_to_hex(sha1));
	}
	obj->flags |= HAS_OBJ;
	return fsck_obj(obj);
}

static int fsck_obj_buffer(const unsigned char *sha1, enum object_type type,
			   unsigned long size, void *buffer, int *eaten)
{
	struct object *obj;
	obj = parse_object_buffer(sha1, type, size, buffer, eaten);
	if (!obj) {
		errors_found |= ERROR_OBJECT;
		return error("%s: object corrupt or missing", sha1_to_hex(sha1));
	}
	obj->flags = HAS_OBJ;
	return fsck_obj(obj);
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
		if (fsck_sha1(sha1))
			errors_found |= ERROR_OBJECT;
		free(entry);
	}
	sha1_list.nr = 0;
}

static void add_sha1_list(unsigned char *sha1, unsigned long ino)
{
	struct sha1_entry *entry = xmalloc(sizeof(*entry));
	int nr;

	entry->ino = ino;
	hashcpy(entry->sha1, sha1);
	nr = sha1_list.nr;
	if (nr == MAX_SHA1_ENTRIES) {
		fsck_sha1_list();
		nr = 0;
	}
	sha1_list.entry[nr] = entry;
	sha1_list.nr = ++nr;
}

static inline int is_loose_object_file(struct dirent *de,
				       char *name, unsigned char *sha1)
{
	if (strlen(de->d_name) != 38)
		return 0;
	memcpy(name + 2, de->d_name, 39);
	return !get_sha1_hex(name, sha1);
}

static void fsck_dir(int i, char *path)
{
	DIR *dir = opendir(path);
	struct dirent *de;
	char name[100];

	if (!dir)
		return;

	if (verbose)
		fprintf(stderr, "Checking directory %s\n", path);

	sprintf(name, "%02x", i);
	while ((de = readdir(dir)) != NULL) {
		unsigned char sha1[20];

		if (is_dot_or_dotdot(de->d_name))
			continue;
		if (is_loose_object_file(de, name, sha1)) {
			add_sha1_list(sha1, DIRENT_SORT_HINT(de));
			continue;
		}
		if (starts_with(de->d_name, "tmp_obj_"))
			continue;
		fprintf(stderr, "bad sha1 file: %s/%s\n", path, de->d_name);
	}
	closedir(dir);
}

static int default_refs;

static int fsck_handle_reflog_ent(unsigned char *osha1, unsigned char *nsha1,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	struct object *obj;

	if (verbose)
		fprintf(stderr, "Checking reflog %s->%s\n",
			sha1_to_hex(osha1), sha1_to_hex(nsha1));

	if (!is_null_sha1(osha1)) {
		obj = lookup_object(osha1);
		if (obj) {
			obj->used = 1;
			mark_object_reachable(obj);
		}
	}
	obj = lookup_object(nsha1);
	if (obj) {
		obj->used = 1;
		mark_object_reachable(obj);
	}
	return 0;
}

static int fsck_handle_reflog(const char *logname, const unsigned char *sha1, int flag, void *cb_data)
{
	for_each_reflog_ent(logname, fsck_handle_reflog_ent, NULL);
	return 0;
}

static int fsck_handle_ref(const char *refname, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *obj;

	obj = parse_object(sha1);
	if (!obj) {
		error("%s: invalid sha1 pointer %s", refname, sha1_to_hex(sha1));
		errors_found |= ERROR_REACHABLE;
		/* We'll continue with the rest despite the error.. */
		return 0;
	}
	if (obj->type != OBJ_COMMIT && is_branch(refname))
		error("%s: not a commit", refname);
	default_refs++;
	obj->used = 1;
	mark_object_reachable(obj);

	return 0;
}

static void get_default_heads(void)
{
	if (head_points_at && !is_null_sha1(head_sha1))
		fsck_handle_ref("HEAD", head_sha1, 0, NULL);
	for_each_rawref(fsck_handle_ref, NULL);
	if (include_reflogs)
		for_each_reflog(fsck_handle_reflog, NULL);

	/*
	 * Not having any default heads isn't really fatal, but
	 * it does mean that "--unreachable" no longer makes any
	 * sense (since in this case everything will obviously
	 * be unreachable by definition.
	 *
	 * Showing dangling objects is valid, though (as those
	 * dangling objects are likely lost heads).
	 *
	 * So we just print a warning about it, and clear the
	 * "show_unreachable" flag.
	 */
	if (!default_refs) {
		fprintf(stderr, "notice: No default references\n");
		show_unreachable = 0;
	}
}

static void fsck_object_dir(const char *path)
{
	int i;
	struct progress *progress = NULL;

	if (verbose)
		fprintf(stderr, "Checking object directory\n");

	if (show_progress)
		progress = start_progress(_("Checking object directories"), 256);
	for (i = 0; i < 256; i++) {
		static char dir[4096];
		sprintf(dir, "%s/%02x", path, i);
		fsck_dir(i, dir);
		display_progress(progress, i+1);
	}
	stop_progress(&progress);
	fsck_sha1_list();
}

static int fsck_head_link(void)
{
	int flag;
	int null_is_error = 0;

	if (verbose)
		fprintf(stderr, "Checking HEAD link\n");

	head_points_at = resolve_ref_unsafe("HEAD", head_sha1, 0, &flag);
	if (!head_points_at)
		return error("Invalid HEAD");
	if (!strcmp(head_points_at, "HEAD"))
		/* detached HEAD */
		null_is_error = 1;
	else if (!starts_with(head_points_at, "refs/heads/"))
		return error("HEAD points to something strange (%s)",
			     head_points_at);
	if (is_null_sha1(head_sha1)) {
		if (null_is_error)
			return error("HEAD: detached HEAD points at nothing");
		fprintf(stderr, "notice: HEAD points to an unborn branch (%s)\n",
			head_points_at + 11);
	}
	return 0;
}

static int fsck_cache_tree(struct cache_tree *it)
{
	int i;
	int err = 0;

	if (verbose)
		fprintf(stderr, "Checking cache tree\n");

	if (0 <= it->entry_count) {
		struct object *obj = parse_object(it->sha1);
		if (!obj) {
			error("%s: invalid sha1 pointer in cache-tree",
			      sha1_to_hex(it->sha1));
			return 1;
		}
		obj->used = 1;
		mark_object_reachable(obj);
		if (obj->type != OBJ_TREE)
			err |= objerror(obj, "non-tree in cache-tree");
	}
	for (i = 0; i < it->subtree_nr; i++)
		err |= fsck_cache_tree(it->down[i]->cache_tree);
	return err;
}

static char const * const fsck_usage[] = {
	N_("git fsck [options] [<object>...]"),
	NULL
};

static struct option fsck_opts[] = {
	OPT__VERBOSE(&verbose, N_("be verbose")),
	OPT_BOOL(0, "unreachable", &show_unreachable, N_("show unreachable objects")),
	OPT_BOOL(0, "dangling", &show_dangling, N_("show dangling objects")),
	OPT_BOOL(0, "tags", &show_tags, N_("report tags")),
	OPT_BOOL(0, "root", &show_root, N_("report root nodes")),
	OPT_BOOL(0, "cache", &keep_cache_objects, N_("make index objects head nodes")),
	OPT_BOOL(0, "reflogs", &include_reflogs, N_("make reflogs head nodes (default)")),
	OPT_BOOL(0, "full", &check_full, N_("also consider packs and alternate objects")),
	OPT_BOOL(0, "strict", &check_strict, N_("enable more strict checking")),
	OPT_BOOL(0, "lost-found", &write_lost_and_found,
				N_("write dangling objects in .git/lost-found")),
	OPT_BOOL(0, "progress", &show_progress, N_("show progress")),
	OPT_END(),
};

int cmd_fsck(int argc, const char **argv, const char *prefix)
{
	int i, heads;
	struct alternate_object_database *alt;

	errors_found = 0;
	check_replace_refs = 0;

	argc = parse_options(argc, argv, prefix, fsck_opts, fsck_usage, 0);

	if (show_progress == -1)
		show_progress = isatty(2);
	if (verbose)
		show_progress = 0;

	if (write_lost_and_found) {
		check_full = 1;
		include_reflogs = 0;
	}

	fsck_head_link();
	fsck_object_dir(get_object_directory());

	prepare_alt_odb();
	for (alt = alt_odb_list; alt; alt = alt->next) {
		char namebuf[PATH_MAX];
		int namelen = alt->name - alt->base;
		memcpy(namebuf, alt->base, namelen);
		namebuf[namelen - 1] = 0;
		fsck_object_dir(namebuf);
	}

	if (check_full) {
		struct packed_git *p;
		uint32_t total = 0, count = 0;
		struct progress *progress = NULL;

		prepare_packed_git();

		if (show_progress) {
			for (p = packed_git; p; p = p->next) {
				if (open_pack_index(p))
					continue;
				total += p->num_objects;
			}

			progress = start_progress(_("Checking objects"), total);
		}
		for (p = packed_git; p; p = p->next) {
			/* verify gives error messages itself */
			if (verify_pack(p, fsck_obj_buffer,
					progress, count))
				errors_found |= ERROR_PACK;
			count += p->num_objects;
		}
		stop_progress(&progress);
	}

	heads = 0;
	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];
		unsigned char sha1[20];
		if (!get_sha1(arg, sha1)) {
			struct object *obj = lookup_object(sha1);

			/* Error is printed by lookup_object(). */
			if (!obj)
				continue;

			obj->used = 1;
			mark_object_reachable(obj);
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
		read_cache();
		for (i = 0; i < active_nr; i++) {
			unsigned int mode;
			struct blob *blob;
			struct object *obj;

			mode = active_cache[i]->ce_mode;
			if (S_ISGITLINK(mode))
				continue;
			blob = lookup_blob(active_cache[i]->sha1);
			if (!blob)
				continue;
			obj = &blob->object;
			obj->used = 1;
			mark_object_reachable(obj);
		}
		if (active_cache_tree)
			fsck_cache_tree(active_cache_tree);
	}

	check_connectivity();
	return errors_found;
}
