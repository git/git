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
#include "gpg-interface.h"
#include "ident.h"
#include "pager.h"
#include "refs.h"
#include "path.h"
#include "strbuf.h"
#include "strvec.h"
#include "run-command.h"

static const char var_usage[] = "git var (-l [-z] | [-z] <variable>...)";

static char *committer(int ident_flag)
{
	return xstrdup_or_null(git_committer_info(ident_flag));
}

static char *ident_part(const char *ident, char part)
{
	struct ident_split split;

	if (!ident)
		return NULL;
	if (split_ident_line(&split, ident, strlen(ident)))
		return NULL;

	switch (part) {
	case 'n':
		if (!split.name_begin || !split.name_end)
			return NULL;
		return xmemdupz(split.name_begin, split.name_end - split.name_begin);
	case 'e':
		if (!split.mail_begin || !split.mail_end)
			return NULL;
		return xmemdupz(split.mail_begin, split.mail_end - split.mail_begin);
	case 'd':
		if (!split.date_begin)
			return NULL;
		if (split.tz_end)
			return xmemdupz(split.date_begin, split.tz_end - split.date_begin);
		if (split.date_end)
			return xmemdupz(split.date_begin, split.date_end - split.date_begin);
		return NULL;
	default:
		return NULL;
	}
}

static char *committer_name(int ident_flag)
{
	return ident_part(git_committer_info(ident_flag), 'n');
}

static char *committer_email(int ident_flag)
{
	return ident_part(git_committer_info(ident_flag), 'e');
}

static char *committer_date(int ident_flag)
{
	return ident_part(git_committer_info(ident_flag), 'd');
}

static char *author(int ident_flag)
{
	return xstrdup_or_null(git_author_info(ident_flag));
}

static char *author_name(int ident_flag)
{
	return ident_part(git_author_info(ident_flag), 'n');
}

static char *author_email(int ident_flag)
{
	return ident_part(git_author_info(ident_flag), 'e');
}

static char *author_date(int ident_flag)
{
	return ident_part(git_author_info(ident_flag), 'd');
}

static char *default_key(int ident_flag UNUSED)
{
	int gpgsign = 0;
	char *signing_key = NULL;

	if (repo_config_get_string(the_repository, "user.signingkey", &signing_key) == 0 && signing_key && *signing_key)
		return signing_key;
	free(signing_key);

	if (repo_config_get_bool(the_repository, "commit.gpgsign", &gpgsign) == 0 && gpgsign)
		return get_signing_key_id();

	return NULL;
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
		.name = "GIT_COMMITTER_NAME",
		.read = committer_name,
	},
	{
		.name = "GIT_COMMITTER_EMAIL",
		.read = committer_email,
	},
	{
		.name = "GIT_COMMITTER_DATE",
		.read = committer_date,
	},
	{
		.name = "GIT_AUTHOR_IDENT",
		.read = author,
	},
	{
		.name = "GIT_AUTHOR_NAME",
		.read = author_name,
	},
	{
		.name = "GIT_AUTHOR_EMAIL",
		.read = author_email,
	},
	{
		.name = "GIT_AUTHOR_DATE",
		.read = author_date,
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
		.name = "GIT_DEFAULT_KEY",
		.read = default_key,
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

static void list_vars(int null_term)
{
	struct git_var *ptr;
	char *val;
	char eol = null_term ? '\0' : '\n';

	for (ptr = git_vars; ptr->read; ptr++)
		if ((val = ptr->read(0))) {
			if (ptr->multivalued && *val) {
				struct string_list list = STRING_LIST_INIT_DUP;

				string_list_split(&list, val, "\n", -1);
				for (size_t i = 0; i < list.nr; i++)
					printf("%s=%s%c", ptr->name, list.items[i].string, eol);
				string_list_clear(&list, 0);
			} else {
				printf("%s=%s%c", ptr->name, val, eol);
			}
			free(val);
		}
}

static const struct git_var *get_git_var(const char *var)
{
	struct git_var *ptr;
	if (!strcmp(var, "GIT_SIGNING_KEY"))
		var = "GIT_DEFAULT_KEY";
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
	int null_term = cb ? *(int *)cb : 0;
	char eol = null_term ? '\0' : '\n';

	if (value)
		printf("%s=%s%c", var, value, eol);
	else
		printf("%s%c", var, eol);
	return git_default_config(var, value, ctx, cb);
}

int cmd_var(int argc,
	    const char **argv,
	    const char *prefix UNUSED,
	    struct repository *repo UNUSED)
{
	struct strvec vars = STRVEC_INIT;
	int list = 0;
	int null_term = 0;
	int i;

	show_usage_if_asked(argc, argv, var_usage);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "-l")) {
			list = 1;
		} else if (!strcmp(arg, "-z")) {
			null_term = 1;
		} else if (!strcmp(arg, "--")) {
			for (i = i + 1; i < argc; i++)
				strvec_push(&vars, argv[i]);
			break;
		} else if (arg[0] == '-') {
			usage(var_usage);
		} else {
			strvec_push(&vars, arg);
		}
	}

	if (list) {
		if (vars.nr > 0) {
			strvec_clear(&vars);
			usage(var_usage);
		}
		repo_config(the_repository, show_config, &null_term);
		list_vars(null_term);
		return 0;
	}

	if (!vars.nr)
		usage(var_usage);

	repo_config(the_repository, git_default_config, NULL);

	for (size_t j = 0; j < vars.nr; j++) {
		const struct git_var *git_var = get_git_var(vars.v[j]);
		char *val;

		if (!git_var) {
			strvec_clear(&vars);
			usage(var_usage);
		}

		val = git_var->read(IDENT_STRICT);
		if (!val) {
			strvec_clear(&vars);
			return 1;
		}

		printf("%s%c", val, null_term ? '\0' : '\n');
		free(val);
	}

	strvec_clear(&vars);
	return 0;
}
