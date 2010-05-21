#include "builtin.h"
#include "cache.h"
#include "color.h"
#include "parse-options.h"

static const char *const builtin_config_usage[] = {
	"git config [options]",
	NULL
};

static char *key;
static regex_t *key_regexp;
static regex_t *regexp;
static int show_keys;
static int use_key_regexp;
static int do_all;
static int do_not_match;
static int seen;
static char delim = '=';
static char key_delim = ' ';
static char term = '\n';

static int use_global_config, use_system_config;
static const char *given_config_file;
static int actions, types;
static const char *get_color_slot, *get_colorbool_slot;
static int end_null;

#define ACTION_GET (1<<0)
#define ACTION_GET_ALL (1<<1)
#define ACTION_GET_REGEXP (1<<2)
#define ACTION_REPLACE_ALL (1<<3)
#define ACTION_ADD (1<<4)
#define ACTION_UNSET (1<<5)
#define ACTION_UNSET_ALL (1<<6)
#define ACTION_RENAME_SECTION (1<<7)
#define ACTION_REMOVE_SECTION (1<<8)
#define ACTION_LIST (1<<9)
#define ACTION_EDIT (1<<10)
#define ACTION_SET (1<<11)
#define ACTION_SET_ALL (1<<12)
#define ACTION_GET_COLOR (1<<13)
#define ACTION_GET_COLORBOOL (1<<14)

#define TYPE_BOOL (1<<0)
#define TYPE_INT (1<<1)
#define TYPE_BOOL_OR_INT (1<<2)
#define TYPE_PATH (1<<3)

static struct option builtin_config_options[] = {
	OPT_GROUP("Config file location"),
	OPT_BOOLEAN(0, "global", &use_global_config, "use global config file"),
	OPT_BOOLEAN(0, "system", &use_system_config, "use system config file"),
	OPT_STRING('f', "file", &given_config_file, "FILE", "use given config file"),
	OPT_GROUP("Action"),
	OPT_BIT(0, "get", &actions, "get value: name [value-regex]", ACTION_GET),
	OPT_BIT(0, "get-all", &actions, "get all values: key [value-regex]", ACTION_GET_ALL),
	OPT_BIT(0, "get-regexp", &actions, "get values for regexp: name-regex [value-regex]", ACTION_GET_REGEXP),
	OPT_BIT(0, "replace-all", &actions, "replace all matching variables: name value [value_regex]", ACTION_REPLACE_ALL),
	OPT_BIT(0, "add", &actions, "adds a new variable: name value", ACTION_ADD),
	OPT_BIT(0, "unset", &actions, "removes a variable: name [value-regex]", ACTION_UNSET),
	OPT_BIT(0, "unset-all", &actions, "removes all matches: name [value-regex]", ACTION_UNSET_ALL),
	OPT_BIT(0, "rename-section", &actions, "rename section: old-name new-name", ACTION_RENAME_SECTION),
	OPT_BIT(0, "remove-section", &actions, "remove a section: name", ACTION_REMOVE_SECTION),
	OPT_BIT('l', "list", &actions, "list all", ACTION_LIST),
	OPT_BIT('e', "edit", &actions, "opens an editor", ACTION_EDIT),
	OPT_STRING(0, "get-color", &get_color_slot, "slot", "find the color configured: [default]"),
	OPT_STRING(0, "get-colorbool", &get_colorbool_slot, "slot", "find the color setting: [stdout-is-tty]"),
	OPT_GROUP("Type"),
	OPT_BIT(0, "bool", &types, "value is \"true\" or \"false\"", TYPE_BOOL),
	OPT_BIT(0, "int", &types, "value is decimal number", TYPE_INT),
	OPT_BIT(0, "bool-or-int", &types, "value is --bool or --int", TYPE_BOOL_OR_INT),
	OPT_BIT(0, "path", &types, "value is a path (file or directory name)", TYPE_PATH),
	OPT_GROUP("Other"),
	OPT_BOOLEAN('z', "null", &end_null, "terminate values with NUL byte"),
	OPT_END(),
};

static void check_argc(int argc, int min, int max) {
	if (argc >= min && argc <= max)
		return;
	error("wrong number of arguments");
	usage_with_options(builtin_config_usage, builtin_config_options);
}

static int show_all_config(const char *key_, const char *value_, void *cb)
{
	if (value_)
		printf("%s%c%s%c", key_, delim, value_, term);
	else
		printf("%s%c", key_, term);
	return 0;
}

static int show_config(const char *key_, const char *value_, void *cb)
{
	char value[256];
	const char *vptr = value;
	int must_free_vptr = 0;
	int dup_error = 0;

	if (!use_key_regexp && strcmp(key_, key))
		return 0;
	if (use_key_regexp && regexec(key_regexp, key_, 0, NULL, 0))
		return 0;
	if (regexp != NULL &&
	    (do_not_match ^ !!regexec(regexp, (value_?value_:""), 0, NULL, 0)))
		return 0;

	if (show_keys) {
		if (value_)
			printf("%s%c", key_, key_delim);
		else
			printf("%s", key_);
	}
	if (seen && !do_all)
		dup_error = 1;
	if (types == TYPE_INT)
		sprintf(value, "%d", git_config_int(key_, value_?value_:""));
	else if (types == TYPE_BOOL)
		vptr = git_config_bool(key_, value_) ? "true" : "false";
	else if (types == TYPE_BOOL_OR_INT) {
		int is_bool, v;
		v = git_config_bool_or_int(key_, value_, &is_bool);
		if (is_bool)
			vptr = v ? "true" : "false";
		else
			sprintf(value, "%d", v);
	} else if (types == TYPE_PATH) {
		git_config_pathname(&vptr, key_, value_);
		must_free_vptr = 1;
	}
	else
		vptr = value_?value_:"";
	seen++;
	if (dup_error) {
		error("More than one value for the key %s: %s",
				key_, vptr);
	}
	else
		printf("%s%c", vptr, term);
	if (must_free_vptr)
		/* If vptr must be freed, it's a pointer to a
		 * dynamically allocated buffer, it's safe to cast to
		 * const.
		*/
		free((char *)vptr);

	return 0;
}

static int get_value(const char *key_, const char *regex_)
{
	int ret = -1;
	char *tl;
	char *global = NULL, *repo_config = NULL;
	const char *system_wide = NULL, *local;

	local = config_exclusive_filename;
	if (!local) {
		const char *home = getenv("HOME");
		local = repo_config = git_pathdup("config");
		if (git_config_global() && home)
			global = xstrdup(mkpath("%s/.gitconfig", home));
		if (git_config_system())
			system_wide = git_etc_gitconfig();
	}

	key = xstrdup(key_);
	for (tl=key+strlen(key)-1; tl >= key && *tl != '.'; --tl)
		*tl = tolower(*tl);
	for (tl=key; *tl && *tl != '.'; ++tl)
		*tl = tolower(*tl);

	if (use_key_regexp) {
		key_regexp = (regex_t*)xmalloc(sizeof(regex_t));
		if (regcomp(key_regexp, key, REG_EXTENDED)) {
			fprintf(stderr, "Invalid key pattern: %s\n", key_);
			goto free_strings;
		}
	}

	if (regex_) {
		if (regex_[0] == '!') {
			do_not_match = 1;
			regex_++;
		}

		regexp = (regex_t*)xmalloc(sizeof(regex_t));
		if (regcomp(regexp, regex_, REG_EXTENDED)) {
			fprintf(stderr, "Invalid pattern: %s\n", regex_);
			goto free_strings;
		}
	}

	if (do_all && system_wide)
		git_config_from_file(show_config, system_wide, NULL);
	if (do_all && global)
		git_config_from_file(show_config, global, NULL);
	if (do_all)
		git_config_from_file(show_config, local, NULL);
	git_config_from_parameters(show_config, NULL);
	if (!do_all && !seen)
		git_config_from_file(show_config, local, NULL);
	if (!do_all && !seen && global)
		git_config_from_file(show_config, global, NULL);
	if (!do_all && !seen && system_wide)
		git_config_from_file(show_config, system_wide, NULL);

	free(key);
	if (regexp) {
		regfree(regexp);
		free(regexp);
	}

	if (do_all)
		ret = !seen;
	else
		ret = (seen == 1) ? 0 : seen > 1 ? 2 : 1;

free_strings:
	free(repo_config);
	free(global);
	return ret;
}

static char *normalize_value(const char *key, const char *value)
{
	char *normalized;

	if (!value)
		return NULL;

	if (types == 0 || types == TYPE_PATH)
		/*
		 * We don't do normalization for TYPE_PATH here: If
		 * the path is like ~/foobar/, we prefer to store
		 * "~/foobar/" in the config file, and to expand the ~
		 * when retrieving the value.
		 */
		normalized = xstrdup(value);
	else {
		normalized = xmalloc(64);
		if (types == TYPE_INT) {
			int v = git_config_int(key, value);
			sprintf(normalized, "%d", v);
		}
		else if (types == TYPE_BOOL)
			sprintf(normalized, "%s",
				git_config_bool(key, value) ? "true" : "false");
		else if (types == TYPE_BOOL_OR_INT) {
			int is_bool, v;
			v = git_config_bool_or_int(key, value, &is_bool);
			if (!is_bool)
				sprintf(normalized, "%d", v);
			else
				sprintf(normalized, "%s", v ? "true" : "false");
		}
	}

	return normalized;
}

static int get_color_found;
static const char *get_color_slot;
static const char *get_colorbool_slot;
static char parsed_color[COLOR_MAXLEN];

static int git_get_color_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, get_color_slot)) {
		if (!value)
			config_error_nonbool(var);
		color_parse(value, var, parsed_color);
		get_color_found = 1;
	}
	return 0;
}

static void get_color(const char *def_color)
{
	get_color_found = 0;
	parsed_color[0] = '\0';
	git_config(git_get_color_config, NULL);

	if (!get_color_found && def_color)
		color_parse(def_color, "command line", parsed_color);

	fputs(parsed_color, stdout);
}

static int stdout_is_tty;
static int get_colorbool_found;
static int get_diff_color_found;
static int git_get_colorbool_config(const char *var, const char *value,
		void *cb)
{
	if (!strcmp(var, get_colorbool_slot)) {
		get_colorbool_found =
			git_config_colorbool(var, value, stdout_is_tty);
	}
	if (!strcmp(var, "diff.color")) {
		get_diff_color_found =
			git_config_colorbool(var, value, stdout_is_tty);
	}
	if (!strcmp(var, "color.ui")) {
		git_use_color_default = git_config_colorbool(var, value, stdout_is_tty);
		return 0;
	}
	return 0;
}

static int get_colorbool(int print)
{
	get_colorbool_found = -1;
	get_diff_color_found = -1;
	git_config(git_get_colorbool_config, NULL);

	if (get_colorbool_found < 0) {
		if (!strcmp(get_colorbool_slot, "color.diff"))
			get_colorbool_found = get_diff_color_found;
		if (get_colorbool_found < 0)
			get_colorbool_found = git_use_color_default;
	}

	if (print) {
		printf("%s\n", get_colorbool_found ? "true" : "false");
		return 0;
	} else
		return get_colorbool_found ? 0 : 1;
}

int cmd_config(int argc, const char **argv, const char *unused_prefix)
{
	int nongit;
	char *value;
	const char *prefix = setup_git_directory_gently(&nongit);

	config_exclusive_filename = getenv(CONFIG_ENVIRONMENT);

	argc = parse_options(argc, argv, prefix, builtin_config_options,
			     builtin_config_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (use_global_config + use_system_config + !!given_config_file > 1) {
		error("only one config file at a time.");
		usage_with_options(builtin_config_usage, builtin_config_options);
	}

	if (use_global_config) {
		char *home = getenv("HOME");
		if (home) {
			char *user_config = xstrdup(mkpath("%s/.gitconfig", home));
			config_exclusive_filename = user_config;
		} else {
			die("$HOME not set");
		}
	}
	else if (use_system_config)
		config_exclusive_filename = git_etc_gitconfig();
	else if (given_config_file) {
		if (!is_absolute_path(given_config_file) && prefix)
			config_exclusive_filename = prefix_filename(prefix,
								    strlen(prefix),
								    given_config_file);
		else
			config_exclusive_filename = given_config_file;
	}

	if (end_null) {
		term = '\0';
		delim = '\n';
		key_delim = '\n';
	}

	if (HAS_MULTI_BITS(types)) {
		error("only one type at a time.");
		usage_with_options(builtin_config_usage, builtin_config_options);
	}

	if (get_color_slot)
	    actions |= ACTION_GET_COLOR;
	if (get_colorbool_slot)
	    actions |= ACTION_GET_COLORBOOL;

	if ((get_color_slot || get_colorbool_slot) && types) {
		error("--get-color and variable type are incoherent");
		usage_with_options(builtin_config_usage, builtin_config_options);
	}

	if (HAS_MULTI_BITS(actions)) {
		error("only one action at a time.");
		usage_with_options(builtin_config_usage, builtin_config_options);
	}
	if (actions == 0)
		switch (argc) {
		case 1: actions = ACTION_GET; break;
		case 2: actions = ACTION_SET; break;
		case 3: actions = ACTION_SET_ALL; break;
		default:
			usage_with_options(builtin_config_usage, builtin_config_options);
		}

	if (actions == ACTION_LIST) {
		check_argc(argc, 0, 0);
		if (git_config(show_all_config, NULL) < 0) {
			if (config_exclusive_filename)
				die_errno("unable to read config file '%s'",
					  config_exclusive_filename);
			else
				die("error processing config file(s)");
		}
	}
	else if (actions == ACTION_EDIT) {
		check_argc(argc, 0, 0);
		if (!config_exclusive_filename && nongit)
			die("not in a git directory");
		git_config(git_default_config, NULL);
		launch_editor(config_exclusive_filename ?
			      config_exclusive_filename : git_path("config"),
			      NULL, NULL);
	}
	else if (actions == ACTION_SET) {
		check_argc(argc, 2, 2);
		value = normalize_value(argv[0], argv[1]);
		return git_config_set(argv[0], value);
	}
	else if (actions == ACTION_SET_ALL) {
		check_argc(argc, 2, 3);
		value = normalize_value(argv[0], argv[1]);
		return git_config_set_multivar(argv[0], value, argv[2], 0);
	}
	else if (actions == ACTION_ADD) {
		check_argc(argc, 2, 2);
		value = normalize_value(argv[0], argv[1]);
		return git_config_set_multivar(argv[0], value, "^$", 0);
	}
	else if (actions == ACTION_REPLACE_ALL) {
		check_argc(argc, 2, 3);
		value = normalize_value(argv[0], argv[1]);
		return git_config_set_multivar(argv[0], value, argv[2], 1);
	}
	else if (actions == ACTION_GET) {
		check_argc(argc, 1, 2);
		return get_value(argv[0], argv[1]);
	}
	else if (actions == ACTION_GET_ALL) {
		do_all = 1;
		check_argc(argc, 1, 2);
		return get_value(argv[0], argv[1]);
	}
	else if (actions == ACTION_GET_REGEXP) {
		show_keys = 1;
		use_key_regexp = 1;
		do_all = 1;
		check_argc(argc, 1, 2);
		return get_value(argv[0], argv[1]);
	}
	else if (actions == ACTION_UNSET) {
		check_argc(argc, 1, 2);
		if (argc == 2)
			return git_config_set_multivar(argv[0], NULL, argv[1], 0);
		else
			return git_config_set(argv[0], NULL);
	}
	else if (actions == ACTION_UNSET_ALL) {
		check_argc(argc, 1, 2);
		return git_config_set_multivar(argv[0], NULL, argv[1], 1);
	}
	else if (actions == ACTION_RENAME_SECTION) {
		int ret;
		check_argc(argc, 2, 2);
		ret = git_config_rename_section(argv[0], argv[1]);
		if (ret < 0)
			return ret;
		if (ret == 0)
			die("No such section!");
	}
	else if (actions == ACTION_REMOVE_SECTION) {
		int ret;
		check_argc(argc, 1, 1);
		ret = git_config_rename_section(argv[0], NULL);
		if (ret < 0)
			return ret;
		if (ret == 0)
			die("No such section!");
	}
	else if (actions == ACTION_GET_COLOR) {
		get_color(argv[0]);
	}
	else if (actions == ACTION_GET_COLORBOOL) {
		if (argc == 1)
			stdout_is_tty = git_config_bool("command line", argv[0]);
		else if (argc == 0)
			stdout_is_tty = isatty(1);
		return get_colorbool(argc != 0);
	}

	return 0;
}
