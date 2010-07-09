#include "../git-compat-util.h"
#include "win32.h"
#include <conio.h>
#include "../strbuf.h"

DIR *opendir(const char *name)
{
	int len;
	DIR *p;
	p = (DIR*)malloc(sizeof(DIR));
	memset(p, 0, sizeof(DIR));
	strncpy(p->dd_name, name, PATH_MAX);
	len = strlen(p->dd_name);
	p->dd_name[len] = '/';
	p->dd_name[len+1] = '*';

	if (p == NULL)
		return NULL;

	p->dd_handle = _findfirst(p->dd_name, &p->dd_dta);

	if (p->dd_handle == -1) {
		free(p);
		return NULL;
	}
	return p;
}
int closedir(DIR *dir)
{
	_findclose(dir->dd_handle);
	free(dir);
	return 0;
}

#include "mingw.c"
