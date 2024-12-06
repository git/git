/*
 * Various trivial helper wrappers around standard functions
 */

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "abspath.h"
#include "parse.h"
#include "gettext.h"
#include "strbuf.h"
#include "trace2.h"

#ifdef HAVE_RTLGENRANDOM
/* This is required to get access to RtlGenRandom. */
#define SystemFunction036 NTAPI SystemFunction036
#include <ntsecapi.h>
#undef SystemFunction036
#endif

static int memory_limit_check(size_t size, int gentle)
{
	static size_t limit = 0;
	if (!limit) {
		limit = git_env_ulong("GIT_ALLOC_LIMIT", 0);
		if (!limit)
			limit = SIZE_MAX;
	}
	if (size > limit) {
		if (gentle) {
			error("attempting to allocate %"PRIuMAX" over limit %"PRIuMAX,
			      (uintmax_t)size, (uintmax_t)limit);
			return -1;
		} else
			die("attempting to allocate %"PRIuMAX" over limit %"PRIuMAX,
			    (uintmax_t)size, (uintmax_t)limit);
	}
	return 0;
}

char *xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret)
		die("Out of memory, strdup failed");
	return ret;
}

static void *do_xmalloc(size_t size, int gentle)
{
	void *ret;

	if (memory_limit_check(size, gentle))
		return NULL;
	ret = malloc(size);
	if (!ret && !size)
		ret = malloc(1);
	if (!ret) {
		if (!gentle)
			die("Out of memory, malloc failed (tried to allocate %lu bytes)",
			    (unsigned long)size);
		else {
			error("Out of memory, malloc failed (tried to allocate %lu bytes)",
			      (unsigned long)size);
			return NULL;
		}
	}
#ifdef XMALLOC_POISON
	memset(ret, 0xA5, size);
#endif
	return ret;
}

void *xmalloc(size_t size)
{
	return do_xmalloc(size, 0);
}

static void *do_xmallocz(size_t size, int gentle)
{
	void *ret;
	if (unsigned_add_overflows(size, 1)) {
		if (gentle) {
			error("Data too large to fit into virtual memory space.");
			return NULL;
		} else
			die("Data too large to fit into virtual memory space.");
	}
	ret = do_xmalloc(size + 1, gentle);
	if (ret)
		((char*)ret)[size] = 0;
	return ret;
}

void *xmallocz(size_t size)
{
	return do_xmallocz(size, 0);
}

void *xmallocz_gently(size_t size)
{
	return do_xmallocz(size, 1);
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

int xstrncmpz(const char *s, const char *t, size_t len)
{
	int res = strncmp(s, t, len);
	if (res)
		return res;
	return s[len] == '\0' ? 0 : 1;
}

void *xrealloc(void *ptr, size_t size)
{
	void *ret;

	if (!size) {
		free(ptr);
		return xmalloc(0);
	}

	memory_limit_check(size, 0);
	ret = realloc(ptr, size);
	if (!ret)
		die("Out of memory, realloc failed");
	return ret;
}

void *xcalloc(size_t nmemb, size_t size)
{
	void *ret;

	if (unsigned_mult_overflows(nmemb, size))
		die("data too large to fit into virtual memory space");

	memory_limit_check(size * nmemb, 0);
	ret = calloc(nmemb, size);
	if (!ret && (!nmemb || !size))
		ret = calloc(1, 1);
	if (!ret)
		die("Out of memory, calloc failed");
	return ret;
}

void xsetenv(const char *name, const char *value, int overwrite)
{
	if (setenv(name, value, overwrite))
		die_errno(_("could not setenv '%s'"), name ? name : "(null)");
}

/**
 * xopen() is the same as open(), but it die()s if the open() fails.
 */
int xopen(const char *path, int oflag, ...)
{
	mode_t mode = 0;
	va_list ap;

	/*
	 * va_arg() will have undefined behavior if the specified type is not
	 * compatible with the argument type. Since integers are promoted to
	 * ints, we fetch the next argument as an int, and then cast it to a
	 * mode_t to avoid undefined behavior.
	 */
	va_start(ap, oflag);
	if (oflag & O_CREAT)
		mode = va_arg(ap, int);
	va_end(ap);

	for (;;) {
		int fd = open(path, oflag, mode);
		if (fd >= 0)
			return fd;
		if (errno == EINTR)
			continue;

		if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
			die_errno(_("unable to create '%s'"), path);
		else if ((oflag & O_RDWR) == O_RDWR)
			die_errno(_("could not open '%s' for reading and writing"), path);
		else if ((oflag & O_WRONLY) == O_WRONLY)
			die_errno(_("could not open '%s' for writing"), path);
		else
			die_errno(_("could not open '%s' for reading"), path);
	}
}

static int handle_nonblock(int fd, short poll_events, int err)
{
	struct pollfd pfd;

	if (err != EAGAIN && err != EWOULDBLOCK)
		return 0;

	pfd.fd = fd;
	pfd.events = poll_events;

	/*
	 * no need to check for errors, here;
	 * a subsequent read/write will detect unrecoverable errors
	 */
	poll(&pfd, 1, -1);
	return 1;
}

/*
 * xread() is the same a read(), but it automatically restarts read()
 * operations with a recoverable error (EAGAIN and EINTR). xread()
 * DOES NOT GUARANTEE that "len" bytes is read even if the data is available.
 */
ssize_t xread(int fd, void *buf, size_t len)
{
	ssize_t nr;
	if (len > MAX_IO_SIZE)
		len = MAX_IO_SIZE;
	while (1) {
		nr = read(fd, buf, len);
		if (nr < 0) {
			if (errno == EINTR)
				continue;
			if (handle_nonblock(fd, POLLIN, errno))
				continue;
		}
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
	if (len > MAX_IO_SIZE)
		len = MAX_IO_SIZE;
	while (1) {
		nr = write(fd, buf, len);
		if (nr < 0) {
			if (errno == EINTR)
				continue;
			if (handle_nonblock(fd, POLLOUT, errno))
				continue;
		}

		return nr;
	}
}

/*
 * xpread() is the same as pread(), but it automatically restarts pread()
 * operations with a recoverable error (EAGAIN and EINTR). xpread() DOES
 * NOT GUARANTEE that "len" bytes is read even if the data is available.
 */
ssize_t xpread(int fd, void *buf, size_t len, off_t offset)
{
	ssize_t nr;
	if (len > MAX_IO_SIZE)
		len = MAX_IO_SIZE;
	while (1) {
		nr = pread(fd, buf, len, offset);
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
		if (loaded < 0)
			return -1;
		if (loaded == 0)
			return total;
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

ssize_t pread_in_full(int fd, void *buf, size_t count, off_t offset)
{
	char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t loaded = xpread(fd, p, count, offset);
		if (loaded < 0)
			return -1;
		if (loaded == 0)
			return total;
		count -= loaded;
		p += loaded;
		total += loaded;
		offset += loaded;
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

/**
 * xfopen() is the same as fopen(), but it die()s if the fopen() fails.
 */
FILE *xfopen(const char *path, const char *mode)
{
	for (;;) {
		FILE *fp = fopen(path, mode);
		if (fp)
			return fp;
		if (errno == EINTR)
			continue;

		if (*mode && mode[1] == '+')
			die_errno(_("could not open '%s' for reading and writing"), path);
		else if (*mode == 'w' || *mode == 'a')
			die_errno(_("could not open '%s' for writing"), path);
		else
			die_errno(_("could not open '%s' for reading"), path);
	}
}

FILE *xfdopen(int fd, const char *mode)
{
	FILE *stream = fdopen(fd, mode);
	if (!stream)
		die_errno("Out of memory? fdopen failed");
	return stream;
}

FILE *fopen_for_writing(const char *path)
{
	FILE *ret = fopen(path, "w");

	if (!ret && errno == EPERM) {
		if (!unlink(path))
			ret = fopen(path, "w");
		else
			errno = EPERM;
	}
	return ret;
}

static void warn_on_inaccessible(const char *path)
{
	warning_errno(_("unable to access '%s'"), path);
}

int warn_on_fopen_errors(const char *path)
{
	if (errno != ENOENT && errno != ENOTDIR) {
		warn_on_inaccessible(path);
		return -1;
	}

	return 0;
}

FILE *fopen_or_warn(const char *path, const char *mode)
{
	FILE *fp = fopen(path, mode);

	if (fp)
		return fp;

	warn_on_fopen_errors(path);
	return NULL;
}

int xmkstemp(char *filename_template)
{
	int fd;
	char origtemplate[PATH_MAX];
	strlcpy(origtemplate, filename_template, sizeof(origtemplate));

	fd = mkstemp(filename_template);
	if (fd < 0) {
		int saved_errno = errno;
		const char *nonrelative_template;

		if (strlen(filename_template) != strlen(origtemplate))
			filename_template = origtemplate;

		nonrelative_template = absolute_path(filename_template);
		errno = saved_errno;
		die_errno("Unable to create temporary file '%s'",
			nonrelative_template);
	}
	return fd;
}

/* Adapted from libiberty's mkstemp.c. */

#undef TMP_MAX
#define TMP_MAX 16384

int git_mkstemps_mode(char *pattern, int suffix_len, int mode)
{
	static const char letters[] =
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"0123456789";
	static const int num_letters = ARRAY_SIZE(letters) - 1;
	static const char x_pattern[] = "XXXXXX";
	static const int num_x = ARRAY_SIZE(x_pattern) - 1;
	char *filename_template;
	size_t len;
	int fd, count;

	len = strlen(pattern);

	if (len < num_x + suffix_len) {
		errno = EINVAL;
		return -1;
	}

	if (strncmp(&pattern[len - num_x - suffix_len], x_pattern, num_x)) {
		errno = EINVAL;
		return -1;
	}

	/*
	 * Replace pattern's XXXXXX characters with randomness.
	 * Try TMP_MAX different filenames.
	 */
	filename_template = &pattern[len - num_x - suffix_len];
	for (count = 0; count < TMP_MAX; ++count) {
		int i;
		uint64_t v;
		if (csprng_bytes(&v, sizeof(v)) < 0)
			return error_errno("unable to get random bytes for temporary file");

		/* Fill in the random bits. */
		for (i = 0; i < num_x; i++) {
			filename_template[i] = letters[v % num_letters];
			v /= num_letters;
		}

		fd = open(pattern, O_CREAT | O_EXCL | O_RDWR, mode);
		if (fd >= 0)
			return fd;
		/*
		 * Fatal error (EPERM, ENOSPC etc).
		 * It doesn't make sense to loop.
		 */
		if (errno != EEXIST)
			break;
	}
	/* We return the null string if we can't find a unique file name.  */
	pattern[0] = '\0';
	return -1;
}

int git_mkstemp_mode(char *pattern, int mode)
{
	/* mkstemp is just mkstemps with no suffix */
	return git_mkstemps_mode(pattern, 0, mode);
}

int xmkstemp_mode(char *filename_template, int mode)
{
	int fd;
	char origtemplate[PATH_MAX];
	strlcpy(origtemplate, filename_template, sizeof(origtemplate));

	fd = git_mkstemp_mode(filename_template, mode);
	if (fd < 0) {
		int saved_errno = errno;
		const char *nonrelative_template;

		if (!filename_template[0])
			filename_template = origtemplate;

		nonrelative_template = absolute_path(filename_template);
		errno = saved_errno;
		die_errno("Unable to create temporary file '%s'",
			nonrelative_template);
	}
	return fd;
}

/*
 * Some platforms return EINTR from fsync. Since fsync is invoked in some
 * cases by a wrapper that dies on failure, do not expose EINTR to callers.
 */
static int fsync_loop(int fd)
{
	int err;

	do {
		err = fsync(fd);
	} while (err < 0 && errno == EINTR);
	return err;
}

int git_fsync(int fd, enum fsync_action action)
{
	switch (action) {
	case FSYNC_WRITEOUT_ONLY:
		trace2_counter_add(TRACE2_COUNTER_ID_FSYNC_WRITEOUT_ONLY, 1);

#ifdef __APPLE__
		/*
		 * On macOS, fsync just causes filesystem cache writeback but
		 * does not flush hardware caches.
		 */
		return fsync_loop(fd);
#endif

#ifdef HAVE_SYNC_FILE_RANGE
		/*
		 * On linux 2.6.17 and above, sync_file_range is the way to
		 * issue a writeback without a hardware flush. An offset of
		 * 0 and size of 0 indicates writeout of the entire file and the
		 * wait flags ensure that all dirty data is written to the disk
		 * (potentially in a disk-side cache) before we continue.
		 */

		return sync_file_range(fd, 0, 0, SYNC_FILE_RANGE_WAIT_BEFORE |
						 SYNC_FILE_RANGE_WRITE |
						 SYNC_FILE_RANGE_WAIT_AFTER);
#endif

#ifdef fsync_no_flush
		return fsync_no_flush(fd);
#endif

		errno = ENOSYS;
		return -1;

	case FSYNC_HARDWARE_FLUSH:
		trace2_counter_add(TRACE2_COUNTER_ID_FSYNC_HARDWARE_FLUSH, 1);

		/*
		 * On macOS, a special fcntl is required to really flush the
		 * caches within the storage controller. As of this writing,
		 * this is a very expensive operation on Apple SSDs.
		 */
#ifdef __APPLE__
		return fcntl(fd, F_FULLFSYNC);
#else
		return fsync_loop(fd);
#endif
	default:
		BUG("unexpected git_fsync(%d) call", action);
	}
}

static int warn_if_unremovable(const char *op, const char *file, int rc)
{
	int err;
	if (!rc || errno == ENOENT)
		return 0;
	err = errno;
	warning_errno("unable to %s '%s'", op, file);
	errno = err;
	return rc;
}

int unlink_or_msg(const char *file, struct strbuf *err)
{
	int rc = unlink(file);

	assert(err);

	if (!rc || errno == ENOENT)
		return 0;

	strbuf_addf(err, "unable to unlink '%s': %s",
		    file, strerror(errno));
	return -1;
}

int unlink_or_warn(const char *file)
{
	return warn_if_unremovable("unlink", file, unlink(file));
}

int rmdir_or_warn(const char *file)
{
	return warn_if_unremovable("rmdir", file, rmdir(file));
}

static int access_error_is_ok(int err, unsigned flag)
{
	return (is_missing_file_error(err) ||
		((flag & ACCESS_EACCES_OK) && err == EACCES));
}

int access_or_warn(const char *path, int mode, unsigned flag)
{
	int ret = access(path, mode);
	if (ret && !access_error_is_ok(errno, flag))
		warn_on_inaccessible(path);
	return ret;
}

int access_or_die(const char *path, int mode, unsigned flag)
{
	int ret = access(path, mode);
	if (ret && !access_error_is_ok(errno, flag))
		die_errno(_("unable to access '%s'"), path);
	return ret;
}

char *xgetcwd(void)
{
	struct strbuf sb = STRBUF_INIT;
	if (strbuf_getcwd(&sb))
		die_errno(_("unable to get current working directory"));
	return strbuf_detach(&sb, NULL);
}

int xsnprintf(char *dst, size_t max, const char *fmt, ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(dst, max, fmt, ap);
	va_end(ap);

	if (len < 0)
		die(_("unable to format message: %s"), fmt);
	if (len >= max)
		BUG("attempt to snprintf into too-small buffer");
	return len;
}

void write_file_buf(const char *path, const char *buf, size_t len)
{
	int fd = xopen(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (write_in_full(fd, buf, len) < 0)
		die_errno(_("could not write to '%s'"), path);
	if (close(fd))
		die_errno(_("could not close '%s'"), path);
}

void write_file(const char *path, const char *fmt, ...)
{
	va_list params;
	struct strbuf sb = STRBUF_INIT;

	va_start(params, fmt);
	strbuf_vaddf(&sb, fmt, params);
	va_end(params);

	strbuf_complete_line(&sb);

	write_file_buf(path, sb.buf, sb.len);
	strbuf_release(&sb);
}

void sleep_millisec(int millisec)
{
	poll(NULL, 0, millisec);
}

int xgethostname(char *buf, size_t len)
{
	/*
	 * If the full hostname doesn't fit in buf, POSIX does not
	 * specify whether the buffer will be null-terminated, so to
	 * be safe, do it ourselves.
	 */
	int ret = gethostname(buf, len);
	if (!ret)
		buf[len - 1] = 0;
	return ret;
}

int is_empty_or_missing_file(const char *filename)
{
	struct stat st;

	if (stat(filename, &st) < 0) {
		if (errno == ENOENT)
			return 1;
		die_errno(_("could not stat %s"), filename);
	}

	return !st.st_size;
}

int open_nofollow(const char *path, int flags)
{
#ifdef O_NOFOLLOW
	return open(path, flags | O_NOFOLLOW);
#else
	struct stat st;
	if (lstat(path, &st) < 0)
		return -1;
	if (S_ISLNK(st.st_mode)) {
		errno = ELOOP;
		return -1;
	}
	return open(path, flags);
#endif
}

int csprng_bytes(void *buf, size_t len)
{
#if defined(HAVE_ARC4RANDOM) || defined(HAVE_ARC4RANDOM_LIBBSD)
	/* This function never returns an error. */
	arc4random_buf(buf, len);
	return 0;
#elif defined(HAVE_GETRANDOM)
	ssize_t res;
	char *p = buf;
	while (len) {
		res = getrandom(p, len, 0);
		if (res < 0)
			return -1;
		len -= res;
		p += res;
	}
	return 0;
#elif defined(HAVE_GETENTROPY)
	int res;
	char *p = buf;
	while (len) {
		/* getentropy has a maximum size of 256 bytes. */
		size_t chunk = len < 256 ? len : 256;
		res = getentropy(p, chunk);
		if (res < 0)
			return -1;
		len -= chunk;
		p += chunk;
	}
	return 0;
#elif defined(HAVE_RTLGENRANDOM)
	if (!RtlGenRandom(buf, len))
		return -1;
	return 0;
#elif defined(HAVE_OPENSSL_CSPRNG)
	int res = RAND_bytes(buf, len);
	if (res == 1)
		return 0;
	if (res == -1)
		errno = ENOTSUP;
	else
		errno = EIO;
	return -1;
#else
	ssize_t res;
	char *p = buf;
	int fd, err;
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return -1;
	while (len) {
		res = xread(fd, p, len);
		if (res < 0) {
			err = errno;
			close(fd);
			errno = err;
			return -1;
		}
		len -= res;
		p += res;
	}
	close(fd);
	return 0;
#endif
}

uint32_t git_rand(void)
{
	uint32_t result;

	if (csprng_bytes(&result, sizeof(result)) < 0)
		die(_("unable to get random bytes"));

	return result;
}
