#include "refs.h"
#include "cache.h"

#include <errno.h>

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

static int read_ref_file(const char *filename, unsigned char *sha1) {
	int fd = open(filename, O_RDONLY);
	char hex[41];
	if (fd < 0) {
		return error("Couldn't open %s\n", filename);
	}
	if ((read(fd, hex, 41) < 41) ||
	    (hex[40] != '\n') ||
	    get_sha1_hex(hex, sha1)) {
		error("Couldn't read a hash from %s\n", filename);
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int get_ref_sha1(const char *ref, unsigned char *sha1)
{
	char *filename;
	int retval;
	if (check_ref_format(ref))
		return -1;
	filename = ref_file_name(ref);
	retval = read_ref_file(filename, sha1);
	free(filename);
	return retval;
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
	retval = read_ref_file(filename, current_sha1);
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
