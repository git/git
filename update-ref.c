#include "cache.h"
#include "refs.h"

static const char git_update_ref_usage[] = "git-update-ref <refname> <value> [<oldval>]";

#define MAXDEPTH 5

const char *resolve_ref(const char *path, unsigned char *sha1)
{
	int depth = MAXDEPTH, len;
	char buffer[256];

	for (;;) {
		struct stat st;
		int fd;

		if (--depth < 0)
			return NULL;

		/* Special case: non-existing file */
		if (lstat(path, &st) < 0) {
			if (errno != ENOENT)
				return NULL;
			memset(sha1, 0, 20);
			return path;
		}

		/* Follow "normalized" - ie "refs/.." symlinks by hand */
		if (S_ISLNK(st.st_mode)) {
			len = readlink(path, buffer, sizeof(buffer)-1);
			if (len >= 5 && !memcmp("refs/", buffer, 5)) {
				path = git_path("%.*s", len, buffer);
				continue;
			}
		}

		/*
		 * Anything else, just open it and try to use it as
		 * a ref
		 */
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return NULL;
		len = read(fd, buffer, sizeof(buffer)-1);
		close(fd);
		break;
	}
	if (len < 40 || get_sha1_hex(buffer, sha1))
		return NULL;
	return path;
}

int main(int argc, char **argv)
{
	char *hex;
	const char *refname, *value, *oldval, *path, *lockpath;
	unsigned char sha1[20], oldsha1[20], currsha1[20];
	int fd, written;

	setup_git_directory();
	if (argc < 3 || argc > 4)
		usage(git_update_ref_usage);

	refname = argv[1];
	value = argv[2];
	oldval = argv[3];
	if (get_sha1(value, sha1) < 0)
		die("%s: not a valid SHA1", value);
	memset(oldsha1, 0, 20);
	if (oldval && get_sha1(oldval, oldsha1) < 0)
		die("%s: not a valid old SHA1", oldval);

	path = resolve_ref(git_path("%s", refname), currsha1);
	if (!path)
		die("No such ref: %s", refname);

	if (oldval) {
		if (memcmp(currsha1, oldsha1, 20))
			die("Ref %s changed to %s", refname, sha1_to_hex(currsha1));
		/* Nothing to do? */
		if (!memcmp(oldsha1, sha1, 20))
			exit(0);
	}
	path = strdup(path);
	lockpath = mkpath("%s.lock", path);

	fd = open(lockpath, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (fd < 0)
		die("Unable to create %s", lockpath);
	hex = sha1_to_hex(sha1);
	hex[40] = '\n';
	written = write(fd, hex, 41);
	close(fd);
	if (written != 41) {
		unlink(lockpath);
		die("Unable to write to %s", lockpath);
	}

	/*
	 * FIXME!
	 *
	 * We should re-read the old ref here, and re-verify that it
	 * matches "oldsha1". Otherwise there's a small race.
	 */

	if (rename(lockpath, path) < 0) {
		unlink(lockpath);
		die("Unable to create %s", path);
	}
	return 0;
}
