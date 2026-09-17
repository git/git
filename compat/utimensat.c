#include "../git-compat-util.h"

int git_utimensat(int fd, const char *path, const struct timespec times[2], int flag)
{
	struct timeval tv[2];
	struct timeval *tvp = NULL;

	if (fd != AT_FDCWD) {
		errno = ENOSYS;
		return -1;
	}

	if (flag) {
		errno = ENOSYS;
		return -1;
	}

	if (times) {
		for (int i = 0; i < 2; i++) {
			if (times[i].tv_nsec == UTIME_NOW) {
				struct timeval now;
				gettimeofday(&now, NULL);
				tv[i] = now;
			} else if (times[i].tv_nsec == UTIME_OMIT) {
				struct stat st;
				if (stat(path, &st) < 0)
					return -1;
				tv[i].tv_sec = (i == 0) ? st.st_atime : st.st_mtime;
				tv[i].tv_usec = (i == 0) ? ST_ATIME_NSEC(st) / 1000 : ST_MTIME_NSEC(st) / 1000;
			} else {
				tv[i].tv_sec = times[i].tv_sec;
				tv[i].tv_usec = times[i].tv_nsec / 1000;
			}
		}
		tvp = tv;
	}

	return utimes(path, tvp);
}
