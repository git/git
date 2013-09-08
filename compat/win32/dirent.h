#ifndef DIRENT_H
#define DIRENT_H

#define DT_UNKNOWN 0
#define DT_DIR     1
#define DT_REG     2
#define DT_LNK     3

struct dirent {
	unsigned char d_type; /* file type to prevent lstat after readdir */
	char *d_name;         /* file name */
};

/*
 * Base DIR structure, contains pointers to readdir/closedir implementations so
 * that opendir may choose a concrete implementation on a call-by-call basis.
 */
typedef struct DIR {
	struct dirent *(*preaddir)(struct DIR *dir);
	int (*pclosedir)(struct DIR *dir);
} DIR;

/* default dirent implementation */
extern DIR *dirent_opendir(const char *dirname);

/* current dirent implementation */
extern DIR *(*opendir)(const char *dirname);

#define readdir(dir) (dir->preaddir(dir))
#define closedir(dir) (dir->pclosedir(dir))

#endif /* DIRENT_H */
