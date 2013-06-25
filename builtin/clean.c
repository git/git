/*
 * "git clean" builtin command
 *
 * Copyright (C) 2007 Shawn Bohrer
 *
 * Based on git-clean.sh by Pavel Roskin
 */

#include "builtin.h"
#include "cache.h"
#include "dir.h"
#include "parse-options.h"
#include "refs.h"
#include "string-list.h"
#include "quote.h"
#include "column.h"
#include "color.h"

static int force = -1; /* unset */
static int interactive;
static struct string_list del_list = STRING_LIST_INIT_DUP;
static unsigned int colopts;

static const char *const builtin_clean_usage[] = {
	N_("git clean [-d] [-f] [-i] [-n] [-q] [-e <pattern>] [-x | -X] [--] <paths>..."),
	NULL
};

static const char *msg_remove = N_("Removing %s\n");
static const char *msg_would_remove = N_("Would remove %s\n");
static const char *msg_skip_git_dir = N_("Skipping repository %s\n");
static const char *msg_would_skip_git_dir = N_("Would skip repository %s\n");
static const char *msg_warn_remove_failed = N_("failed to remove %s");

static int clean_use_color = -1;
static char clean_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_NORMAL,	/* PLAIN */
	GIT_COLOR_BOLD_BLUE,	/* PROMPT */
	GIT_COLOR_BOLD,		/* HEADER */
	GIT_COLOR_BOLD_RED,	/* HELP */
	GIT_COLOR_BOLD_RED,	/* ERROR */
};
enum color_clean {
	CLEAN_COLOR_RESET = 0,
	CLEAN_COLOR_PLAIN = 1,
	CLEAN_COLOR_PROMPT = 2,
	CLEAN_COLOR_HEADER = 3,
	CLEAN_COLOR_HELP = 4,
	CLEAN_COLOR_ERROR = 5,
};

static int parse_clean_color_slot(const char *var)
{
	if (!strcasecmp(var, "reset"))
		return CLEAN_COLOR_RESET;
	if (!strcasecmp(var, "plain"))
		return CLEAN_COLOR_PLAIN;
	if (!strcasecmp(var, "prompt"))
		return CLEAN_COLOR_PROMPT;
	if (!strcasecmp(var, "header"))
		return CLEAN_COLOR_HEADER;
	if (!strcasecmp(var, "help"))
		return CLEAN_COLOR_HELP;
	if (!strcasecmp(var, "error"))
		return CLEAN_COLOR_ERROR;
	return -1;
}

static int git_clean_config(const char *var, const char *value, void *cb)
{
	if (!prefixcmp(var, "column."))
		return git_column_config(var, value, "clean", &colopts);

	/* honors the color.interactive* config variables which also
	   applied in git-add--interactive and git-stash */
	if (!strcmp(var, "color.interactive")) {
		clean_use_color = git_config_colorbool(var, value);
		return 0;
	}
	if (!prefixcmp(var, "color.interactive.")) {
		int slot = parse_clean_color_slot(var +
						  strlen("color.interactive."));
		if (slot < 0)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		color_parse(value, var, clean_colors[slot]);
		return 0;
	}

	if (!strcmp(var, "clean.requireforce")) {
		force = !git_config_bool(var, value);
		return 0;
	}

	/* inspect the color.ui config variable and others */
	return git_color_default_config(var, value, cb);
}

static const char *clean_get_color(enum color_clean ix)
{
	if (want_color(clean_use_color))
		return clean_colors[ix];
	return "";
}

static void clean_print_color(enum color_clean ix)
{
	printf("%s", clean_get_color(ix));
}

static int exclude_cb(const struct option *opt, const char *arg, int unset)
{
	struct string_list *exclude_list = opt->value;
	string_list_append(exclude_list, arg);
	return 0;
}

static int remove_dirs(struct strbuf *path, const char *prefix, int force_flag,
		int dry_run, int quiet, int *dir_gone)
{
	DIR *dir;
	struct strbuf quoted = STRBUF_INIT;
	struct dirent *e;
	int res = 0, ret = 0, gone = 1, original_len = path->len, len, i;
	unsigned char submodule_head[20];
	struct string_list dels = STRING_LIST_INIT_DUP;

	*dir_gone = 1;

	if ((force_flag & REMOVE_DIR_KEEP_NESTED_GIT) &&
			!resolve_gitlink_ref(path->buf, "HEAD", submodule_head)) {
		if (!quiet) {
			quote_path_relative(path->buf, prefix, &quoted);
			printf(dry_run ?  _(msg_would_skip_git_dir) : _(msg_skip_git_dir),
					quoted.buf);
		}

		*dir_gone = 0;
		return 0;
	}

	dir = opendir(path->buf);
	if (!dir) {
		/* an empty dir could be removed even if it is unreadble */
		res = dry_run ? 0 : rmdir(path->buf);
		if (res) {
			quote_path_relative(path->buf, prefix, &quoted);
			warning(_(msg_warn_remove_failed), quoted.buf);
			*dir_gone = 0;
		}
		return res;
	}

	if (path->buf[original_len - 1] != '/')
		strbuf_addch(path, '/');

	len = path->len;
	while ((e = readdir(dir)) != NULL) {
		struct stat st;
		if (is_dot_or_dotdot(e->d_name))
			continue;

		strbuf_setlen(path, len);
		strbuf_addstr(path, e->d_name);
		if (lstat(path->buf, &st))
			; /* fall thru */
		else if (S_ISDIR(st.st_mode)) {
			if (remove_dirs(path, prefix, force_flag, dry_run, quiet, &gone))
				ret = 1;
			if (gone) {
				quote_path_relative(path->buf, prefix, &quoted);
				string_list_append(&dels, quoted.buf);
			} else
				*dir_gone = 0;
			continue;
		} else {
			res = dry_run ? 0 : unlink(path->buf);
			if (!res) {
				quote_path_relative(path->buf, prefix, &quoted);
				string_list_append(&dels, quoted.buf);
			} else {
				quote_path_relative(path->buf, prefix, &quoted);
				warning(_(msg_warn_remove_failed), quoted.buf);
				*dir_gone = 0;
				ret = 1;
			}
			continue;
		}

		/* path too long, stat fails, or non-directory still exists */
		*dir_gone = 0;
		ret = 1;
		break;
	}
	closedir(dir);

	strbuf_setlen(path, original_len);

	if (*dir_gone) {
		res = dry_run ? 0 : rmdir(path->buf);
		if (!res)
			*dir_gone = 1;
		else {
			quote_path_relative(path->buf, prefix, &quoted);
			warning(_(msg_warn_remove_failed), quoted.buf);
			*dir_gone = 0;
			ret = 1;
		}
	}

	if (!*dir_gone && !quiet) {
		for (i = 0; i < dels.nr; i++)
			printf(dry_run ?  _(msg_would_remove) : _(msg_remove), dels.items[i].string);
	}
	string_list_clear(&dels, 0);
	return ret;
}

static void pretty_print_dels(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct strbuf buf = STRBUF_INIT;
	const char *qname;
	struct column_options copts;

	for_each_string_list_item(item, &del_list) {
		qname = quote_path_relative(item->string, NULL, &buf);
		string_list_append(&list, qname);
	}

	/*
	 * always enable column display, we only consult column.*
	 * about layout strategy and stuff
	 */
	colopts = (colopts & ~COL_ENABLE_MASK) | COL_ENABLED;
	memset(&copts, 0, sizeof(copts));
	copts.indent = "  ";
	copts.padding = 2;
	print_columns(&list, colopts, &copts);
	putchar('\n');
	strbuf_release(&buf);
	string_list_clear(&list, 0);
}

static void interactive_main_loop(void)
{
	struct strbuf confirm = STRBUF_INIT;

	while (del_list.nr) {
		putchar('\n');
		clean_print_color(CLEAN_COLOR_HEADER);
		printf_ln(Q_("Would remove the following item:",
			     "Would remove the following items:",
			     del_list.nr));
		clean_print_color(CLEAN_COLOR_RESET);
		putchar('\n');

		pretty_print_dels();

		clean_print_color(CLEAN_COLOR_PROMPT);
		printf(_("Remove [y/n]? "));
		clean_print_color(CLEAN_COLOR_RESET);
		if (strbuf_getline(&confirm, stdin, '\n') != EOF) {
			strbuf_trim(&confirm);
		} else {
			/* Ctrl-D is the same as "quit" */
			string_list_clear(&del_list, 0);
			putchar('\n');
			printf_ln("Bye.");
			break;
		}

		if (confirm.len) {
			if (!strncasecmp(confirm.buf, "yes", confirm.len)) {
				break;
			} else if (!strncasecmp(confirm.buf, "no", confirm.len) ||
				   !strncasecmp(confirm.buf, "quit", confirm.len)) {
				string_list_clear(&del_list, 0);
				printf_ln("Bye.");
				break;
			} else {
				continue;
			}
		}
	}

	strbuf_release(&confirm);
}

int cmd_clean(int argc, const char **argv, const char *prefix)
{
	int i, res;
	int dry_run = 0, remove_directories = 0, quiet = 0, ignored = 0;
	int ignored_only = 0, config_set = 0, errors = 0, gone = 1;
	int rm_flags = REMOVE_DIR_KEEP_NESTED_GIT;
	struct strbuf abs_path = STRBUF_INIT;
	struct dir_struct dir;
	static const char **pathspec;
	struct strbuf buf = STRBUF_INIT;
	struct string_list exclude_list = STRING_LIST_INIT_NODUP;
	struct exclude_list *el;
	struct string_list_item *item;
	const char *qname;
	char *seen = NULL;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("do not print names of files removed")),
		OPT__DRY_RUN(&dry_run, N_("dry run")),
		OPT__FORCE(&force, N_("force")),
		OPT_BOOL('i', "interactive", &interactive, N_("interactive cleaning")),
		OPT_BOOLEAN('d', NULL, &remove_directories,
				N_("remove whole directories")),
		{ OPTION_CALLBACK, 'e', "exclude", &exclude_list, N_("pattern"),
		  N_("add <pattern> to ignore rules"), PARSE_OPT_NONEG, exclude_cb },
		OPT_BOOLEAN('x', NULL, &ignored, N_("remove ignored files, too")),
		OPT_BOOLEAN('X', NULL, &ignored_only,
				N_("remove only ignored files")),
		OPT_END()
	};

	git_config(git_clean_config, NULL);
	if (force < 0)
		force = 0;
	else
		config_set = 1;

	argc = parse_options(argc, argv, prefix, options, builtin_clean_usage,
			     0);

	memset(&dir, 0, sizeof(dir));
	if (ignored_only)
		dir.flags |= DIR_SHOW_IGNORED;

	if (ignored && ignored_only)
		die(_("-x and -X cannot be used together"));

	if (!interactive && !dry_run && !force) {
		if (config_set)
			die(_("clean.requireForce set to true and neither -i, -n nor -f given; "
				  "refusing to clean"));
		else
			die(_("clean.requireForce defaults to true and neither -i, -n nor -f given; "
				  "refusing to clean"));
	}

	if (force > 1)
		rm_flags = 0;

	dir.flags |= DIR_SHOW_OTHER_DIRECTORIES;

	if (read_cache() < 0)
		die(_("index file corrupt"));

	if (!ignored)
		setup_standard_excludes(&dir);

	el = add_exclude_list(&dir, EXC_CMDL, "--exclude option");
	for (i = 0; i < exclude_list.nr; i++)
		add_exclude(exclude_list.items[i].string, "", 0, el, -(i+1));

	pathspec = get_pathspec(prefix, argv);

	fill_directory(&dir, pathspec);

	if (pathspec)
		seen = xmalloc(argc > 0 ? argc : 1);

	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		int len, pos;
		int matches = 0;
		struct cache_entry *ce;
		struct stat st;
		const char *rel;

		/*
		 * Remove the '/' at the end that directory
		 * walking adds for directory entries.
		 */
		len = ent->len;
		if (len && ent->name[len-1] == '/')
			len--;
		pos = cache_name_pos(ent->name, len);
		if (0 <= pos)
			continue;	/* exact match */
		pos = -pos - 1;
		if (pos < active_nr) {
			ce = active_cache[pos];
			if (ce_namelen(ce) == len &&
			    !memcmp(ce->name, ent->name, len))
				continue; /* Yup, this one exists unmerged */
		}

		if (lstat(ent->name, &st))
			die_errno("Cannot lstat '%s'", ent->name);

		if (pathspec) {
			memset(seen, 0, argc > 0 ? argc : 1);
			matches = match_pathspec(pathspec, ent->name, len,
						 0, seen);
		}

		if (S_ISDIR(st.st_mode)) {
			if (remove_directories || (matches == MATCHED_EXACTLY)) {
				rel = relative_path(ent->name, prefix, &buf);
				string_list_append(&del_list, rel);
			}
		} else {
			if (pathspec && !matches)
				continue;
			rel = relative_path(ent->name, prefix, &buf);
			string_list_append(&del_list, rel);
		}
	}

	if (interactive && del_list.nr > 0)
		interactive_main_loop();

	for_each_string_list_item(item, &del_list) {
		struct stat st;

		if (prefix)
			strbuf_addstr(&abs_path, prefix);

		strbuf_addstr(&abs_path, item->string);

		/*
		 * we might have removed this as part of earlier
		 * recursive directory removal, so lstat() here could
		 * fail with ENOENT.
		 */
		if (lstat(abs_path.buf, &st))
			continue;

		if (S_ISDIR(st.st_mode)) {
			if (remove_dirs(&abs_path, prefix, rm_flags, dry_run, quiet, &gone))
				errors++;
			if (gone && !quiet) {
				qname = quote_path_relative(item->string, NULL, &buf);
				printf(dry_run ? _(msg_would_remove) : _(msg_remove), qname);
			}
		} else {
			res = dry_run ? 0 : unlink(abs_path.buf);
			if (res) {
				qname = quote_path_relative(item->string, NULL, &buf);
				warning(_(msg_warn_remove_failed), qname);
				errors++;
			} else if (!quiet) {
				qname = quote_path_relative(item->string, NULL, &buf);
				printf(dry_run ? _(msg_would_remove) : _(msg_remove), qname);
			}
		}
		strbuf_reset(&abs_path);
	}
	free(seen);

	strbuf_release(&abs_path);
	strbuf_release(&buf);
	string_list_clear(&del_list, 0);
	string_list_clear(&exclude_list, 0);
	return (errors != 0);
}
