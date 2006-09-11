#include "cache.h"
#include "refs.h"

static FILE *refs_file;
static const char *result_path, *lock_path;

static void remove_lock_file(void)
{
	if (lock_path)
		unlink(lock_path);
}

static int handle_one_ref(const char *path, const unsigned char *sha1)
{
	fprintf(refs_file, "%s %s\n", sha1_to_hex(sha1), path);
	return 0;
}

int cmd_pack_refs(int argc, const char **argv, const char *prefix)
{
	int fd;

	result_path = xstrdup(git_path("packed-refs"));
	lock_path = xstrdup(mkpath("%s.lock", result_path));

	fd = open(lock_path, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (fd < 0)
		die("unable to create new ref-pack file (%s)", strerror(errno));
	atexit(remove_lock_file);

	refs_file = fdopen(fd, "w");
	if (!refs_file)
		die("unable to create ref-pack file structure (%s)", strerror(errno));
	for_each_ref(handle_one_ref);
	fsync(fd);
	fclose(refs_file);
	if (rename(lock_path, result_path) < 0)
		die("unable to overwrite old ref-pack file (%s)", strerror(errno));
	lock_path = NULL;
	return 0;
}
