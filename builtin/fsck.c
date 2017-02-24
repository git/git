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
#include "decorate.h"

#define REACHABLE 0x0001
#define SEEN      0x0002
#define HAS_OBJ   0x0004

static int show_root;
static int show_tags;
static int show_unreachable;
static int include_reflogs = 1;
static int check_full = 1;
static int connectivity_only;
static int check_strict;
static int keep_cache_objects;
static struct fsck_options fsck_walk_options = FSCK_OPTIONS_DEFAULT;
static struct fsck_options fsck_obj_options = FSCK_OPTIONS_DEFAULT;
static struct object_id head_oid;
static const char *head_points_at;
static int errors_found;
static int write_lost_and_found;
static int verbose;
static int show_progress = -1;
static int show_dangling = 1;
static int name_objects;
#define ERROR_OBJECT 01
#define ERROR_REACHABLE 02
#define ERROR_PACK 04
#define ERROR_REFS 010

static const char *describe_object(struct object *obj)
{
	static struct strbuf buf = STRBUF_INIT;
	char *name = name_objects ?
		lookup_decoration(fsck_walk_options.object_names, obj) : NULL;

	strbuf_reset(&buf);
	strbuf_addstr(&buf, oid_to_hex(&obj->oid));
	if (name)
		strbuf_addf(&buf, " (%s)", name);

	return buf.buf;
}

static const char *printable_type(struct object *obj)
{
	const char *ret;

	if (obj->type == OBJ_NONE) {
		enum object_type type = sha1_object_info(obj->oid.hash, NULL);
		if (type > 0)
			object_as_type(obj, type, 0);
	}

	ret = typename(obj->type);
	if (!ret)
		ret = "unknown";

	return ret;
}

static int fsck_config(const char *var, const char *value, void *cb)
{
	if (strcmp(var, "fsck.skiplist") == 0) {
		const char *path;
		struct strbuf sb = STRBUF_INIT;

		if (git_config_pathname(&path, var, value))
			return 1;
		strbuf_addf(&sb, "skiplist=%s", path);
		free((char *)path);
		fsck_set_msg_types(&fsck_obj_options, sb.buf);
		strbuf_release(&sb);
		return 0;
	}

	if (skip_prefix(var, "fsck.", &var)) {
		fsck_set_msg_type(&fsck_obj_options, var, value);
		return 0;
	}

	return git_default_config(var, value, cb);
}

static void objreport(struct object *obj, const char *msg_type,
			const char *err)
{
	fprintf(stderr, "%s in %s %s: %s\n",
		msg_type, printable_type(obj), describe_object(obj), err);
}

static int objerror(struct object *obj, const char *err)
{
	errors_found |= ERROR_OBJECT;
	objreport(obj, "error", err);
	return -1;
}

static int fsck_error_func(struct fsck_options *o,
	struct object *obj, int type, const char *message)
{
	objreport(obj, (type == FSCK_WARN) ? "warning" : "error", message);
	return (type == FSCK_WARN) ? 0 : 1;
}

static struct object_array pending;

static int mark_object(struct object *obj, int type, void *data, struct fsck_options *options)
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
			   printable_type(parent), describe_object(parent));
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
		if (parent && !has_object_file(&obj->oid)) {
			printf("broken link from %7s %s\n",
				 printable_type(parent), describe_object(parent));
			printf("              to %7s %s\n",
				 printable_type(obj), describe_object(obj));
			errors_found |= ERROR_REACHABLE;
		}
		return 1;
	}

	add_object_array(obj, NULL, &pending);
	return 0;
}

static void mark_object_reachable(struct object *obj)
{
	mark_object(obj, OBJ_ANY, NULL, NULL);
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
	result = fsck_walk(obj, obj, &fsck_walk_options);
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

static int mark_used(struct object *obj, int type, void *data, struct fsck_options *options)
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
		if (has_sha1_pack(obj->oid.hash))
			return; /* it is in pack - forget about it */
		printf("missing %s %s\n", printable_type(obj),
			describe_object(obj));
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
	if (!(obj->flags & HAS_OBJ))
		return;

	/*
	 * Unreachable object that exists? Show it if asked to,
	 * since this is something that is prunable.
	 */
	if (show_unreachable) {
		printf("unreachable %s %s\n", printable_type(obj),
			describe_object(obj));
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
			printf("dangling %s %s\n", printable_type(obj),
			       describe_object(obj));
		if (write_lost_and_found) {
			char *filename = git_pathdup("lost-found/%s/%s",
				obj->type == OBJ_COMMIT ? "commit" : "other",
				describe_object(obj));
			FILE *f;

			if (safe_create_leading_directories_const(filename)) {
				error("Could not create lost-found");
				free(filename);
				return;
			}
			if (!(f = fopen(filename, "w")))
				die_errno("Could not open '%s'", filename);
			if (obj->type == OBJ_BLOB) {
				if (stream_blob_to_fd(fileno(f), &obj->oid, NULL, 1))
					die_errno("Could not write '%s'", filename);
			} else
				fprintf(f, "%s\n", describe_object(obj));
			if (fclose(f))
				die_errno("Could not finish '%s'",
					  filename);
			free(filename);
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
		fprintf(stderr, "Checking %s\n", describe_object(obj));

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
			printable_type(obj), describe_object(obj));

	if (fsck_walk(obj, NULL, &fsck_obj_options))
		objerror(obj, "broken links");
	if (fsck_object(obj, NULL, 0, &fsck_obj_options))
		return -1;

	if (obj->type == OBJ_TREE) {
		struct tree *item = (struct tree *) obj;

		free_tree_buffer(item);
	}

	if (obj->type == OBJ_COMMIT) {
		struct commit *commit = (struct commit *) obj;

		free_commit_buffer(commit);

		if (!commit->parents && show_root)
			printf("root %s\n", describe_object(&commit->object));
	}

	if (obj->type == OBJ_TAG) {
		struct tag *tag = (struct tag *) obj;

		if (show_tags && tag->tagged) {
			printf("tagged %s %s", printable_type(tag->tagged),
				describe_object(tag->tagged));
			printf(" (%s) in %s\n", tag->tag,
				describe_object(&tag->object));
		}
	}

	return 0;
}

static int fsck_obj_buffer(const unsigned char *sha1, enum object_type type,
			   unsigned long size, void *buffer, int *eaten)
{
	/*
	 * Note, buffer may be NULL if type is OBJ_BLOB. See
	 * verify_packfile(), data_valid variable for details.
	 */
	struct object *obj;
	obj = parse_object_buffer(sha1, type, size, buffer, eaten);
	if (!obj) {
		errors_found |= ERROR_OBJECT;
		return error("%s: object corrupt or missing", sha1_to_hex(sha1));
	}
	obj->flags = HAS_OBJ;
	return fsck_obj(obj);
}

static int default_refs;

static void fsck_handle_reflog_oid(const char *refname, struct object_id *oid,
	unsigned long timestamp)
{
	struct object *obj;

	if (!is_null_oid(oid)) {
		obj = lookup_object(oid->hash);
		if (obj && (obj->flags & HAS_OBJ)) {
			if (timestamp && name_objects)
				add_decoration(fsck_walk_options.object_names,
					obj,
					xstrfmt("%s@{%ld}", refname, timestamp));
			obj->used = 1;
			mark_object_reachable(obj);
		} else {
			error("%s: invalid reflog entry %s", refname, oid_to_hex(oid));
			errors_found |= ERROR_REACHABLE;
		}
	}
}

static int fsck_handle_reflog_ent(struct object_id *ooid, struct object_id *noid,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	const char *refname = cb_data;

	if (verbose)
		fprintf(stderr, "Checking reflog %s->%s\n",
			oid_to_hex(ooid), oid_to_hex(noid));

	fsck_handle_reflog_oid(refname, ooid, 0);
	fsck_handle_reflog_oid(refname, noid, timestamp);
	return 0;
}

static int fsck_handle_reflog(const char *logname, const struct object_id *oid,
			      int flag, void *cb_data)
{
	for_each_reflog_ent(logname, fsck_handle_reflog_ent, (void *)logname);
	return 0;
}

static int fsck_handle_ref(const char *refname, const struct object_id *oid,
			   int flag, void *cb_data)
{
	struct object *obj;

	obj = parse_object(oid->hash);
	if (!obj) {
		error("%s: invalid sha1 pointer %s", refname, oid_to_hex(oid));
		errors_found |= ERROR_REACHABLE;
		/* We'll continue with the rest despite the error.. */
		return 0;
	}
	if (obj->type != OBJ_COMMIT && is_branch(refname)) {
		error("%s: not a commit", refname);
		errors_found |= ERROR_REFS;
	}
	default_refs++;
	obj->used = 1;
	if (name_objects)
		add_decoration(fsck_walk_options.object_names,
			obj, xstrdup(refname));
	mark_object_reachable(obj);

	return 0;
}

static void get_default_heads(void)
{
	if (head_points_at && !is_null_oid(&head_oid))
		fsck_handle_ref("HEAD", &head_oid, 0, NULL);
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

static struct object *parse_loose_object(const struct object_id *oid,
					 const char *path)
{
	struct object *obj;
	void *contents;
	enum object_type type;
	unsigned long size;
	int eaten;

	if (read_loose_object(path, oid->hash, &type, &size, &contents) < 0)
		return NULL;

	if (!contents && type != OBJ_BLOB)
		die("BUG: read_loose_object streamed a non-blob");

	obj = parse_object_buffer(oid->hash, type, size, contents, &eaten);

	if (!eaten)
		free(contents);
	return obj;
}

static int fsck_loose(const struct object_id *oid, const char *path, void *data)
{
	struct object *obj = parse_loose_object(oid, path);

	if (!obj) {
		errors_found |= ERROR_OBJECT;
		error("%s: object corrupt or missing: %s",
		      oid_to_hex(oid), path);
		return 0; /* keep checking other objects */
	}

	obj->flags = HAS_OBJ;
	if (fsck_obj(obj))
		errors_found |= ERROR_OBJECT;
	return 0;
}

static int fsck_cruft(const char *basename, const char *path, void *data)
{
	if (!starts_with(basename, "tmp_obj_"))
		fprintf(stderr, "bad sha1 file: %s\n", path);
	return 0;
}

static int fsck_subdir(int nr, const char *path, void *progress)
{
	display_progress(progress, nr + 1);
	return 0;
}

static void fsck_object_dir(const char *path)
{
	struct progress *progress = NULL;

	if (verbose)
		fprintf(stderr, "Checking object directory\n");

	if (show_progress)
		progress = start_progress(_("Checking object directories"), 256);

	for_each_loose_file_in_objdir(path, fsck_loose, fsck_cruft, fsck_subdir,
				      progress);
	display_progress(progress, 256);
	stop_progress(&progress);
}

static int fsck_head_link(void)
{
	int null_is_error = 0;

	if (verbose)
		fprintf(stderr, "Checking HEAD link\n");

	head_points_at = resolve_ref_unsafe("HEAD", 0, head_oid.hash, NULL);
	if (!head_points_at) {
		errors_found |= ERROR_REFS;
		return error("Invalid HEAD");
	}
	if (!strcmp(head_points_at, "HEAD"))
		/* detached HEAD */
		null_is_error = 1;
	else if (!starts_with(head_points_at, "refs/heads/")) {
		errors_found |= ERROR_REFS;
		return error("HEAD points to something strange (%s)",
			     head_points_at);
	}
	if (is_null_oid(&head_oid)) {
		if (null_is_error) {
			errors_found |= ERROR_REFS;
			return error("HEAD: detached HEAD points at nothing");
		}
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
			errors_found |= ERROR_REFS;
			return 1;
		}
		obj->used = 1;
		if (name_objects)
			add_decoration(fsck_walk_options.object_names,
				obj, xstrdup(":"));
		mark_object_reachable(obj);
		if (obj->type != OBJ_TREE)
			err |= objerror(obj, "non-tree in cache-tree");
	}
	for (i = 0; i < it->subtree_nr; i++)
		err |= fsck_cache_tree(it->down[i]->cache_tree);
	return err;
}

static void mark_object_for_connectivity(const struct object_id *oid)
{
	struct object *obj = lookup_unknown_object(oid->hash);
	obj->flags |= HAS_OBJ;
}

static int mark_loose_for_connectivity(const struct object_id *oid,
				       const char *path,
				       void *data)
{
	mark_object_for_connectivity(oid);
	return 0;
}

static int mark_packed_for_connectivity(const struct object_id *oid,
					struct packed_git *pack,
					uint32_t pos,
					void *data)
{
	mark_object_for_connectivity(oid);
	return 0;
}

static char const * const fsck_usage[] = {
	N_("git fsck [<options>] [<object>...]"),
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
	OPT_BOOL(0, "connectivity-only", &connectivity_only, N_("check only connectivity")),
	OPT_BOOL(0, "strict", &check_strict, N_("enable more strict checking")),
	OPT_BOOL(0, "lost-found", &write_lost_and_found,
				N_("write dangling objects in .git/lost-found")),
	OPT_BOOL(0, "progress", &show_progress, N_("show progress")),
	OPT_BOOL(0, "name-objects", &name_objects, N_("show verbose names for reachable objects")),
	OPT_END(),
};

int cmd_fsck(int argc, const char **argv, const char *prefix)
{
	int i, heads;
	struct alternate_object_database *alt;

	errors_found = 0;
	check_replace_refs = 0;

	argc = parse_options(argc, argv, prefix, fsck_opts, fsck_usage, 0);

	fsck_walk_options.walk = mark_object;
	fsck_obj_options.walk = mark_used;
	fsck_obj_options.error_func = fsck_error_func;
	if (check_strict)
		fsck_obj_options.strict = 1;

	if (show_progress == -1)
		show_progress = isatty(2);
	if (verbose)
		show_progress = 0;

	if (write_lost_and_found) {
		check_full = 1;
		include_reflogs = 0;
	}

	if (name_objects)
		fsck_walk_options.object_names =
			xcalloc(1, sizeof(struct decoration));

	git_config(fsck_config, NULL);

	fsck_head_link();
	if (connectivity_only) {
		for_each_loose_object(mark_loose_for_connectivity, NULL, 0);
		for_each_packed_object(mark_packed_for_connectivity, NULL, 0);
	} else {
		fsck_object_dir(get_object_directory());

		prepare_alt_odb();
		for (alt = alt_odb_list; alt; alt = alt->next)
			fsck_object_dir(alt->path);

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
	}

	heads = 0;
	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];
		unsigned char sha1[20];
		if (!get_sha1(arg, sha1)) {
			struct object *obj = lookup_object(sha1);

			if (!obj || !(obj->flags & HAS_OBJ)) {
				error("%s: object missing", sha1_to_hex(sha1));
				errors_found |= ERROR_OBJECT;
				continue;
			}

			obj->used = 1;
			if (name_objects)
				add_decoration(fsck_walk_options.object_names,
					obj, xstrdup(arg));
			mark_object_reachable(obj);
			heads++;
			continue;
		}
		error("invalid parameter: expected sha1, got '%s'", arg);
		errors_found |= ERROR_OBJECT;
	}

	/*
	 * If we've not been given any explicit head information, do the
	 * default ones from .git/refs. We also consider the index file
	 * in this case (ie this implies --cache).
	 */
	if (!argc) {
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
			blob = lookup_blob(active_cache[i]->oid.hash);
			if (!blob)
				continue;
			obj = &blob->object;
			obj->used = 1;
			if (name_objects)
				add_decoration(fsck_walk_options.object_names,
					obj,
					xstrfmt(":%s", active_cache[i]->name));
			mark_object_reachable(obj);
		}
		if (active_cache_tree)
			fsck_cache_tree(active_cache_tree);
	}

	check_connectivity();
	return errors_found;
}
