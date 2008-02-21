#include "../git-compat-util.h"
#undef fopen
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
