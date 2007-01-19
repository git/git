#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "builtin.h"
#include "reachable.h"

static const char prune_usage[] = "git-prune [-n] [--grace=time]";
static int show_only;
static int prune_grace_period;

static int prune_object(char *path, const char *filename, const unsigned char *sha1)
{
	char buf[20];
	const char *type;

	if (show_only) {
		if (sha1_object_info(sha1, buf, NULL))
			type = "unknown";
		else
			type = buf;
		printf("%s %s\n", sha1_to_hex(sha1), type);
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
		struct stat st;

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

			if (prune_grace_period > 0 &&
			    !stat(mkpath("%s/%s", path, de->d_name), &st) &&
			    st.st_mtime > prune_grace_period)
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

static int git_prune_config(const char *var, const char *value)
{
	if (!strcmp(var, "gc.prunegrace")) {
		if (!strcmp(value, "off"))
			prune_grace_period = 0;
		else
			prune_grace_period = approxidate(value);
		return 0;
	}
	return git_default_config(var, value);
}

int cmd_prune(int argc, const char **argv, const char *prefix)
{
	int i;
	struct rev_info revs;
	prune_grace_period = time(NULL)-24*60*60;

	git_config(git_prune_config);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "-n")) {
			show_only = 1;
			continue;
		}
		if (!strncmp(arg, "--grace=", 8)) {
			if (!strcmp(arg+8, "off"))
				prune_grace_period = 0;
			else
				prune_grace_period = approxidate(arg+8);
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
