#include "exec_cmd.h"
#include <locale.h>
#include <libintl.h>
#include <stdlib.h>

extern void git_setup_gettext(void) {
	char *podir;
	char *envdir = getenv("GIT_TEXTDOMAINDIR");

	if (envdir) {
		(void)bindtextdomain("git", envdir);
	} else {
		podir = (char *)system_path("share/locale");
		if (!podir) return;
		(void)bindtextdomain("git", podir);
		free(podir);
	}

	(void)setlocale(LC_MESSAGES, "");
	(void)textdomain("git");
}
