#include "cache.h"
#include "refs.h"

static const char git_update_ref_usage[] = "git-update-ref <refname> <value> [<oldval>]";

static int re_verify(const char *path, unsigned char *oldsha1, unsigned char *currsha1)
{
	char buf[40];
	int fd = open(path, O_RDONLY), nr;
	if (fd < 0)
		return -1;
	nr = read(fd, buf, 40);
	close(fd);
	if (nr != 40 || get_sha1_hex(buf, currsha1) < 0)
		return -1;
	return memcmp(oldsha1, currsha1, 20) ? -1 : 0;
}

int main(int argc, char **argv)
{
	char *hex;
	const char *refname, *value, *oldval, *path;
	char *lockpath;
	unsigned char sha1[20], oldsha1[20], currsha1[20];
	int fd, written;

	setup_git_directory();
	git_config(git_default_config);
	if (argc < 3 || argc > 4)
		usage(git_update_ref_usage);

	refname = argv[1];
	value = argv[2];
	oldval = argv[3];
	if (get_sha1(value, sha1))
		die("%s: not a valid SHA1", value);
	memset(oldsha1, 0, 20);
	if (oldval && get_sha1(oldval, oldsha1))
		die("%s: not a valid old SHA1", oldval);

	path = resolve_ref(git_path("%s", refname), currsha1, !!oldval);
	if (!path)
		die("No such ref: %s", refname);

	if (oldval) {
		if (memcmp(currsha1, oldsha1, 20))
			die("Ref %s is at %s but expected %s", refname, sha1_to_hex(currsha1), sha1_to_hex(oldsha1));
		/* Nothing to do? */
		if (!memcmp(oldsha1, sha1, 20))
			exit(0);
	}
	path = strdup(path);
	lockpath = mkpath("%s.lock", path);
	if (safe_create_leading_directories(lockpath) < 0)
		die("Unable to create all of %s", lockpath);

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
	 * Re-read the ref after getting the lock to verify
	 */
	if (oldval && re_verify(path, oldsha1, currsha1) < 0) {
		unlink(lockpath);
		die("Ref lock failed");
	}

	/*
	 * Finally, replace the old ref with the new one
	 */
	if (rename(lockpath, path) < 0) {
		unlink(lockpath);
		die("Unable to create %s", path);
	}
	return 0;
}
