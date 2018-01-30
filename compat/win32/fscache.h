#ifndef FSCACHE_H
#define FSCACHE_H

int fscache_enable(int enable);
#define enable_fscache(x) fscache_enable(x)

int fscache_enabled(const char *path);
#define is_fscache_enabled(path) fscache_enabled(path)

void fscache_flush(void);
#define flush_fscache() fscache_flush()

DIR *fscache_opendir(const char *dir);
int fscache_lstat(const char *file_name, struct stat *buf);

#endif
