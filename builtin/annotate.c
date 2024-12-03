/*
 * "git annotate" builtin alias
 *
 * Copyright (C) 2006 Ryan Anderson
 */

#include "git-compat-util.h"
#include "builtin.h"
#include "strvec.h"

int cmd_annotate(int argc,
		 const char **argv,
		 const char *prefix,
		 struct repository *repo)
{
	struct strvec args = STRVEC_INIT;
	const char **args_copy;
	int ret;

	strvec_pushl(&args, "annotate", "-c", NULL);
	for (int i = 1; i < argc; i++)
		strvec_push(&args, argv[i]);

	/*
	 * `cmd_blame()` ends up modifying the array, which causes memory leaks
	 * if we didn't copy the array here.
	 */
	CALLOC_ARRAY(args_copy, args.nr + 1);
	COPY_ARRAY(args_copy, args.v, args.nr);

	ret = cmd_blame(args.nr, args_copy, prefix, repo);

	strvec_clear(&args);
	free(args_copy);
	return ret;
}
