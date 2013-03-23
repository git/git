#include <sys/types.h>
#include <sys/stat.h>

typedef int (*stat_fn_t)(const char*, struct stat*);
extern stat_fn_t cygwin_stat_fn;
extern stat_fn_t cygwin_lstat_fn;
int cygwin_get_st_mode_bits(const char *path, int *mode);

#define get_st_mode_bits(p,m) cygwin_get_st_mode_bits((p),(m))
#ifndef CYGWIN_C
/* cygwin.c needs the original lstat() */
#define stat(path, buf) (*cygwin_stat_fn)(path, buf)
#define lstat(path, buf) (*cygwin_lstat_fn)(path, buf)
#endif
