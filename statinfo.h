#ifndef STATINFO_H
#define STATINFO_H

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

#endif
