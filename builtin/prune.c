#include "builtin.h"
#include "commit.h"
#include "diff.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "revision.h"
#include "reachable.h"
#include "parse-options.h"
#include "path.h"
#include "progress.h"
#include "prune-packed.h"
#include "replace-object.h"
#include "object-file.h"
#include "object-name.h"
#include "object-store-ll.h"
#include "shallow.h"

static const char * const prune_usage[] = {
	N_("git prune [-n] [-v] [--progress] [--expire <time>] [--] [<head>...]"),
	NULL
};
static int show_only;
static int verbose;
static timestamp_t expire;
static int show_progress = -1;

static int prune_tmp_file(const char *fullpath)
{
	struct stat st;
	if (lstat(fullpath, &st))
		return error("Could not stat '%s'", fullpath);
	if (st.st_mtime > expire)
		return 0;
	if (S_ISDIR(st.st_mode)) {
		if (show_only || verbose)
			printf("Removing stale temporary directory %s\n", fullpath);
		if (!show_only) {
			struct strbuf remove_dir_buf = STRBUF_INIT;

			strbuf_addstr(&remove_dir_buf, fullpath);
			remove_dir_recursively(&remove_dir_buf, 0);
			strbuf_release(&remove_dir_buf);
		}
	} else {
		if (show_only || verbose)
			printf("Removing stale temporary file %s\n", fullpath);
		if (!show_only)
			unlink_or_warn(fullpath);
	}
	return 0;
}

static void perform_reachability_traversal(struct rev_info *revs)
{
	static int initialized;
	struct progress *progress = NULL;

	if (initialized)
		return;

	if (show_progress)
		progress = start_delayed_progress(_("Checking connectivity"), 0);
	mark_reachable_objects(revs, 1, expire, progress);
	stop_progress(&progress);
	initialized = 1;
}

static int is_object_reachable(const struct object_id *oid,
			       struct rev_info *revs)
{
	struct object *obj;

	perform_reachability_traversal(revs);

	obj = lookup_object(the_repository, oid);
	return obj && (obj->flags & SEEN);
}

static int prune_object(const struct object_id *oid, const char *fullpath,
			void *data)
{
	struct rev_info *revs = data;
	struct stat st;

	if (is_object_reachable(oid, revs))
		return 0;

	if (lstat(fullpath, &st)) {
		/* report errors, but do not stop pruning */
		error("Could not stat '%s'", fullpath);
		return 0;
	}
	if (st.st_mtime > expire)
		return 0;
	if (show_only || verbose) {
		enum object_type type = oid_object_info(the_repository, oid,
							NULL);
		printf("%s %s\n", oid_to_hex(oid),
		       (type > 0) ? type_name(type) : "unknown");
	}
	if (!show_only)
		unlink_or_warn(fullpath);
	return 0;
}

static int prune_cruft(const char *basename, const char *path,
		       void *data UNUSED)
{
	if (starts_with(basename, "tmp_obj_"))
		prune_tmp_file(path);
	else
		fprintf(stderr, "bad sha1 file: %s\n", path);
	return 0;
}

static int prune_subdir(unsigned int nr UNUSED, const char *path,
			void *data UNUSED)
{
	if (!show_only)
		rmdir(path);
	return 0;
}

/*
 * Write errors (particularly out of space) can result in
 * failed temporary packs (and more rarely indexes and other
 * files beginning with "tmp_") accumulating in the object
 * and the pack directories.
 */
static void remove_temporary_files(const char *path)
{
	DIR *dir;
	struct dirent *de;

	dir = opendir(path);
	if (!dir) {
		if (errno != ENOENT)
			fprintf(stderr, "Unable to open directory %s: %s\n",
				path, strerror(errno));
		return;
	}
	while ((de = readdir(dir)) != NULL)
		if (starts_with(de->d_name, "tmp_"))
			prune_tmp_file(mkpath("%s/%s", path, de->d_name));
	closedir(dir);
}

int cmd_prune(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	int exclude_promisor_objects = 0;
	const struct option options[] = {
		OPT__DRY_RUN(&show_only, N_("do not remove, show only")),
		OPT__VERBOSE(&verbose, N_("report pruned objects")),
		OPT_BOOL(0, "progress", &show_progress, N_("show progress")),
		OPT_EXPIRY_DATE(0, "expire", &expire,
				N_("expire objects older than <time>")),
		OPT_BOOL(0, "exclude-promisor-objects", &exclude_promisor_objects,
			 N_("limit traversal to objects outside promisor packfiles")),
		OPT_END()
	};
	char *s;

	expire = TIME_MAX;
	save_commit_buffer = 0;
	disable_replace_refs();
	repo_init_revisions(the_repository, &revs, prefix);

	argc = parse_options(argc, argv, prefix, options, prune_usage, 0);

	if (repository_format_precious_objects)
		die(_("cannot prune in a precious-objects repo"));

	while (argc--) {
		struct object_id oid;
		const char *name = *argv++;

		if (!repo_get_oid(the_repository, name, &oid)) {
			struct object *object = parse_object_or_die(&oid,
								    name);
			add_pending_object(&revs, object, "");
		}
		else
			die("unrecognized argument: %s", name);
	}

	if (show_progress == -1)
		show_progress = isatty(2);
	if (exclude_promisor_objects) {
		fetch_if_missing = 0;
		revs.exclude_promisor_objects = 1;
	}

	for_each_loose_file_in_objdir(get_object_directory(), prune_object,
				      prune_cruft, prune_subdir, &revs);

	prune_packed_objects(show_only ? PRUNE_PACKED_DRY_RUN : 0);
	remove_temporary_files(get_object_directory());
	s = mkpathdup("%s/pack", get_object_directory());
	remove_temporary_files(s);
	free(s);

	if (is_repository_shallow(the_repository)) {
		perform_reachability_traversal(&revs);
		prune_shallow(show_only ? PRUNE_SHOW_ONLY : 0);
	}

	release_revisions(&revs);
	return 0;
}
