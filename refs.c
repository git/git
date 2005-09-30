#include "refs.h"
#include "cache.h"

#include <errno.h>
#include <ctype.h>

/* We allow "recursive" symbolic refs. Only within reason, though */
#define MAXDEPTH 5

const char *resolve_ref(const char *path, unsigned char *sha1, int reading)
{
	int depth = MAXDEPTH, len;
	char buffer[256];

	for (;;) {
		struct stat st;
		char *buf;
		int fd;

		if (--depth < 0)
			return NULL;

		/* Special case: non-existing file.
		 * Not having the refs/heads/new-branch is OK
		 * if we are writing into it, so is .git/HEAD
		 * that points at refs/heads/master still to be
		 * born.  It is NOT OK if we are resolving for
		 * reading.
		 */
		if (lstat(path, &st) < 0) {
			if (reading || errno != ENOENT)
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

		/*
		 * Is it a symbolic ref?
		 */
		if (len < 4 || memcmp("ref:", buffer, 4))
			break;
		buf = buffer + 4;
		len -= 4;
		while (len && isspace(*buf))
			buf++, len--;
		while (len && isspace(buf[len-1]))
			buf[--len] = 0;
		path = git_path("%.*s", len, buf);
	}
	if (len < 40 || get_sha1_hex(buffer, sha1))
		return NULL;
	return path;
}

int read_ref(const char *filename, unsigned char *sha1)
{
	if (resolve_ref(filename, sha1, 1))
		return 0;
	return -1;
}

static int do_for_each_ref(const char *base, int (*fn)(const char *path, const unsigned char *sha1))
{
	int retval = 0;
	DIR *dir = opendir(git_path("%s", base));

	if (dir) {
		struct dirent *de;
		int baselen = strlen(base);
		char *path = xmalloc(baselen + 257);

		if (!strncmp(base, "./", 2)) {
			base += 2;
			baselen -= 2;
		}
		memcpy(path, base, baselen);
		if (baselen && base[baselen-1] != '/')
			path[baselen++] = '/';

		while ((de = readdir(dir)) != NULL) {
			unsigned char sha1[20];
			struct stat st;
			int namelen;

			if (de->d_name[0] == '.')
				continue;
			namelen = strlen(de->d_name);
			if (namelen > 255)
				continue;
			memcpy(path + baselen, de->d_name, namelen+1);
			if (stat(git_path("%s", path), &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode)) {
				retval = do_for_each_ref(path, fn);
				if (retval)
					break;
				continue;
			}
			if (read_ref(git_path("%s", path), sha1) < 0)
				continue;
			if (!has_sha1_file(sha1))
				continue;
			retval = fn(path, sha1);
			if (retval)
				break;
		}
		free(path);
		closedir(dir);
	}
	return retval;
}

int head_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	unsigned char sha1[20];
	if (!read_ref(git_path("HEAD"), sha1))
		return fn("HEAD", sha1);
	return 0;
}

int for_each_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	return do_for_each_ref("refs", fn);
}

static char *ref_file_name(const char *ref)
{
	char *base = get_refs_directory();
	int baselen = strlen(base);
	int reflen = strlen(ref);
	char *ret = xmalloc(baselen + 2 + reflen);
	sprintf(ret, "%s/%s", base, ref);
	return ret;
}

static char *ref_lock_file_name(const char *ref)
{
	char *base = get_refs_directory();
	int baselen = strlen(base);
	int reflen = strlen(ref);
	char *ret = xmalloc(baselen + 7 + reflen);
	sprintf(ret, "%s/%s.lock", base, ref);
	return ret;
}

int get_ref_sha1(const char *ref, unsigned char *sha1)
{
	const char *filename;

	if (check_ref_format(ref))
		return -1;
	filename = git_path("refs/%s", ref);
	return read_ref(filename, sha1);
}

static int lock_ref_file(const char *filename, const char *lock_filename,
			 const unsigned char *old_sha1)
{
	int fd = open(lock_filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
	unsigned char current_sha1[20];
	int retval;
	if (fd < 0) {
		return error("Couldn't open lock file for %s: %s",
			     filename, strerror(errno));
	}
	retval = read_ref(filename, current_sha1);
	if (old_sha1) {
		if (retval) {
			close(fd);
			unlink(lock_filename);
			return error("Could not read the current value of %s",
				     filename);
		}
		if (memcmp(current_sha1, old_sha1, 20)) {
			close(fd);
			unlink(lock_filename);
			error("The current value of %s is %s",
			      filename, sha1_to_hex(current_sha1));
			return error("Expected %s",
				     sha1_to_hex(old_sha1));
		}
	} else {
		if (!retval) {
			close(fd);
			unlink(lock_filename);
			return error("Unexpectedly found a value of %s for %s",
				     sha1_to_hex(current_sha1), filename);
		}
	}
	return fd;
}

int lock_ref_sha1(const char *ref, const unsigned char *old_sha1)
{
	char *filename;
	char *lock_filename;
	int retval;
	if (check_ref_format(ref))
		return -1;
	filename = ref_file_name(ref);
	lock_filename = ref_lock_file_name(ref);
	retval = lock_ref_file(filename, lock_filename, old_sha1);
	free(filename);
	free(lock_filename);
	return retval;
}

static int write_ref_file(const char *filename,
			  const char *lock_filename, int fd,
			  const unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	char term = '\n';
	if (write(fd, hex, 40) < 40 ||
	    write(fd, &term, 1) < 1) {
		error("Couldn't write %s\n", filename);
		close(fd);
		return -1;
	}
	close(fd);
	rename(lock_filename, filename);
	return 0;
}

int write_ref_sha1(const char *ref, int fd, const unsigned char *sha1)
{
	char *filename;
	char *lock_filename;
	int retval;
	if (fd < 0)
		return -1;
	if (check_ref_format(ref))
		return -1;
	filename = ref_file_name(ref);
	lock_filename = ref_lock_file_name(ref);
	retval = write_ref_file(filename, lock_filename, fd, sha1);
	free(filename);
	free(lock_filename);
	return retval;
}

int check_ref_format(const char *ref)
{
	char *middle;
	if (ref[0] == '.' || ref[0] == '/')
		return -1;
	middle = strchr(ref, '/');
	if (!middle || !middle[1])
		return -1;
	if (strchr(middle + 1, '/'))
		return -1;
	return 0;
}

int write_ref_sha1_unlocked(const char *ref, const unsigned char *sha1)
{
	char *filename;
	char *lock_filename;
	int fd;
	int retval;
	if (check_ref_format(ref))
		return -1;
	filename = ref_file_name(ref);
	lock_filename = ref_lock_file_name(ref);
	fd = open(lock_filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0) {
		error("Writing %s", lock_filename);
		perror("Open");
	}
	retval = write_ref_file(filename, lock_filename, fd, sha1);
	free(filename);
	free(lock_filename);
	return retval;
}
