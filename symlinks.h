#ifndef SYMLINKS_H
#define SYMLINKS_H

#include "strbuf.h"

struct cache_def {
	struct strbuf path;
	int flags;
	int track_flags;
	int prefix_len_stat_func;
};
#define CACHE_DEF_INIT { \
	.path = STRBUF_INIT, \
}
static inline void cache_def_clear(struct cache_def *cache)
{
	strbuf_release(&cache->path);
}

int has_symlink_leading_path(const char *name, int len);
int threaded_has_symlink_leading_path(struct cache_def *, const char *, int);
int check_leading_path(const char *name, int len, int warn_on_lstat_err);
int has_dirs_only_path(const char *name, int len, int prefix_len);
void invalidate_lstat_cache(void);
void schedule_dir_for_removal(const char *name, int len);
void remove_scheduled_dirs(void);

#endif /* SYMLINKS_H */
