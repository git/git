#include "../git-compat-util.h"
#include "win32.h"
#include <conio.h>
#include "../strbuf.h"

DIR *opendir(const char *name)
{
	int len = strlen(name);
	DIR *p;
	p = malloc(sizeof(DIR) + len + 2);
	if (!p)
		return NULL;

	memset(p, 0, sizeof(DIR) + len + 2);
	strcpy(p->dd_name, name);
	p->dd_name[len] = '/';
	p->dd_name[len+1] = '*';

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
