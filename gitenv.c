/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"

/*
 * This array must be sorted by its canonical name, because
 * we do look-up by binary search.
 */
static struct backward_compatible_env {
	const char *canonical;
	const char *old;
} bc_name[] = {
	{ "GIT_ALTERNATE_OBJECT_DIRECTORIES", "SHA1_FILE_DIRECTORIES" },
	{ "GIT_AUTHOR_DATE", "AUTHOR_DATE" },
	{ "GIT_AUTHOR_EMAIL", "AUTHOR_EMAIL" },
	{ "GIT_AUTHOR_NAME", "AUTHOR_NAME" }, 
	{ "GIT_COMMITTER_EMAIL", "COMMIT_AUTHOR_EMAIL" },
	{ "GIT_COMMITTER_NAME", "COMMIT_AUTHOR_NAME" },
	{ "GIT_OBJECT_DIRECTORY", "SHA1_FILE_DIRECTORY" },
};

static void warn_old_environment(int pos)
{
	int i;
	static int warned = 0;
	if (warned)
		return;

	warned = 1;
	fprintf(stderr,
		"warning: Attempting to use %s\n",
		bc_name[pos].old);
	fprintf(stderr,
		"warning: GIT environment variables have been renamed.\n"
		"warning: Please adjust your scripts and environment.\n");
	for (i = 0; i < sizeof(bc_name) / sizeof(bc_name[0]); i++) {
		/* warning is needed only when old name is there and
		 * new name is not.
		 */
		if (!getenv(bc_name[i].canonical) && getenv(bc_name[i].old))
			fprintf(stderr, "warning: old %s => new %s\n",
				bc_name[i].old, bc_name[i].canonical);
	}
}

char *gitenv_bc(const char *e)
{
	int first, last;
	char *val = getenv(e);
	if (val)
		die("gitenv_bc called on existing %s; fix the caller.", e);

	first = 0;
	last = sizeof(bc_name) / sizeof(bc_name[0]);
	while (last > first) {
		int next = (last + first) >> 1;
		int cmp = strcmp(e, bc_name[next].canonical);
		if (!cmp) {
			val = getenv(bc_name[next].old);
			/* If the user has only old name, warn.
			 * otherwise stay silent.
			 */
			if (val)
				warn_old_environment(next);
			return val;
		}
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	return NULL;
}
