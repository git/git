/*
 * Various trivial helper wrappers around standard functions
 */
#include "cache.h"

static void try_to_free_builtin(size_t size)
{
	release_pack_memory(size, -1);
}

static void (*try_to_free_routine)(size_t size) = try_to_free_builtin;

try_to_free_t set_try_to_free_routine(try_to_free_t routine)
{
	try_to_free_t old = try_to_free_routine;
	try_to_free_routine = routine;
	return old;
}

char *xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret) {
		try_to_free_routine(strlen(str) + 1);
		ret = strdup(str);
		if (!ret)
			die("Out of memory, strdup failed");
	}
	return ret;
}

void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (!ret && !size)
		ret = malloc(1);
	if (!ret) {
		try_to_free_routine(size);
		ret = malloc(size);
		if (!ret && !size)
			ret = malloc(1);
		if (!ret)
			die("Out of memory, malloc failed (tried to allocate %lu bytes)",
			    (unsigned long)size);
	}
#ifdef XMALLOC_POISON
	memset(ret, 0xA5, size);
#endif
	return ret;
}

void *xmallocz(size_t size)
{
	void *ret;
	if (size + 1 < size)
		die("Data too large to fit into virtual memory space.");
	ret = xmalloc(size + 1);
	((char*)ret)[size] = 0;
	return ret;
}

/*
 * xmemdupz() allocates (len + 1) bytes of memory, duplicates "len" bytes of
 * "data" to the allocated memory, zero terminates the allocated memory,
 * and returns a pointer to the allocated memory. If the allocation fails,
 * the program dies.
 */
void *xmemdupz(const void *data, size_t len)
{
	return memcpy(xmallocz(len), data, len);
}

char *xstrndup(const char *str, size_t len)
{
	char *p = memchr(str, '\0', len);
	return xmemdupz(str, p ? p - str : len);
}

void *xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret && !size)
		ret = realloc(ptr, 1);
	if (!ret) {
		try_to_free_routine(size);
		ret = realloc(ptr, size);
		if (!ret && !size)
			ret = realloc(ptr, 1);
		if (!ret)
			die("Out of memory, realloc failed");
	}
	return ret;
}

void *xcalloc(size_t nmemb, size_t size)
{
	void *ret = calloc(nmemb, size);
	if (!ret && (!nmemb || !size))
		ret = calloc(1, 1);
	if (!ret) {
		try_to_free_routine(nmemb * size);
		ret = calloc(nmemb, size);
		if (!ret && (!nmemb || !size))
			ret = calloc(1, 1);
		if (!ret)
			die("Out of memory, calloc failed");
	}
	return ret;
}

void *xmmap(void *start, size_t length,
	int prot, int flags, int fd, off_t offset)
{
	void *ret = mmap(start, length, prot, flags, fd, offset);
	if (ret == MAP_FAILED) {
		if (!length)
			return NULL;
		release_pack_memory(length, fd);
		ret = mmap(start, length, prot, flags, fd, offset);
		if (ret == MAP_FAILED)
			die_errno("Out of memory? mmap failed");
	}
	return ret;
}

/*
 * xread() is the same a read(), but it automatically restarts read()
 * operations with a recoverable error (EAGAIN and EINTR). xread()
 * DOES NOT GUARANTEE that "len" bytes is read even if the data is available.
 */
ssize_t xread(int fd, void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = read(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

/*
 * xwrite() is the same a write(), but it automatically restarts write()
 * operations with a recoverable error (EAGAIN and EINTR). xwrite() DOES NOT
 * GUARANTEE that "len" bytes is written even if the operation is successful.
 */
ssize_t xwrite(int fd, const void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = write(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

ssize_t read_in_full(int fd, void *buf, size_t count)
{
	char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t loaded = xread(fd, p, count);
		if (loaded <= 0)
			return total ? total : loaded;
		count -= loaded;
		p += loaded;
		total += loaded;
	}

	return total;
}

ssize_t write_in_full(int fd, const void *buf, size_t count)
{
	const char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t written = xwrite(fd, p, count);
		if (written < 0)
			return -1;
		if (!written) {
			errno = ENOSPC;
			return -1;
		}
		count -= written;
		p += written;
		total += written;
	}

	return total;
}

int xdup(int fd)
{
	int ret = dup(fd);
	if (ret < 0)
		die_errno("dup failed");
	return ret;
}

FILE *xfdopen(int fd, const char *mode)
{
	FILE *stream = fdopen(fd, mode);
	if (stream == NULL)
		die_errno("Out of memory? fdopen failed");
	return stream;
}

int xmkstemp(char *template)
{
	int fd;

	fd = mkstemp(template);
	if (fd < 0)
		die_errno("Unable to create temporary file");
	return fd;
}

int xmkstemp_mode(char *template, int mode)
{
	int fd;

	fd = git_mkstemp_mode(template, mode);
	if (fd < 0)
		die_errno("Unable to create temporary file");
	return fd;
}

/*
 * zlib wrappers to make sure we don't silently miss errors
 * at init time.
 */
void git_inflate_init(z_streamp strm)
{
	const char *err;

	switch (inflateInit(strm)) {
	case Z_OK:
		return;

	case Z_MEM_ERROR:
		err = "out of memory";
		break;
	case Z_VERSION_ERROR:
		err = "wrong version";
		break;
	default:
		err = "error";
	}
	die("inflateInit: %s (%s)", err, strm->msg ? strm->msg : "no message");
}

void git_inflate_end(z_streamp strm)
{
	if (inflateEnd(strm) != Z_OK)
		error("inflateEnd: %s", strm->msg ? strm->msg : "failed");
}

int git_inflate(z_streamp strm, int flush)
{
	int ret = inflate(strm, flush);
	const char *err;

	switch (ret) {
	/* Out of memory is fatal. */
	case Z_MEM_ERROR:
		die("inflate: out of memory");

	/* Data corruption errors: we may want to recover from them (fsck) */
	case Z_NEED_DICT:
		err = "needs dictionary"; break;
	case Z_DATA_ERROR:
		err = "data stream error"; break;
	case Z_STREAM_ERROR:
		err = "stream consistency error"; break;
	default:
		err = "unknown error"; break;

	/* Z_BUF_ERROR: normal, needs more space in the output buffer */
	case Z_BUF_ERROR:
	case Z_OK:
	case Z_STREAM_END:
		return ret;
	}
	error("inflate: %s (%s)", err, strm->msg ? strm->msg : "no message");
	return ret;
}

int odb_mkstemp(char *template, size_t limit, const char *pattern)
{
	int fd;
	/*
	 * we let the umask do its job, don't try to be more
	 * restrictive except to remove write permission.
	 */
	int mode = 0444;
	snprintf(template, limit, "%s/%s",
		 get_object_directory(), pattern);
	fd = git_mkstemp_mode(template, mode);
	if (0 <= fd)
		return fd;

	/* slow path */
	/* some mkstemp implementations erase template on failure */
	snprintf(template, limit, "%s/%s",
		 get_object_directory(), pattern);
	safe_create_leading_directories(template);
	return xmkstemp_mode(template, mode);
}

int odb_pack_keep(char *name, size_t namesz, unsigned char *sha1)
{
	int fd;

	snprintf(name, namesz, "%s/pack/pack-%s.keep",
		 get_object_directory(), sha1_to_hex(sha1));
	fd = open(name, O_RDWR|O_CREAT|O_EXCL, 0600);
	if (0 <= fd)
		return fd;

	/* slow path */
	safe_create_leading_directories(name);
	return open(name, O_RDWR|O_CREAT|O_EXCL, 0600);
}

static int warn_if_unremovable(const char *op, const char *file, int rc)
{
	if (rc < 0) {
		int err = errno;
		if (ENOENT != err) {
			warning("unable to %s %s: %s",
				op, file, strerror(errno));
			errno = err;
		}
	}
	return rc;
}

int unlink_or_warn(const char *file)
{
	return warn_if_unremovable("unlink", file, unlink(file));
}

int rmdir_or_warn(const char *file)
{
	return warn_if_unremovable("rmdir", file, rmdir(file));
}

int remove_or_warn(unsigned int mode, const char *file)
{
	return S_ISGITLINK(mode) ? rmdir_or_warn(file) : unlink_or_warn(file);
}
