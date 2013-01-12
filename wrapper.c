/*
 * Various trivial helper wrappers around standard functions
 */
#include "cache.h"

static void do_nothing(size_t size)
{
}

static void (*try_to_free_routine)(size_t size) = do_nothing;

static void memory_limit_check(size_t size)
{
	static int limit = -1;
	if (limit == -1) {
		const char *env = getenv("GIT_ALLOC_LIMIT");
		limit = env ? atoi(env) * 1024 : 0;
	}
	if (limit && size > limit)
		die("attempting to allocate %"PRIuMAX" over limit %d",
		    (intmax_t)size, limit);
}

try_to_free_t set_try_to_free_routine(try_to_free_t routine)
{
	try_to_free_t old = try_to_free_routine;
	if (!routine)
		routine = do_nothing;
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
	void *ret;

	memory_limit_check(size);
	ret = malloc(size);
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
	if (unsigned_add_overflows(size, 1))
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
	void *ret;

	memory_limit_check(size);
	ret = realloc(ptr, size);
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
	void *ret;

	memory_limit_check(size * nmemb);
	ret = calloc(nmemb, size);
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
	char origtemplate[PATH_MAX];
	strlcpy(origtemplate, template, sizeof(origtemplate));

	fd = mkstemp(template);
	if (fd < 0) {
		int saved_errno = errno;
		const char *nonrelative_template;

		if (strlen(template) != strlen(origtemplate))
			template = origtemplate;

		nonrelative_template = absolute_path(template);
		errno = saved_errno;
		die_errno("Unable to create temporary file '%s'",
			nonrelative_template);
	}
	return fd;
}

/* git_mkstemp() - create tmp file honoring TMPDIR variable */
int git_mkstemp(char *path, size_t len, const char *template)
{
	const char *tmp;
	size_t n;

	tmp = getenv("TMPDIR");
	if (!tmp)
		tmp = "/tmp";
	n = snprintf(path, len, "%s/%s", tmp, template);
	if (len <= n) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return mkstemp(path);
}

/* git_mkstemps() - create tmp file with suffix honoring TMPDIR variable. */
int git_mkstemps(char *path, size_t len, const char *template, int suffix_len)
{
	const char *tmp;
	size_t n;

	tmp = getenv("TMPDIR");
	if (!tmp)
		tmp = "/tmp";
	n = snprintf(path, len, "%s/%s", tmp, template);
	if (len <= n) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return mkstemps(path, suffix_len);
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
	static const int num_letters = 62;
	uint64_t value;
	struct timeval tv;
	char *template;
	size_t len;
	int fd, count;

	len = strlen(pattern);

	if (len < 6 + suffix_len) {
		errno = EINVAL;
		return -1;
	}

	if (strncmp(&pattern[len - 6 - suffix_len], "XXXXXX", 6)) {
		errno = EINVAL;
		return -1;
	}

	/*
	 * Replace pattern's XXXXXX characters with randomness.
	 * Try TMP_MAX different filenames.
	 */
	gettimeofday(&tv, NULL);
	value = ((size_t)(tv.tv_usec << 16)) ^ tv.tv_sec ^ getpid();
	template = &pattern[len - 6 - suffix_len];
	for (count = 0; count < TMP_MAX; ++count) {
		uint64_t v = value;
		/* Fill in the random bits. */
		template[0] = letters[v % num_letters]; v /= num_letters;
		template[1] = letters[v % num_letters]; v /= num_letters;
		template[2] = letters[v % num_letters]; v /= num_letters;
		template[3] = letters[v % num_letters]; v /= num_letters;
		template[4] = letters[v % num_letters]; v /= num_letters;
		template[5] = letters[v % num_letters]; v /= num_letters;

		fd = open(pattern, O_CREAT | O_EXCL | O_RDWR, mode);
		if (fd > 0)
			return fd;
		/*
		 * Fatal error (EPERM, ENOSPC etc).
		 * It doesn't make sense to loop.
		 */
		if (errno != EEXIST)
			break;
		/*
		 * This is a random value.  It is only necessary that
		 * the next TMP_MAX values generated by adding 7777 to
		 * VALUE are different with (module 2^32).
		 */
		value += 7777;
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

int gitmkstemps(char *pattern, int suffix_len)
{
	return git_mkstemps_mode(pattern, suffix_len, 0600);
}

int xmkstemp_mode(char *template, int mode)
{
	int fd;
	char origtemplate[PATH_MAX];
	strlcpy(origtemplate, template, sizeof(origtemplate));

	fd = git_mkstemp_mode(template, mode);
	if (fd < 0) {
		int saved_errno = errno;
		const char *nonrelative_template;

		if (!template[0])
			template = origtemplate;

		nonrelative_template = absolute_path(template);
		errno = saved_errno;
		die_errno("Unable to create temporary file '%s'",
			nonrelative_template);
	}
	return fd;
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

void warn_on_inaccessible(const char *path)
{
	warning(_("unable to access '%s': %s"), path, strerror(errno));
}

int access_or_warn(const char *path, int mode)
{
	int ret = access(path, mode);
	if (ret && errno != ENOENT && errno != ENOTDIR)
		warn_on_inaccessible(path);
	return ret;
}

int access_or_die(const char *path, int mode)
{
	int ret = access(path, mode);
	if (ret && errno != ENOENT && errno != ENOTDIR)
		die_errno(_("unable to access '%s'"), path);
	return ret;
}

struct passwd *xgetpwuid_self(void)
{
	struct passwd *pw;

	errno = 0;
	pw = getpwuid(getuid());
	if (!pw)
		die(_("unable to look up current user in the passwd file: %s"),
		    errno ? strerror(errno) : _("no such user"));
	return pw;
}
