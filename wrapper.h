#ifndef WRAPPER_H
#define WRAPPER_H

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

#endif /* WRAPPER_H */
