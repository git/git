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
	 * Start with room for NAME_MAX + 1 bytes, but keep d_name as a
	 * flexible array. Some systems have NAME_MAX=255 while strlen(d_name)
	 * from readdir() may return 508 or 510 bytes. Grow the allocation as
	 * needed in precompose_utf8_readdir().
	 */
	char   d_name[FLEX_ARRAY];
} dirent_prec_psx;


typedef struct {
	iconv_t ic_precompose;
	DIR *dirp;
	struct dirent_prec_psx *dirent_nfc;
} PREC_DIR;

const char *precompose_argv_prefix(int argc, const char **argv, const char *prefix);
const char *precompose_string_if_needed(const char *in);
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
