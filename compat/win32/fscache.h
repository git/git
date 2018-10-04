#ifndef FSCACHE_H
#define FSCACHE_H

/*
 * The fscache is thread specific. enable_fscache() must be called
 * for each thread where caching is desired.
 */

int fscache_enable(size_t initial_size);
#define enable_fscache(initial_size) fscache_enable(initial_size)

void fscache_disable(void);
#define disable_fscache() fscache_disable()

int fscache_enabled(const char *path);
#define is_fscache_enabled(path) fscache_enabled(path)

void fscache_flush(void);
#define flush_fscache() fscache_flush()

DIR *fscache_opendir(const char *dir);
int fscache_lstat(const char *file_name, struct stat *buf);

/* opaque fscache structure */
struct fscache;

struct fscache *fscache_getcache(void);
#define getcache_fscache() fscache_getcache()

void fscache_merge(struct fscache *dest);
#define merge_fscache(dest) fscache_merge(dest)

#endif
