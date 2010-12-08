#ifndef DIRENT_H
#define DIRENT_H

typedef struct DIR DIR;

#define DT_UNKNOWN 0
#define DT_DIR     1
#define DT_REG     2
#define DT_LNK     3

struct dirent {
	long d_ino;                      /* Always zero. */
	char d_name[FILENAME_MAX];       /* File name. */
	union {
		unsigned short d_reclen; /* Always zero. */
		unsigned char  d_type;   /* Reimplementation adds this */
	};
};

DIR *opendir(const char *dirname);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#endif /* DIRENT_H */
