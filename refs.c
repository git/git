#include "refs.h"
#include "cache.h"

#include <errno.h>

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

int create_symref(const char *git_HEAD, const char *refs_heads_master)
{
	const char *lockpath;
	char ref[1000];
	int fd, len, written;

#ifndef NO_SYMLINK_HEAD
	if (prefer_symlink_refs) {
		unlink(git_HEAD);
		if (!symlink(refs_heads_master, git_HEAD))
			return 0;
		fprintf(stderr, "no symlink - falling back to symbolic ref\n");
	}
#endif

	len = snprintf(ref, sizeof(ref), "ref: %s\n", refs_heads_master);
	if (sizeof(ref) <= len) {
		error("refname too long: %s", refs_heads_master);
		return -1;
	}
	lockpath = mkpath("%s.lock", git_HEAD);
	fd = open(lockpath, O_CREAT | O_EXCL | O_WRONLY, 0666);	
	written = write(fd, ref, len);
	close(fd);
	if (written != len) {
		unlink(lockpath);
		error("Unable to write to %s", lockpath);
		return -2;
	}
	if (rename(lockpath, git_HEAD) < 0) {
		unlink(lockpath);
		error("Unable to create %s", git_HEAD);
		return -3;
	}
	return 0;
}

int read_ref(const char *filename, unsigned char *sha1)
{
	if (resolve_ref(filename, sha1, 1))
		return 0;
	return -1;
}

static int do_for_each_ref(const char *base, int (*fn)(const char *path, const unsigned char *sha1), int trim)
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
				retval = do_for_each_ref(path, fn, trim);
				if (retval)
					break;
				continue;
			}
			if (read_ref(git_path("%s", path), sha1) < 0) {
				error("%s points nowhere!", path);
				continue;
			}
			if (!has_sha1_file(sha1)) {
				error("%s does not point to a valid "
				      "commit object!", path);
				continue;
			}
			retval = fn(path + trim, sha1);
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
	return do_for_each_ref("refs", fn, 0);
}

int for_each_tag_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	return do_for_each_ref("refs/tags", fn, 10);
}

int for_each_branch_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	return do_for_each_ref("refs/heads", fn, 11);
}

int for_each_remote_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	return do_for_each_ref("refs/remotes", fn, 13);
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
	if (check_ref_format(ref))
		return -1;
	return read_ref(git_path("refs/%s", ref), sha1);
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
		error("Couldn't write %s", filename);
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
	if (safe_create_leading_directories(filename))
		die("unable to create leading directory for %s", filename);
	retval = write_ref_file(filename, lock_filename, fd, sha1);
	free(filename);
	free(lock_filename);
	return retval;
}

/*
 * Make sure "ref" is something reasonable to have under ".git/refs/";
 * We do not like it if:
 *
 * - any path component of it begins with ".", or
 * - it has double dots "..", or
 * - it has ASCII control character, "~", "^", ":" or SP, anywhere, or
 * - it ends with a "/".
 */

static inline int bad_ref_char(int ch)
{
	return (((unsigned) ch) <= ' ' ||
		ch == '~' || ch == '^' || ch == ':' ||
		/* 2.13 Pattern Matching Notation */
		ch == '?' || ch == '*' || ch == '[');
}

int check_ref_format(const char *ref)
{
	int ch, level;
	const char *cp = ref;

	level = 0;
	while (1) {
		while ((ch = *cp++) == '/')
			; /* tolerate duplicated slashes */
		if (!ch)
			return -1; /* should not end with slashes */

		/* we are at the beginning of the path component */
		if (ch == '.' || bad_ref_char(ch))
			return -1;

		/* scan the rest of the path component */
		while ((ch = *cp++) != 0) {
			if (bad_ref_char(ch))
				return -1;
			if (ch == '/')
				break;
			if (ch == '.' && *cp == '.')
				return -1;
		}
		level++;
		if (!ch) {
			if (level < 2)
				return -1; /* at least of form "heads/blah" */
			return 0;
		}
	}
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
	if (safe_create_leading_directories(filename))
		die("unable to create leading directory for %s", filename);
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
