#ifndef TAR_H
#define TAR_H

#define TYPEFLAG_AUTO		'\0'
#define TYPEFLAG_REG		'0'
#define TYPEFLAG_LNK		'2'
#define TYPEFLAG_DIR		'5'
#define TYPEFLAG_GLOBAL_HEADER	'g'
#define TYPEFLAG_EXT_HEADER	'x'

struct ustar_header {
	char name[100];		/*   0 */
	char mode[8];		/* 100 */
	char uid[8];		/* 108 */
	char gid[8];		/* 116 */
	char size[12];		/* 124 */
	char mtime[12];		/* 136 */
	char chksum[8];		/* 148 */
	char typeflag[1];	/* 156 */
	char linkname[100];	/* 157 */
	char magic[6];		/* 257 */
	char version[2];	/* 263 */
	char uname[32];		/* 265 */
	char gname[32];		/* 297 */
	char devmajor[8];	/* 329 */
	char devminor[8];	/* 337 */
	char prefix[155];	/* 345 */
};

#endif /* TAR_H */
