#define WIN32_LEAN_AND_MEAN
#include "../git-compat-util.h"
#include "win32.h"
#include "../cache.h" /* to read configuration */

static inline void filetime_to_timespec(const FILETIME *ft, struct timespec *ts)
{
	long long winTime = ((long long)ft->dwHighDateTime << 32) +
			ft->dwLowDateTime;
	winTime -= 116444736000000000LL; /* Windows to Unix Epoch conversion */
	/* convert 100-nsecond interval to seconds and nanoseconds */
	ts->tv_sec = (time_t)(winTime/10000000);
	ts->tv_nsec = (long)(winTime - ts->tv_sec*10000000LL) * 100;
}

#define size_to_blocks(s) (((s)+511)/512)

/* do_stat is a common implementation for cygwin_lstat and cygwin_stat.
 *
 * To simplify its logic, in the case of cygwin symlinks, this implementation
 * falls back to the cygwin version of stat/lstat, which is provided as the
 * last argument.
 */
static int do_stat(const char *file_name, struct stat *buf, stat_fn_t cygstat)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata;

	if (file_name[0] == '/')
		return cygstat (file_name, buf);

	if (!(errno = get_file_attr(file_name, &fdata))) {
		/*
		 * If the system attribute is set and it is not a directory then
		 * it could be a symbol link created in the nowinsymlinks mode.
		 * Normally, Cygwin works in the winsymlinks mode, so this situation
		 * is very unlikely. For the sake of simplicity of our code, let's
		 * Cygwin to handle it.
		 */
		if ((fdata.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) &&
		    !(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			return cygstat(file_name, buf);

		/* fill out the stat structure */
		buf->st_dev = buf->st_rdev = 0; /* not used by Git */
		buf->st_ino = 0;
		buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes);
		buf->st_nlink = 1;
		buf->st_uid = buf->st_gid = 0;
#ifdef __CYGWIN_USE_BIG_TYPES__
		buf->st_size = ((_off64_t)fdata.nFileSizeHigh << 32) +
			fdata.nFileSizeLow;
#else
		buf->st_size = (off_t)fdata.nFileSizeLow;
#endif
		buf->st_blocks = size_to_blocks(buf->st_size);
		filetime_to_timespec(&fdata.ftLastAccessTime, &buf->st_atim);
		filetime_to_timespec(&fdata.ftLastWriteTime, &buf->st_mtim);
		filetime_to_timespec(&fdata.ftCreationTime, &buf->st_ctim);
		return 0;
	} else if (errno == ENOENT) {
		/*
		 * In the winsymlinks mode (which is the default), Cygwin
		 * emulates symbol links using Windows shortcut files. These
		 * files are formed by adding .lnk extension. So, if we have
		 * not found the specified file name, it could be that it is
		 * a symbol link. Let's Cygwin to deal with that.
		 */
		return cygstat(file_name, buf);
	}
	return -1;
}

/* We provide our own lstat/stat functions, since the provided Cygwin versions
 * of these functions are too slow. These stat functions are tailored for Git's
 * usage, and therefore they are not meant to be complete and correct emulation
 * of lstat/stat functionality.
 */
static int cygwin_lstat(const char *path, struct stat *buf)
{
	return do_stat(path, buf, lstat);
}

static int cygwin_stat(const char *path, struct stat *buf)
{
	return do_stat(path, buf, stat);
}


/*
 * At start up, we are trying to determine whether Win32 API or cygwin stat
 * functions should be used. The choice is determined by core.ignorecygwinfstricks.
 * Reading this option is not always possible immediately as git_dir may
 * not be set yet. So until it is set, use cygwin lstat/stat functions.
 * However, if core.filemode is set, we must use the Cygwin posix
 * stat/lstat as the Windows stat functions do not determine posix filemode.
 *
 * Note that git_cygwin_config() does NOT call git_default_config() and this
 * is deliberate.  Many commands read from config to establish initial
 * values in variables and later tweak them from elsewhere (e.g. command line).
 * init_stat() is called lazily on demand, typically much late in the program,
 * and calling git_default_config() from here would break such variables.
 */
static int native_stat = 1;
static int core_filemode;

static int git_cygwin_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "core.ignorecygwinfstricks"))
		native_stat = git_config_bool(var, value);
	else if (!strcmp(var, "core.filemode"))
		core_filemode = git_config_bool(var, value);
	return 0;
}

static int init_stat(void)
{
	if (have_git_dir() && git_config(git_cygwin_config,NULL)) {
		if (!core_filemode && native_stat) {
			cygwin_stat_fn = cygwin_stat;
			cygwin_lstat_fn = cygwin_lstat;
		} else {
			cygwin_stat_fn = stat;
			cygwin_lstat_fn = lstat;
		}
		return 0;
	}
	return 1;
}

static int cygwin_stat_stub(const char *file_name, struct stat *buf)
{
	return (init_stat() ? stat : *cygwin_stat_fn)(file_name, buf);
}

static int cygwin_lstat_stub(const char *file_name, struct stat *buf)
{
	return (init_stat() ? lstat : *cygwin_lstat_fn)(file_name, buf);
}

stat_fn_t cygwin_stat_fn = cygwin_stat_stub;
stat_fn_t cygwin_lstat_fn = cygwin_lstat_stub;

