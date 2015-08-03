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
#include "string-list.h"
#include "quote.h"
#include "column.h"
#include "color.h"
#include "pathspec.h"

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
	CLEAN_COLOR_ERROR = 5
};

#define MENU_OPTS_SINGLETON		01
#define MENU_OPTS_IMMEDIATE		02
#define MENU_OPTS_LIST_ONLY		04

struct menu_opts {
	const char *header;
	const char *prompt;
	int flags;
};

#define MENU_RETURN_NO_LOOP		10

struct menu_item {
	char hotkey;
	const char *title;
	int selected;
	int (*fn)(void);
};

enum menu_stuff_type {
	MENU_STUFF_TYPE_STRING_LIST = 1,
	MENU_STUFF_TYPE_MENU_ITEM
};

struct menu_stuff {
	enum menu_stuff_type type;
	int nr;
	void *stuff;
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
	const char *slot_name;

	if (starts_with(var, "column."))
		return git_column_config(var, value, "clean", &colopts);

	/* honors the color.interactive* config variables which also
	   applied in git-add--interactive and git-stash */
	if (!strcmp(var, "color.interactive")) {
		clean_use_color = git_config_colorbool(var, value);
		return 0;
	}
	if (skip_prefix(var, "color.interactive.", &slot_name)) {
		int slot = parse_clean_color_slot(slot_name);
		if (slot < 0)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		return color_parse(value, clean_colors[slot]);
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

/*
 * Return 1 if the given path is the root of a git repository or
 * submodule else 0. Will not return 1 for bare repositories with the
 * exception of creating a bare repository in "foo/.git" and calling
 * is_git_repository("foo").
 */
static int is_git_repository(struct strbuf *path)
{
	int ret = 0;
	int gitfile_error;
	size_t orig_path_len = path->len;
	assert(orig_path_len != 0);
	if (path->buf[orig_path_len - 1] != '/')
		strbuf_addch(path, '/');
	strbuf_addstr(path, ".git");
	if (read_gitfile_gently(path->buf, &gitfile_error) || is_git_directory(path->buf))
		ret = 1;
	if (gitfile_error == READ_GITFILE_ERR_OPEN_FAILED ||
	    gitfile_error == READ_GITFILE_ERR_READ_FAILED)
		ret = 1;  /* This could be a real .git file, take the
			   * safe option and avoid cleaning */
	strbuf_setlen(path, orig_path_len);
	return ret;
}

static int remove_dirs(struct strbuf *path, const char *prefix, int force_flag,
		int dry_run, int quiet, int *dir_gone)
{
	DIR *dir;
	struct strbuf quoted = STRBUF_INIT;
	struct dirent *e;
	int res = 0, ret = 0, gone = 1, original_len = path->len, len;
	struct string_list dels = STRING_LIST_INIT_DUP;

	*dir_gone = 1;

	if ((force_flag & REMOVE_DIR_KEEP_NESTED_GIT) && is_git_repository(path)) {
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
		int i;
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
	strbuf_release(&buf);
	string_list_clear(&list, 0);
}

static void pretty_print_menus(struct string_list *menu_list)
{
	unsigned int local_colopts = 0;
	struct column_options copts;

	local_colopts = COL_ENABLED | COL_ROW;
	memset(&copts, 0, sizeof(copts));
	copts.indent = "  ";
	copts.padding = 2;
	print_columns(menu_list, local_colopts, &copts);
}

static void prompt_help_cmd(int singleton)
{
	clean_print_color(CLEAN_COLOR_HELP);
	printf_ln(singleton ?
		  _("Prompt help:\n"
		    "1          - select a numbered item\n"
		    "foo        - select item based on unique prefix\n"
		    "           - (empty) select nothing") :
		  _("Prompt help:\n"
		    "1          - select a single item\n"
		    "3-5        - select a range of items\n"
		    "2-3,6-9    - select multiple ranges\n"
		    "foo        - select item based on unique prefix\n"
		    "-...       - unselect specified items\n"
		    "*          - choose all items\n"
		    "           - (empty) finish selecting"));
	clean_print_color(CLEAN_COLOR_RESET);
}

/*
 * display menu stuff with number prefix and hotkey highlight
 */
static void print_highlight_menu_stuff(struct menu_stuff *stuff, int **chosen)
{
	struct string_list menu_list = STRING_LIST_INIT_DUP;
	struct strbuf menu = STRBUF_INIT;
	struct menu_item *menu_item;
	struct string_list_item *string_list_item;
	int i;

	switch (stuff->type) {
	default:
		die("Bad type of menu_stuff when print menu");
	case MENU_STUFF_TYPE_MENU_ITEM:
		menu_item = (struct menu_item *)stuff->stuff;
		for (i = 0; i < stuff->nr; i++, menu_item++) {
			const char *p;
			int highlighted = 0;

			p = menu_item->title;
			if ((*chosen)[i] < 0)
				(*chosen)[i] = menu_item->selected ? 1 : 0;
			strbuf_addf(&menu, "%s%2d: ", (*chosen)[i] ? "*" : " ", i+1);
			for (; *p; p++) {
				if (!highlighted && *p == menu_item->hotkey) {
					strbuf_addstr(&menu, clean_get_color(CLEAN_COLOR_PROMPT));
					strbuf_addch(&menu, *p);
					strbuf_addstr(&menu, clean_get_color(CLEAN_COLOR_RESET));
					highlighted = 1;
				} else {
					strbuf_addch(&menu, *p);
				}
			}
			string_list_append(&menu_list, menu.buf);
			strbuf_reset(&menu);
		}
		break;
	case MENU_STUFF_TYPE_STRING_LIST:
		i = 0;
		for_each_string_list_item(string_list_item, (struct string_list *)stuff->stuff) {
			if ((*chosen)[i] < 0)
				(*chosen)[i] = 0;
			strbuf_addf(&menu, "%s%2d: %s",
				    (*chosen)[i] ? "*" : " ", i+1, string_list_item->string);
			string_list_append(&menu_list, menu.buf);
			strbuf_reset(&menu);
			i++;
		}
		break;
	}

	pretty_print_menus(&menu_list);

	strbuf_release(&menu);
	string_list_clear(&menu_list, 0);
}

static int find_unique(const char *choice, struct menu_stuff *menu_stuff)
{
	struct menu_item *menu_item;
	struct string_list_item *string_list_item;
	int i, len, found = 0;

	len = strlen(choice);
	switch (menu_stuff->type) {
	default:
		die("Bad type of menu_stuff when parse choice");
	case MENU_STUFF_TYPE_MENU_ITEM:

		menu_item = (struct menu_item *)menu_stuff->stuff;
		for (i = 0; i < menu_stuff->nr; i++, menu_item++) {
			if (len == 1 && *choice == menu_item->hotkey) {
				found = i + 1;
				break;
			}
			if (!strncasecmp(choice, menu_item->title, len)) {
				if (found) {
					if (len == 1) {
						/* continue for hotkey matching */
						found = -1;
					} else {
						found = 0;
						break;
					}
				} else {
					found = i + 1;
				}
			}
		}
		break;
	case MENU_STUFF_TYPE_STRING_LIST:
		string_list_item = ((struct string_list *)menu_stuff->stuff)->items;
		for (i = 0; i < menu_stuff->nr; i++, string_list_item++) {
			if (!strncasecmp(choice, string_list_item->string, len)) {
				if (found) {
					found = 0;
					break;
				}
				found = i + 1;
			}
		}
		break;
	}
	return found;
}


/*
 * Parse user input, and return choice(s) for menu (menu_stuff).
 *
 * Input
 *     (for single choice)
 *         1          - select a numbered item
 *         foo        - select item based on menu title
 *                    - (empty) select nothing
 *
 *     (for multiple choice)
 *         1          - select a single item
 *         3-5        - select a range of items
 *         2-3,6-9    - select multiple ranges
 *         foo        - select item based on menu title
 *         -...       - unselect specified items
 *         *          - choose all items
 *                    - (empty) finish selecting
 *
 * The parse result will be saved in array **chosen, and
 * return number of total selections.
 */
static int parse_choice(struct menu_stuff *menu_stuff,
			int is_single,
			struct strbuf input,
			int **chosen)
{
	struct strbuf **choice_list, **ptr;
	int nr = 0;
	int i;

	if (is_single) {
		choice_list = strbuf_split_max(&input, '\n', 0);
	} else {
		char *p = input.buf;
		do {
			if (*p == ',')
				*p = ' ';
		} while (*p++);
		choice_list = strbuf_split_max(&input, ' ', 0);
	}

	for (ptr = choice_list; *ptr; ptr++) {
		char *p;
		int choose = 1;
		int bottom = 0, top = 0;
		int is_range, is_number;

		strbuf_trim(*ptr);
		if (!(*ptr)->len)
			continue;

		/* Input that begins with '-'; unchoose */
		if (*(*ptr)->buf == '-') {
			choose = 0;
			strbuf_remove((*ptr), 0, 1);
		}

		is_range = 0;
		is_number = 1;
		for (p = (*ptr)->buf; *p; p++) {
			if ('-' == *p) {
				if (!is_range) {
					is_range = 1;
					is_number = 0;
				} else {
					is_number = 0;
					is_range = 0;
					break;
				}
			} else if (!isdigit(*p)) {
				is_number = 0;
				is_range = 0;
				break;
			}
		}

		if (is_number) {
			bottom = atoi((*ptr)->buf);
			top = bottom;
		} else if (is_range) {
			bottom = atoi((*ptr)->buf);
			/* a range can be specified like 5-7 or 5- */
			if (!*(strchr((*ptr)->buf, '-') + 1))
				top = menu_stuff->nr;
			else
				top = atoi(strchr((*ptr)->buf, '-') + 1);
		} else if (!strcmp((*ptr)->buf, "*")) {
			bottom = 1;
			top = menu_stuff->nr;
		} else {
			bottom = find_unique((*ptr)->buf, menu_stuff);
			top = bottom;
		}

		if (top <= 0 || bottom <= 0 || top > menu_stuff->nr || bottom > top ||
		    (is_single && bottom != top)) {
			clean_print_color(CLEAN_COLOR_ERROR);
			printf_ln(_("Huh (%s)?"), (*ptr)->buf);
			clean_print_color(CLEAN_COLOR_RESET);
			continue;
		}

		for (i = bottom; i <= top; i++)
			(*chosen)[i-1] = choose;
	}

	strbuf_list_free(choice_list);

	for (i = 0; i < menu_stuff->nr; i++)
		nr += (*chosen)[i];
	return nr;
}

/*
 * Implement a git-add-interactive compatible UI, which is borrowed
 * from git-add--interactive.perl.
 *
 * Return value:
 *
 *   - Return an array of integers
 *   - , and it is up to you to free the allocated memory.
 *   - The array ends with EOF.
 *   - If user pressed CTRL-D (i.e. EOF), no selection returned.
 */
static int *list_and_choose(struct menu_opts *opts, struct menu_stuff *stuff)
{
	struct strbuf choice = STRBUF_INIT;
	int *chosen, *result;
	int nr = 0;
	int eof = 0;
	int i;

	chosen = xmalloc(sizeof(int) * stuff->nr);
	/* set chosen as uninitialized */
	for (i = 0; i < stuff->nr; i++)
		chosen[i] = -1;

	for (;;) {
		if (opts->header) {
			printf_ln("%s%s%s",
				  clean_get_color(CLEAN_COLOR_HEADER),
				  _(opts->header),
				  clean_get_color(CLEAN_COLOR_RESET));
		}

		/* chosen will be initialized by print_highlight_menu_stuff */
		print_highlight_menu_stuff(stuff, &chosen);

		if (opts->flags & MENU_OPTS_LIST_ONLY)
			break;

		if (opts->prompt) {
			printf("%s%s%s%s",
			       clean_get_color(CLEAN_COLOR_PROMPT),
			       _(opts->prompt),
			       opts->flags & MENU_OPTS_SINGLETON ? "> " : ">> ",
			       clean_get_color(CLEAN_COLOR_RESET));
		}

		if (strbuf_getline(&choice, stdin, '\n') != EOF) {
			strbuf_trim(&choice);
		} else {
			eof = 1;
			break;
		}

		/* help for prompt */
		if (!strcmp(choice.buf, "?")) {
			prompt_help_cmd(opts->flags & MENU_OPTS_SINGLETON);
			continue;
		}

		/* for a multiple-choice menu, press ENTER (empty) will return back */
		if (!(opts->flags & MENU_OPTS_SINGLETON) && !choice.len)
			break;

		nr = parse_choice(stuff,
				  opts->flags & MENU_OPTS_SINGLETON,
				  choice,
				  &chosen);

		if (opts->flags & MENU_OPTS_SINGLETON) {
			if (nr)
				break;
		} else if (opts->flags & MENU_OPTS_IMMEDIATE) {
			break;
		}
	}

	if (eof) {
		result = xmalloc(sizeof(int));
		*result = EOF;
	} else {
		int j = 0;

		/*
		 * recalculate nr, if return back from menu directly with
		 * default selections.
		 */
		if (!nr) {
			for (i = 0; i < stuff->nr; i++)
				nr += chosen[i];
		}

		result = xcalloc(nr + 1, sizeof(int));
		for (i = 0; i < stuff->nr && j < nr; i++) {
			if (chosen[i])
				result[j++] = i;
		}
		result[j] = EOF;
	}

	free(chosen);
	strbuf_release(&choice);
	return result;
}

static int clean_cmd(void)
{
	return MENU_RETURN_NO_LOOP;
}

static int filter_by_patterns_cmd(void)
{
	struct dir_struct dir;
	struct strbuf confirm = STRBUF_INIT;
	struct strbuf **ignore_list;
	struct string_list_item *item;
	struct exclude_list *el;
	int changed = -1, i;

	for (;;) {
		if (!del_list.nr)
			break;

		if (changed)
			pretty_print_dels();

		clean_print_color(CLEAN_COLOR_PROMPT);
		printf(_("Input ignore patterns>> "));
		clean_print_color(CLEAN_COLOR_RESET);
		if (strbuf_getline(&confirm, stdin, '\n') != EOF)
			strbuf_trim(&confirm);
		else
			putchar('\n');

		/* quit filter_by_pattern mode if press ENTER or Ctrl-D */
		if (!confirm.len)
			break;

		memset(&dir, 0, sizeof(dir));
		el = add_exclude_list(&dir, EXC_CMDL, "manual exclude");
		ignore_list = strbuf_split_max(&confirm, ' ', 0);

		for (i = 0; ignore_list[i]; i++) {
			strbuf_trim(ignore_list[i]);
			if (!ignore_list[i]->len)
				continue;

			add_exclude(ignore_list[i]->buf, "", 0, el, -(i+1));
		}

		changed = 0;
		for_each_string_list_item(item, &del_list) {
			int dtype = DT_UNKNOWN;

			if (is_excluded(&dir, item->string, &dtype)) {
				*item->string = '\0';
				changed++;
			}
		}

		if (changed) {
			string_list_remove_empty_items(&del_list, 0);
		} else {
			clean_print_color(CLEAN_COLOR_ERROR);
			printf_ln(_("WARNING: Cannot find items matched by: %s"), confirm.buf);
			clean_print_color(CLEAN_COLOR_RESET);
		}

		strbuf_list_free(ignore_list);
		clear_directory(&dir);
	}

	strbuf_release(&confirm);
	return 0;
}

static int select_by_numbers_cmd(void)
{
	struct menu_opts menu_opts;
	struct menu_stuff menu_stuff;
	struct string_list_item *items;
	int *chosen;
	int i, j;

	menu_opts.header = NULL;
	menu_opts.prompt = N_("Select items to delete");
	menu_opts.flags = 0;

	menu_stuff.type = MENU_STUFF_TYPE_STRING_LIST;
	menu_stuff.stuff = &del_list;
	menu_stuff.nr = del_list.nr;

	chosen = list_and_choose(&menu_opts, &menu_stuff);
	items = del_list.items;
	for (i = 0, j = 0; i < del_list.nr; i++) {
		if (i < chosen[j]) {
			*(items[i].string) = '\0';
		} else if (i == chosen[j]) {
			/* delete selected item */
			j++;
			continue;
		} else {
			/* end of chosen (chosen[j] == EOF), won't delete */
			*(items[i].string) = '\0';
		}
	}

	string_list_remove_empty_items(&del_list, 0);

	free(chosen);
	return 0;
}

static int ask_each_cmd(void)
{
	struct strbuf confirm = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;
	struct string_list_item *item;
	const char *qname;
	int changed = 0, eof = 0;

	for_each_string_list_item(item, &del_list) {
		/* Ctrl-D should stop removing files */
		if (!eof) {
			qname = quote_path_relative(item->string, NULL, &buf);
			/* TRANSLATORS: Make sure to keep [y/N] as is */
			printf(_("Remove %s [y/N]? "), qname);
			if (strbuf_getline(&confirm, stdin, '\n') != EOF) {
				strbuf_trim(&confirm);
			} else {
				putchar('\n');
				eof = 1;
			}
		}
		if (!confirm.len || strncasecmp(confirm.buf, "yes", confirm.len)) {
			*item->string = '\0';
			changed++;
		}
	}

	if (changed)
		string_list_remove_empty_items(&del_list, 0);

	strbuf_release(&buf);
	strbuf_release(&confirm);
	return MENU_RETURN_NO_LOOP;
}

static int quit_cmd(void)
{
	string_list_clear(&del_list, 0);
	printf_ln(_("Bye."));
	return MENU_RETURN_NO_LOOP;
}

static int help_cmd(void)
{
	clean_print_color(CLEAN_COLOR_HELP);
	printf_ln(_(
		    "clean               - start cleaning\n"
		    "filter by pattern   - exclude items from deletion\n"
		    "select by numbers   - select items to be deleted by numbers\n"
		    "ask each            - confirm each deletion (like \"rm -i\")\n"
		    "quit                - stop cleaning\n"
		    "help                - this screen\n"
		    "?                   - help for prompt selection"
		   ));
	clean_print_color(CLEAN_COLOR_RESET);
	return 0;
}

static void interactive_main_loop(void)
{
	while (del_list.nr) {
		struct menu_opts menu_opts;
		struct menu_stuff menu_stuff;
		struct menu_item menus[] = {
			{'c', "clean",			0, clean_cmd},
			{'f', "filter by pattern",	0, filter_by_patterns_cmd},
			{'s', "select by numbers",	0, select_by_numbers_cmd},
			{'a', "ask each",		0, ask_each_cmd},
			{'q', "quit",			0, quit_cmd},
			{'h', "help",			0, help_cmd},
		};
		int *chosen;

		menu_opts.header = N_("*** Commands ***");
		menu_opts.prompt = N_("What now");
		menu_opts.flags = MENU_OPTS_SINGLETON;

		menu_stuff.type = MENU_STUFF_TYPE_MENU_ITEM;
		menu_stuff.stuff = menus;
		menu_stuff.nr = sizeof(menus) / sizeof(struct menu_item);

		clean_print_color(CLEAN_COLOR_HEADER);
		printf_ln(Q_("Would remove the following item:",
			     "Would remove the following items:",
			     del_list.nr));
		clean_print_color(CLEAN_COLOR_RESET);

		pretty_print_dels();

		chosen = list_and_choose(&menu_opts, &menu_stuff);

		if (*chosen != EOF) {
			int ret;
			ret = menus[*chosen].fn();
			if (ret != MENU_RETURN_NO_LOOP) {
				free(chosen);
				chosen = NULL;
				if (!del_list.nr) {
					clean_print_color(CLEAN_COLOR_ERROR);
					printf_ln(_("No more files to clean, exiting."));
					clean_print_color(CLEAN_COLOR_RESET);
					break;
				}
				continue;
			}
		} else {
			quit_cmd();
		}

		free(chosen);
		chosen = NULL;
		break;
	}
}

int cmd_clean(int argc, const char **argv, const char *prefix)
{
	int i, res;
	int dry_run = 0, remove_directories = 0, quiet = 0, ignored = 0;
	int ignored_only = 0, config_set = 0, errors = 0, gone = 1;
	int rm_flags = REMOVE_DIR_KEEP_NESTED_GIT;
	struct strbuf abs_path = STRBUF_INIT;
	struct dir_struct dir;
	struct pathspec pathspec;
	struct strbuf buf = STRBUF_INIT;
	struct string_list exclude_list = STRING_LIST_INIT_NODUP;
	struct exclude_list *el;
	struct string_list_item *item;
	const char *qname;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("do not print names of files removed")),
		OPT__DRY_RUN(&dry_run, N_("dry run")),
		OPT__FORCE(&force, N_("force")),
		OPT_BOOL('i', "interactive", &interactive, N_("interactive cleaning")),
		OPT_BOOL('d', NULL, &remove_directories,
				N_("remove whole directories")),
		{ OPTION_CALLBACK, 'e', "exclude", &exclude_list, N_("pattern"),
		  N_("add <pattern> to ignore rules"), PARSE_OPT_NONEG, exclude_cb },
		OPT_BOOL('x', NULL, &ignored, N_("remove ignored files, too")),
		OPT_BOOL('X', NULL, &ignored_only,
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
			die(_("clean.requireForce set to true and neither -i, -n, nor -f given; "
				  "refusing to clean"));
		else
			die(_("clean.requireForce defaults to true and neither -i, -n, nor -f given;"
				  " refusing to clean"));
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

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_CWD,
		       prefix, argv);

	fill_directory(&dir, &pathspec);

	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		int matches = 0;
		struct stat st;
		const char *rel;

		if (!cache_name_is_other(ent->name, ent->len))
			continue;

		if (pathspec.nr)
			matches = dir_path_match(ent, &pathspec, 0, NULL);

		if (pathspec.nr && !matches)
			continue;

		if (lstat(ent->name, &st))
			die_errno("Cannot lstat '%s'", ent->name);

		if (S_ISDIR(st.st_mode) && !remove_directories &&
		    matches != MATCHED_EXACTLY)
			continue;

		rel = relative_path(ent->name, prefix, &buf);
		string_list_append(&del_list, rel);
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

	strbuf_release(&abs_path);
	strbuf_release(&buf);
	string_list_clear(&del_list, 0);
	string_list_clear(&exclude_list, 0);
	return (errors != 0);
}
