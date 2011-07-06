#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "builtin.h"
#include "reachable.h"
#include "parse-options.h"

static const char * const prune_usage[] = {
	"git prune [-n] [--expire <time>] [--] [<head>...]",
	NULL
};
static int show_only;
static unsigned long expire;

static int prune_tmp_object(char *path, const char *filename)
{
	const char *fullpath = mkpath("%s/%s", path, filename);
	if (expire) {
		struct stat st;
		if (lstat(fullpath, &st))
			return error("Could not stat '%s'", fullpath);
		if (st.st_mtime > expire)
			return 0;
	}
	printf("Removing stale temporary file %s\n", fullpath);
	if (!show_only)
		unlink(fullpath);
	return 0;
}

static int prune_object(char *path, const char *filename, const unsigned char *sha1)
{
	const char *fullpath = mkpath("%s/%s", path, filename);
	if (expire) {
		struct stat st;
		if (lstat(fullpath, &st))
			return error("Could not stat '%s'", fullpath);
		if (st.st_mtime > expire)
			return 0;
	}
	if (show_only) {
		enum object_type type = sha1_object_info(sha1, NULL);
		printf("%s %s\n", sha1_to_hex(sha1),
		       (type > 0) ? typename(type) : "unknown");
	} else
		unlink(fullpath);
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
		if (!prefixcmp(de->d_name, "tmp_obj_")) {
			prune_tmp_object(path, de->d_name);
			continue;
		}
		fprintf(stderr, "bad sha1 file: %s/%s\n", path, de->d_name);
	}
	if (!show_only)
		rmdir(path);
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

/*
 * Write errors (particularly out of space) can result in
 * failed temporary packs (and more rarely indexes and other
 * files begining with "tmp_") accumulating in the
 * object directory.
 */
static void remove_temporary_files(void)
{
	DIR *dir;
	struct dirent *de;
	char* dirname=get_object_directory();

	dir = opendir(dirname);
	if (!dir) {
		fprintf(stderr, "Unable to open object directory %s\n",
			dirname);
		return;
	}
	while ((de = readdir(dir)) != NULL)
		if (!prefixcmp(de->d_name, "tmp_"))
			prune_tmp_object(dirname, de->d_name);
	closedir(dir);
}

int cmd_prune(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	const struct option options[] = {
		OPT_BOOLEAN('n', NULL, &show_only,
			    "do not remove, show only"),
		OPT_DATE(0, "expire", &expire,
			 "expire objects older than <time>"),
		OPT_END()
	};

	save_commit_buffer = 0;
	init_revisions(&revs, prefix);

	argc = parse_options(argc, argv, options, prune_usage, 0);
	while (argc--) {
		unsigned char sha1[20];
		const char *name = *argv++;

		if (!get_sha1(name, sha1)) {
			struct object *object = parse_object(sha1);
			if (!object)
				die("bad object: %s", name);
			add_pending_object(&revs, object, "");
		}
		else
			die("unrecognized argument: %s", name);
	}
	mark_reachable_objects(&revs, 1);
	prune_object_dir(get_object_directory());

	prune_packed_objects(show_only);
	remove_temporary_files();
	return 0;
}
