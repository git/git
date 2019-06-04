/*
 *  The order of the following two lines is important.
 *
 *  SUPPRESS_FOPEN_REDEFINITION is defined before including git-compat-util.h
 *  to avoid the redefinition of fopen within git-compat-util.h. This is
 *  necessary since fopen is a macro on some platforms which may be set
 *  based on compiler options. For example, on AIX fopen is set to fopen64
 *  when _LARGE_FILES is defined. The previous technique of merely undefining
 *  fopen after including git-compat-util.h is inadequate in this case.
 */
#define SUPPRESS_FOPEN_REDEFINITION
#include "../git-compat-util.h"

FILE *git_fopen(const char *path, const char *mode)
{
	FILE *fp;
	struct stat st;

	if (mode[0] == 'w' || mode[0] == 'a')
		return fopen(path, mode);

	if (!(fp = fopen(path, mode)))
		return NULL;

	if (fstat(fileno(fp), &st)) {
		fclose(fp);
		return NULL;
	}

	if (S_ISDIR(st.st_mode)) {
		fclose(fp);
		errno = EISDIR;
		return NULL;
	}

	return fp;
}
