/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Eric Biederman, 2005
 */
#include "builtin.h"
#include "attr.h"
#include "config.h"
#include "editor.h"
#include "ident.h"
#include "pager.h"
#include "refs.h"
#include "path.h"
#include "strbuf.h"

static const char var_usage[] = "git var (-l | <variable>)";

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
	const char *pgm = git_pager(1);

	if (!pgm)
		pgm = "cat";
	return xstrdup(pgm);
}

static char *default_branch(int ident_flag UNUSED)
{
	return xstrdup_or_null(git_default_branch_name(1));
}

static char *shell_path(int ident_flag UNUSED)
{
	return xstrdup(SHELL_PATH);
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

static char *git_config_val_global(int ident_flag UNUSED)
{
	struct strbuf buf = STRBUF_INIT;
	char *user, *xdg;
	size_t unused;

	git_global_config_paths(&user, &xdg);
	if (xdg && *xdg) {
		normalize_path_copy(xdg, xdg);
		strbuf_addf(&buf, "%s\n", xdg);
	}
	if (user && *user) {
		normalize_path_copy(user, user);
		strbuf_addf(&buf, "%s\n", user);
	}
	free(xdg);
	free(user);
	strbuf_trim_trailing_newline(&buf);
	if (buf.len == 0) {
		strbuf_release(&buf);
		return NULL;
	}
	return strbuf_detach(&buf, &unused);
}

struct git_var {
	const char *name;
	char *(*read)(int);
	int multivalued;
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
		.read = git_config_val_global,
		.multivalued = 1,
	},
	{
		.name = "",
		.read = NULL,
	},
};

static void list_vars(void)
{
	struct git_var *ptr;
	char *val;

	for (ptr = git_vars; ptr->read; ptr++)
		if ((val = ptr->read(0))) {
			if (ptr->multivalued && *val) {
				struct string_list list = STRING_LIST_INIT_DUP;
				int i;

				string_list_split(&list, val, '\n', -1);
				for (i = 0; i < list.nr; i++)
					printf("%s=%s\n", ptr->name, list.items[i].string);
				string_list_clear(&list, 0);
			} else {
				printf("%s=%s\n", ptr->name, val);
			}
			free(val);
		}
}

static const struct git_var *get_git_var(const char *var)
{
	struct git_var *ptr;
	for (ptr = git_vars; ptr->read; ptr++) {
		if (strcmp(var, ptr->name) == 0) {
			return ptr;
		}
	}
	return NULL;
}

static int show_config(const char *var, const char *value,
		       const struct config_context *ctx, void *cb)
{
	if (value)
		printf("%s=%s\n", var, value);
	else
		printf("%s\n", var);
	return git_default_config(var, value, ctx, cb);
}

int cmd_var(int argc, const char **argv, const char *prefix UNUSED)
{
	const struct git_var *git_var;
	char *val;

	if (argc != 2)
		usage(var_usage);

	if (strcmp(argv[1], "-l") == 0) {
		git_config(show_config, NULL);
		list_vars();
		return 0;
	}
	git_config(git_default_config, NULL);

	git_var = get_git_var(argv[1]);
	if (!git_var)
		usage(var_usage);

	val = git_var->read(IDENT_STRICT);
	if (!val)
		return 1;

	printf("%s\n", val);
	free(val);

	return 0;
}
