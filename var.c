/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Eric Biederman, 2005
 */
#include "cache.h"

static const char var_usage[] = "git var [-l | <variable>]";

struct git_var {
	const char *name;
	const char *(*read)(int);
};
static struct git_var git_vars[] = {
	{ "GIT_COMMITTER_IDENT", git_committer_info },
	{ "GIT_AUTHOR_IDENT",   git_author_info },
	{ "", NULL },
};

static void list_vars(void)
{
	struct git_var *ptr;
	for(ptr = git_vars; ptr->read; ptr++) {
		printf("%s=%s\n", ptr->name, ptr->read(IDENT_WARN_ON_NO_NAME));
	}
}

static const char *read_var(const char *var)
{
	struct git_var *ptr;
	const char *val;
	val = NULL;
	for(ptr = git_vars; ptr->read; ptr++) {
		if (strcmp(var, ptr->name) == 0) {
			val = ptr->read(IDENT_ERROR_ON_NO_NAME);
			break;
		}
	}
	return val;
}

static int show_config(const char *var, const char *value, void *cb)
{
	if (value)
		printf("%s=%s\n", var, value);
	else
		printf("%s\n", var);
	return git_default_config(var, value, cb);
}

int main(int argc, char **argv)
{
	const char *val;
	int nongit;
	if (argc != 2) {
		usage(var_usage);
	}

	setup_git_directory_gently(&nongit);
	val = NULL;

	if (strcmp(argv[1], "-l") == 0) {
		git_config(show_config, NULL);
		list_vars();
		return 0;
	}
	git_config(git_default_config, NULL);
	val = read_var(argv[1]);
	if (!val)
		usage(var_usage);

	printf("%s\n", val);

	return 0;
}
