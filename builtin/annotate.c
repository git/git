/*
 * "git annotate" builtin alias
 *
 * Copyright (C) 2006 Ryan Anderson
 */
#include "git-compat-util.h"
#include "builtin.h"

int cmd_annotate(int argc, const char **argv, const char *prefix)
{
	const char **nargv;
	int i;
	nargv = xmalloc(sizeof(char *) * (argc + 2));

	nargv[0] = "annotate";
	nargv[1] = "-c";

	for (i = 1; i < argc; i++) {
		nargv[i+1] = argv[i];
	}
	nargv[argc + 1] = NULL;

	return cmd_blame(argc + 1, nargv, prefix);
}
