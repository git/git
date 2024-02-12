#include "git-compat-util.h"
#include "environment.h"
#include "statinfo.h"

/*
 * Munge st_size into an unsigned int.
 */
static unsigned int munge_st_size(off_t st_size) {
	unsigned int sd_size = st_size;

	/*
	 * If the file is an exact multiple of 4 GiB, modify the value so it
	 * doesn't get marked as racily clean (zero).
	 */
	if (!sd_size && st_size)
		return 0x80000000;
	else
		return sd_size;
}

void fill_stat_data(struct stat_data *sd, struct stat *st)
{
	sd->sd_ctime.sec = (unsigned int)st->st_ctime;
	sd->sd_mtime.sec = (unsigned int)st->st_mtime;
	sd->sd_ctime.nsec = ST_CTIME_NSEC(*st);
	sd->sd_mtime.nsec = ST_MTIME_NSEC(*st);
	sd->sd_dev = st->st_dev;
	sd->sd_ino = st->st_ino;
	sd->sd_uid = st->st_uid;
	sd->sd_gid = st->st_gid;
	sd->sd_size = munge_st_size(st->st_size);
}

static void set_times(struct stat *st, const struct stat_data *sd)
{
	st->st_ctime = sd->sd_ctime.sec;
	st->st_mtime = sd->sd_mtime.sec;
#ifdef NO_NSEC
	; /* nothing */
#else
#ifdef USE_ST_TIMESPEC
	st->st_ctimespec.tv_nsec = sd->sd_ctime.nsec;
	st->st_mtimespec.tv_nsec = sd->sd_mtime.nsec;
#else
	st->st_ctim.tv_nsec = sd->sd_ctime.nsec;
	st->st_mtim.tv_nsec = sd->sd_mtime.nsec;
#endif
#endif
}

void fake_lstat_data(const struct stat_data *sd, struct stat *st)
{
	set_times(st, sd);
	st->st_dev = sd->sd_dev;
	st->st_ino = sd->sd_ino;
	st->st_uid = sd->sd_uid;
	st->st_gid = sd->sd_gid;
	st->st_size = sd->sd_size;
}

int match_stat_data(const struct stat_data *sd, struct stat *st)
{
	int changed = 0;

	if (sd->sd_mtime.sec != (unsigned int)st->st_mtime)
		changed |= MTIME_CHANGED;
	if (trust_ctime && check_stat &&
	    sd->sd_ctime.sec != (unsigned int)st->st_ctime)
		changed |= CTIME_CHANGED;

#ifdef USE_NSEC
	if (check_stat && sd->sd_mtime.nsec != ST_MTIME_NSEC(*st))
		changed |= MTIME_CHANGED;
	if (trust_ctime && check_stat &&
	    sd->sd_ctime.nsec != ST_CTIME_NSEC(*st))
		changed |= CTIME_CHANGED;
#endif

	if (check_stat) {
		if (sd->sd_uid != (unsigned int) st->st_uid ||
			sd->sd_gid != (unsigned int) st->st_gid)
			changed |= OWNER_CHANGED;
		if (sd->sd_ino != (unsigned int) st->st_ino)
			changed |= INODE_CHANGED;
	}

#ifdef USE_STDEV
	/*
	 * st_dev breaks on network filesystems where different
	 * clients will have different views of what "device"
	 * the filesystem is on
	 */
	if (check_stat && sd->sd_dev != (unsigned int) st->st_dev)
			changed |= INODE_CHANGED;
#endif

	if (sd->sd_size != munge_st_size(st->st_size))
		changed |= DATA_CHANGED;

	return changed;
}

void stat_validity_clear(struct stat_validity *sv)
{
	FREE_AND_NULL(sv->sd);
}

int stat_validity_check(struct stat_validity *sv, const char *path)
{
	struct stat st;

	if (stat(path, &st) < 0)
		return sv->sd == NULL;
	if (!sv->sd)
		return 0;
	return S_ISREG(st.st_mode) && !match_stat_data(sv->sd, &st);
}

void stat_validity_update(struct stat_validity *sv, int fd)
{
	struct stat st;

	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode))
		stat_validity_clear(sv);
	else {
		if (!sv->sd)
			CALLOC_ARRAY(sv->sd, 1);
		fill_stat_data(sv->sd, &st);
	}
}
