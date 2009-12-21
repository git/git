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

void show_chm_page(const char *git_cmd)
{
	//const char *page = cmd_to_page(git_cmd);
	struct strbuf page_path; /* it leaks but we exec bellow */

	struct stat st;
	const char *git_path = system_path(GIT_EXEC_PATH);
	int i;

	/* Check that we have a git documentation directory. */
	if (stat(mkpath("%s/TortoiseGit_en.chm", git_path), &st)
	    || !S_ISREG(st.st_mode))
		die("'%s': not a documentation directory.", git_path);

	strbuf_init(&page_path, 0);
	strbuf_addf(&page_path, "%s/TortoiseGit_en.chm::/git-%s(1).html", git_path, git_cmd);
	
	ShellExecute(NULL, "open","hh.exe", page_path.buf,NULL, SW_SHOW);
}
