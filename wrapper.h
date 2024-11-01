#ifndef WRAPPER_H
#define WRAPPER_H

char *xstrdup(const char *str);
void *xmalloc(size_t size);
void *xmallocz(size_t size);
void *xmallocz_gently(size_t size);
void *xmemdupz(const void *data, size_t len);
char *xstrndup(const char *str, size_t len);
void *xrealloc(void *ptr, size_t size);
void *xcalloc(size_t nmemb, size_t size);
void xsetenv(const char *name, const char *value, int overwrite);
void *xmmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
const char *mmap_os_err(void);
void *xmmap_gently(void *start, size_t length, int prot, int flags, int fd, off_t offset);
int xopen(const char *path, int flags, ...);
ssize_t xread(int fd, void *buf, size_t len);
ssize_t xwrite(int fd, const void *buf, size_t len);
ssize_t xwritev(int fd, const struct git_iovec *, int iovcnt);
ssize_t xpread(int fd, void *buf, size_t len, off_t offset);
int xdup(int fd);
FILE *xfopen(const char *path, const char *mode);
FILE *xfdopen(int fd, const char *mode);
int xmkstemp(char *temp_filename);
int xmkstemp_mode(char *temp_filename, int mode);
char *xgetcwd(void);
FILE *fopen_for_writing(const char *path);
FILE *fopen_or_warn(const char *path, const char *mode);

/*
 * Like strncmp, but only return zero if s is NUL-terminated and exactly len
 * characters long.  If it is not, consider it greater than t.
 */
int xstrncmpz(const char *s, const char *t, size_t len);

__attribute__((format (printf, 3, 4)))
int xsnprintf(char *dst, size_t max, const char *fmt, ...);

int xgethostname(char *buf, size_t len);

/* set default permissions by passing mode arguments to open(2) */
int git_mkstemps_mode(char *pattern, int suffix_len, int mode);
int git_mkstemp_mode(char *pattern, int mode);

ssize_t read_in_full(int fd, void *buf, size_t count);
ssize_t write_in_full(int fd, const void *buf, size_t count);
ssize_t pread_in_full(int fd, void *buf, size_t count, off_t offset);

static inline ssize_t write_str_in_full(int fd, const char *str)
{
	return write_in_full(fd, str, strlen(str));
}

/**
 * Open (and truncate) the file at path, write the contents of buf to it,
 * and close it. Dies if any errors are encountered.
 */
void write_file_buf(const char *path, const char *buf, size_t len);

/**
 * Like write_file_buf(), but format the contents into a buffer first.
 * Additionally, write_file() will append a newline if one is not already
 * present, making it convenient to write text files:
 *
 *   write_file(path, "counter: %d", ctr);
 */
__attribute__((format (printf, 2, 3)))
void write_file(const char *path, const char *fmt, ...);

/* Return 1 if the file is empty or does not exists, 0 otherwise. */
int is_empty_or_missing_file(const char *filename);

enum fsync_action {
	FSYNC_WRITEOUT_ONLY,
	FSYNC_HARDWARE_FLUSH
};

/*
 * Issues an fsync against the specified file according to the specified mode.
 *
 * FSYNC_WRITEOUT_ONLY attempts to use interfaces available on some operating
 * systems to flush the OS cache without issuing a flush command to the storage
 * controller. If those interfaces are unavailable, the function fails with
 * ENOSYS.
 *
 * FSYNC_HARDWARE_FLUSH does an OS writeout and hardware flush to ensure that
 * changes are durable. It is not expected to fail.
 */
int git_fsync(int fd, enum fsync_action action);

/*
 * Preserves errno, prints a message, but gives no warning for ENOENT.
 * Returns 0 on success, which includes trying to unlink an object that does
 * not exist.
 */
int unlink_or_warn(const char *path);
 /*
  * Tries to unlink file.  Returns 0 if unlink succeeded
  * or the file already didn't exist.  Returns -1 and
  * appends a message to err suitable for
  * 'error("%s", err->buf)' on error.
  */
int unlink_or_msg(const char *file, struct strbuf *err);
/*
 * Preserves errno, prints a message, but gives no warning for ENOENT.
 * Returns 0 on success, which includes trying to remove a directory that does
 * not exist.
 */
int rmdir_or_warn(const char *path);

/*
 * Call access(2), but warn for any error except "missing file"
 * (ENOENT or ENOTDIR).
 */
#define ACCESS_EACCES_OK (1U << 0)
int access_or_warn(const char *path, int mode, unsigned flag);
int access_or_die(const char *path, int mode, unsigned flag);

/* Warn on an inaccessible file if errno indicates this is an error */
int warn_on_fopen_errors(const char *path);

/*
 * Open with O_NOFOLLOW, or equivalent. Note that the fallback equivalent
 * may be racy. Do not use this as protection against an attacker who can
 * simultaneously create paths.
 */
int open_nofollow(const char *path, int flags);

void sleep_millisec(int millisec);

/*
 * Generate len bytes from the system cryptographically secure PRNG.
 * Returns 0 on success and -1 on error, setting errno.  The inability to
 * satisfy the full request is an error.
 */
int csprng_bytes(void *buf, size_t len);

/*
 * Returns a random uint32_t, uniformly distributed across all possible
 * values.
 */
uint32_t git_rand(void);

/* Provide log2 of the given `size_t`. */
static inline unsigned log2u(uintmax_t sz)
{
	unsigned l = 0;

	/*
	 * Technically this isn't required, but it helps the compiler optimize
	 * this to a `bsr` instruction.
	 */
	if (!sz)
		return 0;

	for (; sz; sz >>= 1)
		l++;

	return l - 1;
}

#endif /* WRAPPER_H */
