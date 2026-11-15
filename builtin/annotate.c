/*
 * "git annotate" builtin alias
 *
 * Copyright (C) 2006 Ryan Anderson
 */

#include "git-compat-util.h"
#include "builtin.h"

int cmd_annotate(int argc,
		 const char **argv,
		 const char *prefix,
		 struct repository *repo)
{
	return cmd_blame(argc, argv, prefix, repo);
}
