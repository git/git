#ifndef FSM_PATH_UTILS_H
#define FSM_PATH_UTILS_H

struct fs_info {
	int is_remote;
	char *typename;
};

/*
 * Get some basic filesystem informtion for the given path
 *
 * The caller owns the storage that is occupied by fs_info and
 * is responsible for releasing it.
 *
 * Returns -1 on error, zero otherwise.
 */
int fsmonitor__get_fs_info(const char *path, struct fs_info *fs_info);

/*
 * Determines if the filesystem that path resides on is remote.
 *
 * Returns -1 on error, 0 if not remote, 1 if remote.
 */
int fsmonitor__is_fs_remote(const char *path);

#endif
