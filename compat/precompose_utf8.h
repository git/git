#ifndef PRECOMPOSE_UNICODE_H
#define PRECOMPOSE_UNICODE_H

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <iconv.h>


typedef struct dirent_prec_psx {
	ino_t d_ino;            /* Posix */
	size_t max_name_len;    /* See below */
	unsigned char d_type;   /* available on all systems git runs on */

	/*
	 * See http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/dirent.h.html
	 * NAME_MAX + 1 should be enough, but some systems have
	 * NAME_MAX=255 and strlen(d_name) may return 508 or 510
	 * Solution: allocate more when needed, see precompose_utf8_readdir()
	 */
	char   d_name[NAME_MAX+1];
} dirent_prec_psx;


typedef struct {
	iconv_t ic_precompose;
	DIR *dirp;
	struct dirent_prec_psx *dirent_nfc;
} PREC_DIR;

void precompose_argv(int argc, const char **argv);
void probe_utf8_pathname_composition(void);

PREC_DIR *precompose_utf8_opendir(const char *dirname);
struct dirent_prec_psx *precompose_utf8_readdir(PREC_DIR *dirp);
int precompose_utf8_closedir(PREC_DIR *dirp);

#ifndef PRECOMPOSE_UNICODE_C
#define dirent dirent_prec_psx
#define opendir(n) precompose_utf8_opendir(n)
#define readdir(d) precompose_utf8_readdir(d)
#define closedir(d) precompose_utf8_closedir(d)
#define DIR PREC_DIR
#endif /* PRECOMPOSE_UNICODE_C */

#endif /* PRECOMPOSE_UNICODE_H */
