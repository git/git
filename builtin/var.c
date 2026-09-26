/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Eric Biederman, 2005
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"

#include "attr.h"
#include "config.h"
#include "editor.h"
#include "environment.h"
#include "ident.h"
#include "pager.h"
#include "parse-options.h"
#include "path.h"
#include "refs.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

static const char * const var_usage[] = {
	N_("git var [-z] -l"),
	N_("git var [-z] <variable>"),
	NULL
};

static char *committer(int ident_flag)
{
	return xstrdup_or_null(git_committer_info(ident_flag));
}

static char *author(int ident_flag)
{
	return xstrdup_or_null(git_author_info(ident_flag));
}

static char *editor(int ident_flag UNUSED)
{
	return xstrdup_or_null(git_editor());
}

static char *sequence_editor(int ident_flag UNUSED)
{
	return xstrdup_or_null(git_sequence_editor());
}

static char *pager(int ident_flag UNUSED)
{
	const char *pgm = git_pager(the_repository, 1);

	if (!pgm)
		pgm = "cat";
	return xstrdup(pgm);
}

static char *default_branch(int ident_flag UNUSED)
{
	return repo_default_branch_name(the_repository, 1);
}

static char *shell_path(int ident_flag UNUSED)
{
	return git_shell_path();
}

static char *git_attr_val_system(int ident_flag UNUSED)
{
	if (git_attr_system_is_enabled()) {
		char *file = xstrdup(git_attr_system_file());
		normalize_path_copy(file, file);
		return file;
	}
	return NULL;
}

static char *git_attr_val_global(int ident_flag UNUSED)
{
	char *file = xstrdup_or_null(git_attr_global_file());
	if (file) {
		normalize_path_copy(file, file);
		return file;
	}
	return NULL;
}

static char *git_config_val_system(int ident_flag UNUSED)
{
	if (git_config_system()) {
		char *file = git_system_config();
		normalize_path_copy(file, file);
		return file;
	}
	return NULL;
}

static void git_config_val_global(struct string_list *list)
{
	char *user, *xdg;

	git_global_config_paths(&user, &xdg);
	if (xdg && *xdg) {
		normalize_path_copy(xdg, xdg);
		string_list_append(list, xdg);
	}
	if (user && *user) {
		normalize_path_copy(user, user);
		string_list_append(list, user);
	}
	free(xdg);
	free(user);
}

struct git_var {
	const char *name;
	char *(*read)(int);
	void (*multiread)(struct string_list *);
};
static struct git_var git_vars[] = {
	{
		.name = "GIT_COMMITTER_IDENT",
		.read = committer,
	},
	{
		.name = "GIT_AUTHOR_IDENT",
		.read = author,
	},
	{
		.name = "GIT_EDITOR",
		.read = editor,
	},
	{
		.name = "GIT_SEQUENCE_EDITOR",
		.read = sequence_editor,
	},
	{
		.name = "GIT_PAGER",
		.read = pager,
	},
	{
		.name = "GIT_DEFAULT_BRANCH",
		.read = default_branch,
	},
	{
		.name = "GIT_SHELL_PATH",
		.read = shell_path,
	},
	{
		.name = "GIT_ATTR_SYSTEM",
		.read = git_attr_val_system,
	},
	{
		.name = "GIT_ATTR_GLOBAL",
		.read = git_attr_val_global,
	},
	{
		.name = "GIT_CONFIG_SYSTEM",
		.read = git_config_val_system,
	},
	{
		.name = "GIT_CONFIG_GLOBAL",
		.multiread = git_config_val_global,
	},
	{
		.name = "",
		.read = NULL,
	},
};

static void list_vars(int nul_term)
{
	struct git_var *ptr;
	char delim = nul_term ? '\n' : '=';
	char term = nul_term ? '\0' : '\n';

	for (ptr = git_vars; ptr->read || ptr->multiread; ptr++) {
		if (ptr->read) {
			char *val = ptr->read(0);

			if (val) {
				printf("%s%c%s%c", ptr->name, delim, val, term);
				free(val);
			}
		} else {
			struct string_list list = STRING_LIST_INIT_DUP;

			ptr->multiread(&list);
			for (size_t i = 0; i < list.nr; i++)
				printf("%s%c%s%c", ptr->name, delim,
				       list.items[i].string, term);
			string_list_clear(&list, 0);
		}
	}
}

static const struct git_var *get_git_var(const char *var)
{
	struct git_var *ptr;
	for (ptr = git_vars; ptr->read || ptr->multiread; ptr++) {
		if (strcmp(var, ptr->name) == 0) {
			return ptr;
		}
	}
	return NULL;
}

static int show_config(const char *var, const char *value,
		       const struct config_context *ctx, void *cb)
{
	int *nul_term = cb;
	char delim = *nul_term ? '\n' : '=';
	char term = *nul_term ? '\0' : '\n';

	if (value)
		printf("%s%c%s%c", var, delim, value, term);
	else
		printf("%s%c", var, term);
	return git_default_config(var, value, ctx, cb);
}

int cmd_var(int argc,
	    const char **argv,
	    const char *prefix,
	    struct repository *repo UNUSED)
{
	const struct git_var *git_var;
	int list = 0;
	int nul_term = 0;
	char term;
	struct option options[] = {
		OPT_BOOL('l', NULL, &list,
			 N_("list all variables")),
		OPT_BOOL('z', NULL, &nul_term,
			 N_("terminate entries with NUL")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options,
			     var_usage, PARSE_OPT_STOP_AT_NON_OPTION);

	if (list) {
		if (argc)
			usage_with_options(var_usage, options);
		repo_config(the_repository, show_config, &nul_term);
		list_vars(nul_term);
		return 0;
	}

	if (argc != 1)
		usage_with_options(var_usage, options);

	repo_config(the_repository, git_default_config, NULL);

	term = nul_term ? '\0' : '\n';

	git_var = get_git_var(argv[0]);
	if (!git_var)
		usage_with_options(var_usage, options);

	if (git_var->read) {
		char *val = git_var->read(IDENT_STRICT);

		if (!val)
			return 1;

		printf("%s%c", val, term);
		free(val);
	} else {
		struct string_list list = STRING_LIST_INIT_DUP;

		git_var->multiread(&list);
		if (!list.nr) {
			string_list_clear(&list, 0);
			return 1;
		}
		for (size_t i = 0; i < list.nr; i++)
			printf("%s%c", list.items[i].string, term);
		string_list_clear(&list, 0);
	}

	return 0;
}
