#ifndef STATINFO_H
#define STATINFO_H

struct index_state;

/*
 * The "cache_time" is just the low 32 bits of the
 * time. It doesn't matter if it overflows - we only
 * check it for equality in the 32 bits we save.
 */
struct cache_time {
	uint32_t sec;
	uint32_t nsec;
};

struct stat_data {
	struct cache_time sd_ctime;
	struct cache_time sd_mtime;
	unsigned int sd_dev;
	unsigned int sd_ino;
	unsigned int sd_uid;
	unsigned int sd_gid;
	unsigned int sd_size;
};

/*
 * A struct to encapsulate the concept of whether a file has changed
 * since we last checked it. This uses criteria similar to those used
 * for the index.
 */
struct stat_validity {
	struct stat_data *sd;
};

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020
#define TYPE_CHANGED    0x0040

/*
 * Record to sd the data from st that we use to check whether a file
 * might have changed.
 */
void fill_stat_data(struct stat_data *sd, struct stat *st);

/*
 * Return 0 if st is consistent with a file not having been changed
 * since sd was filled.  If there are differences, return a
 * combination of MTIME_CHANGED, CTIME_CHANGED, OWNER_CHANGED,
 * INODE_CHANGED, and DATA_CHANGED.
 */
int match_stat_data(const struct stat_data *sd, struct stat *st);

void stat_validity_clear(struct stat_validity *sv);

/*
 * Returns 1 if the path is a regular file (or a symlink to a regular
 * file) and matches the saved stat_validity, 0 otherwise.  A missing
 * or inaccessible file is considered a match if the struct was just
 * initialized, or if the previous update found an inaccessible file.
 */
int stat_validity_check(struct stat_validity *sv, const char *path);

/*
 * Update the stat_validity from a file opened at descriptor fd. If
 * the file is missing, inaccessible, or not a regular file, then
 * future calls to stat_validity_check will match iff one of those
 * conditions continues to be true.
 */
void stat_validity_update(struct stat_validity *sv, int fd);

#if defined(DT_UNKNOWN) && !defined(NO_D_TYPE_IN_DIRENT)
#define DTYPE(de)	((de)->d_type)
#else
#undef DT_UNKNOWN
#undef DT_DIR
#undef DT_REG
#undef DT_LNK
#define DT_UNKNOWN	0
#define DT_DIR		1
#define DT_REG		2
#define DT_LNK		3
#define DTYPE(de)	DT_UNKNOWN
#endif

#endif
