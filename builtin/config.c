#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "color.h"
#include "parse-options.h"
#include "urlmatch.h"
#include "quote.h"

static const char *const builtin_config_usage[] = {
	N_("git config [<options>]"),
	NULL
};

static char *key;
static regex_t *key_regexp;
static regex_t *regexp;
static int show_keys;
static int omit_values;
static int use_key_regexp;
static int do_all;
static int do_not_match;
static char delim = '=';
static char key_delim = ' ';
static char term = '\n';

static int use_global_config, use_system_config, use_local_config;
static struct git_config_source given_config_source;
static int actions, types;
static int end_null;
static int respect_includes_opt = -1;
static struct config_options config_options;
static int show_origin;

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
#define ACTION_GET_URLMATCH (1<<15)

#define TYPE_BOOL (1<<0)
#define TYPE_INT (1<<1)
#define TYPE_BOOL_OR_INT (1<<2)
#define TYPE_PATH (1<<3)
#define TYPE_EXPIRY_DATE (1<<4)

static struct option builtin_config_options[] = {
	OPT_GROUP(N_("Config file location")),
	OPT_BOOL(0, "global", &use_global_config, N_("use global config file")),
	OPT_BOOL(0, "system", &use_system_config, N_("use system config file")),
	OPT_BOOL(0, "local", &use_local_config, N_("use repository config file")),
	OPT_STRING('f', "file", &given_config_source.file, N_("file"), N_("use given config file")),
	OPT_STRING(0, "blob", &given_config_source.blob, N_("blob-id"), N_("read config from given blob object")),
	OPT_GROUP(N_("Action")),
	OPT_BIT(0, "get", &actions, N_("get value: name [value-regex]"), ACTION_GET),
	OPT_BIT(0, "get-all", &actions, N_("get all values: key [value-regex]"), ACTION_GET_ALL),
	OPT_BIT(0, "get-regexp", &actions, N_("get values for regexp: name-regex [value-regex]"), ACTION_GET_REGEXP),
	OPT_BIT(0, "get-urlmatch", &actions, N_("get value specific for the URL: section[.var] URL"), ACTION_GET_URLMATCH),
	OPT_BIT(0, "replace-all", &actions, N_("replace all matching variables: name value [value_regex]"), ACTION_REPLACE_ALL),
	OPT_BIT(0, "add", &actions, N_("add a new variable: name value"), ACTION_ADD),
	OPT_BIT(0, "unset", &actions, N_("remove a variable: name [value-regex]"), ACTION_UNSET),
	OPT_BIT(0, "unset-all", &actions, N_("remove all matches: name [value-regex]"), ACTION_UNSET_ALL),
	OPT_BIT(0, "rename-section", &actions, N_("rename section: old-name new-name"), ACTION_RENAME_SECTION),
	OPT_BIT(0, "remove-section", &actions, N_("remove a section: name"), ACTION_REMOVE_SECTION),
	OPT_BIT('l', "list", &actions, N_("list all"), ACTION_LIST),
	OPT_BIT('e', "edit", &actions, N_("open an editor"), ACTION_EDIT),
	OPT_BIT(0, "get-color", &actions, N_("find the color configured: slot [default]"), ACTION_GET_COLOR),
	OPT_BIT(0, "get-colorbool", &actions, N_("find the color setting: slot [stdout-is-tty]"), ACTION_GET_COLORBOOL),
	OPT_GROUP(N_("Type")),
	OPT_BIT(0, "bool", &types, N_("value is \"true\" or \"false\""), TYPE_BOOL),
	OPT_BIT(0, "int", &types, N_("value is decimal number"), TYPE_INT),
	OPT_BIT(0, "bool-or-int", &types, N_("value is --bool or --int"), TYPE_BOOL_OR_INT),
	OPT_BIT(0, "path", &types, N_("value is a path (file or directory name)"), TYPE_PATH),
	OPT_BIT(0, "expiry-date", &types, N_("value is an expiry date"), TYPE_EXPIRY_DATE),
	OPT_GROUP(N_("Other")),
	OPT_BOOL('z', "null", &end_null, N_("terminate values with NUL byte")),
	OPT_BOOL(0, "name-only", &omit_values, N_("show variable names only")),
	OPT_BOOL(0, "includes", &respect_includes_opt, N_("respect include directives on lookup")),
	OPT_BOOL(0, "show-origin", &show_origin, N_("show origin of config (file, standard input, blob, command line)")),
	OPT_END(),
};

static void check_argc(int argc, int min, int max) {
	if (argc >= min && argc <= max)
		return;
	error("wrong number of arguments");
	usage_with_options(builtin_config_usage, builtin_config_options);
}

static void show_config_origin(struct strbuf *buf)
{
	const char term = end_null ? '\0' : '\t';

	strbuf_addstr(buf, current_config_origin_type());
	strbuf_addch(buf, ':');
	if (end_null)
		strbuf_addstr(buf, current_config_name());
	else
		quote_c_style(current_config_name(), buf, NULL, 0);
	strbuf_addch(buf, term);
}

static int show_all_config(const char *key_, const char *value_, void *cb)
{
	if (show_origin) {
		struct strbuf buf = STRBUF_INIT;
		show_config_origin(&buf);
		/* Use fwrite as "buf" can contain \0's if "end_null" is set. */
		fwrite(buf.buf, 1, buf.len, stdout);
		strbuf_release(&buf);
	}
	if (!omit_values && value_)
		printf("%s%c%s%c", key_, delim, value_, term);
	else
		printf("%s%c", key_, term);
	return 0;
}

struct strbuf_list {
	struct strbuf *items;
	int nr;
	int alloc;
};

static int format_config(struct strbuf *buf, const char *key_, const char *value_)
{
	if (show_origin)
		show_config_origin(buf);
	if (show_keys)
		strbuf_addstr(buf, key_);
	if (!omit_values) {
		if (show_keys)
			strbuf_addch(buf, key_delim);

		if (types == TYPE_INT)
			strbuf_addf(buf, "%"PRId64,
				    git_config_int64(key_, value_ ? value_ : ""));
		else if (types == TYPE_BOOL)
			strbuf_addstr(buf, git_config_bool(key_, value_) ?
				      "true" : "false");
		else if (types == TYPE_BOOL_OR_INT) {
			int is_bool, v;
			v = git_config_bool_or_int(key_, value_, &is_bool);
			if (is_bool)
				strbuf_addstr(buf, v ? "true" : "false");
			else
				strbuf_addf(buf, "%d", v);
		} else if (types == TYPE_PATH) {
			const char *v;
			if (git_config_pathname(&v, key_, value_) < 0)
				return -1;
			strbuf_addstr(buf, v);
			free((char *)v);
		} else if (types == TYPE_EXPIRY_DATE) {
			timestamp_t t;
			if (git_config_expiry_date(&t, key_, value_) < 0)
				return -1;
			strbuf_addf(buf, "%"PRItime, t);
		} else if (value_) {
			strbuf_addstr(buf, value_);
		} else {
			/* Just show the key name; back out delimiter */
			if (show_keys)
				strbuf_setlen(buf, buf->len - 1);
		}
	}
	strbuf_addch(buf, term);
	return 0;
}

static int collect_config(const char *key_, const char *value_, void *cb)
{
	struct strbuf_list *values = cb;

	if (!use_key_regexp && strcmp(key_, key))
		return 0;
	if (use_key_regexp && regexec(key_regexp, key_, 0, NULL, 0))
		return 0;
	if (regexp != NULL &&
	    (do_not_match ^ !!regexec(regexp, (value_?value_:""), 0, NULL, 0)))
		return 0;

	ALLOC_GROW(values->items, values->nr + 1, values->alloc);
	strbuf_init(&values->items[values->nr], 0);

	return format_config(&values->items[values->nr++], key_, value_);
}

static int get_value(const char *key_, const char *regex_)
{
	int ret = CONFIG_GENERIC_ERROR;
	struct strbuf_list values = {NULL};
	int i;

	if (use_key_regexp) {
		char *tl;

		/*
		 * NEEDSWORK: this naive pattern lowercasing obviously does not
		 * work for more complex patterns like "^[^.]*Foo.*bar".
		 * Perhaps we should deprecate this altogether someday.
		 */

		key = xstrdup(key_);
		for (tl = key + strlen(key) - 1;
		     tl >= key && *tl != '.';
		     tl--)
			*tl = tolower(*tl);
		for (tl = key; *tl && *tl != '.'; tl++)
			*tl = tolower(*tl);

		key_regexp = (regex_t*)xmalloc(sizeof(regex_t));
		if (regcomp(key_regexp, key, REG_EXTENDED)) {
			error("invalid key pattern: %s", key_);
			FREE_AND_NULL(key_regexp);
			ret = CONFIG_INVALID_PATTERN;
			goto free_strings;
		}
	} else {
		if (git_config_parse_key(key_, &key, NULL)) {
			ret = CONFIG_INVALID_KEY;
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
			error("invalid pattern: %s", regex_);
			FREE_AND_NULL(regexp);
			ret = CONFIG_INVALID_PATTERN;
			goto free_strings;
		}
	}

	config_with_options(collect_config, &values,
			    &given_config_source, &config_options);

	ret = !values.nr;

	for (i = 0; i < values.nr; i++) {
		struct strbuf *buf = values.items + i;
		if (do_all || i == values.nr - 1)
			fwrite(buf->buf, 1, buf->len, stdout);
		strbuf_release(buf);
	}
	free(values.items);

free_strings:
	free(key);
	if (key_regexp) {
		regfree(key_regexp);
		free(key_regexp);
	}
	if (regexp) {
		regfree(regexp);
		free(regexp);
	}

	return ret;
}

static char *normalize_value(const char *key, const char *value)
{
	if (!value)
		return NULL;

	if (types == 0 || types == TYPE_PATH || types == TYPE_EXPIRY_DATE)
		/*
		 * We don't do normalization for TYPE_PATH here: If
		 * the path is like ~/foobar/, we prefer to store
		 * "~/foobar/" in the config file, and to expand the ~
		 * when retrieving the value.
		 * Also don't do normalization for expiry dates.
		 */
		return xstrdup(value);
	if (types == TYPE_INT)
		return xstrfmt("%"PRId64, git_config_int64(key, value));
	if (types == TYPE_BOOL)
		return xstrdup(git_config_bool(key, value) ?  "true" : "false");
	if (types == TYPE_BOOL_OR_INT) {
		int is_bool, v;
		v = git_config_bool_or_int(key, value, &is_bool);
		if (!is_bool)
			return xstrfmt("%d", v);
		else
			return xstrdup(v ? "true" : "false");
	}

	die("BUG: cannot normalize type %d", types);
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
		if (color_parse(value, parsed_color) < 0)
			return -1;
		get_color_found = 1;
	}
	return 0;
}

static void get_color(const char *var, const char *def_color)
{
	get_color_slot = var;
	get_color_found = 0;
	parsed_color[0] = '\0';
	config_with_options(git_get_color_config, NULL,
			    &given_config_source, &config_options);

	if (!get_color_found && def_color) {
		if (color_parse(def_color, parsed_color) < 0)
			die(_("unable to parse default color value"));
	}

	fputs(parsed_color, stdout);
}

static int get_colorbool_found;
static int get_diff_color_found;
static int get_color_ui_found;
static int git_get_colorbool_config(const char *var, const char *value,
		void *cb)
{
	if (!strcmp(var, get_colorbool_slot))
		get_colorbool_found = git_config_colorbool(var, value);
	else if (!strcmp(var, "diff.color"))
		get_diff_color_found = git_config_colorbool(var, value);
	else if (!strcmp(var, "color.ui"))
		get_color_ui_found = git_config_colorbool(var, value);
	return 0;
}

static int get_colorbool(const char *var, int print)
{
	get_colorbool_slot = var;
	get_colorbool_found = -1;
	get_diff_color_found = -1;
	get_color_ui_found = -1;
	config_with_options(git_get_colorbool_config, NULL,
			    &given_config_source, &config_options);

	if (get_colorbool_found < 0) {
		if (!strcmp(get_colorbool_slot, "color.diff"))
			get_colorbool_found = get_diff_color_found;
		if (get_colorbool_found < 0)
			get_colorbool_found = get_color_ui_found;
	}

	if (get_colorbool_found < 0)
		/* default value if none found in config */
		get_colorbool_found = GIT_COLOR_AUTO;

	get_colorbool_found = want_color(get_colorbool_found);

	if (print) {
		printf("%s\n", get_colorbool_found ? "true" : "false");
		return 0;
	} else
		return get_colorbool_found ? 0 : 1;
}

static void check_write(void)
{
	if (!given_config_source.file && !startup_info->have_repository)
		die("not in a git directory");

	if (given_config_source.use_stdin)
		die("writing to stdin is not supported");

	if (given_config_source.blob)
		die("writing config blobs is not supported");
}

struct urlmatch_current_candidate_value {
	char value_is_null;
	struct strbuf value;
};

static int urlmatch_collect_fn(const char *var, const char *value, void *cb)
{
	struct string_list *values = cb;
	struct string_list_item *item = string_list_insert(values, var);
	struct urlmatch_current_candidate_value *matched = item->util;

	if (!matched) {
		matched = xmalloc(sizeof(*matched));
		strbuf_init(&matched->value, 0);
		item->util = matched;
	} else {
		strbuf_reset(&matched->value);
	}

	if (value) {
		strbuf_addstr(&matched->value, value);
		matched->value_is_null = 0;
	} else {
		matched->value_is_null = 1;
	}
	return 0;
}

static int get_urlmatch(const char *var, const char *url)
{
	int ret;
	char *section_tail;
	struct string_list_item *item;
	struct urlmatch_config config = { STRING_LIST_INIT_DUP };
	struct string_list values = STRING_LIST_INIT_DUP;

	config.collect_fn = urlmatch_collect_fn;
	config.cascade_fn = NULL;
	config.cb = &values;

	if (!url_normalize(url, &config.url))
		die("%s", config.url.err);

	config.section = xstrdup_tolower(var);
	section_tail = strchr(config.section, '.');
	if (section_tail) {
		*section_tail = '\0';
		config.key = section_tail + 1;
		show_keys = 0;
	} else {
		config.key = NULL;
		show_keys = 1;
	}

	config_with_options(urlmatch_config_entry, &config,
			    &given_config_source, &config_options);

	ret = !values.nr;

	for_each_string_list_item(item, &values) {
		struct urlmatch_current_candidate_value *matched = item->util;
		struct strbuf buf = STRBUF_INIT;

		format_config(&buf, item->string,
			      matched->value_is_null ? NULL : matched->value.buf);
		fwrite(buf.buf, 1, buf.len, stdout);
		strbuf_release(&buf);

		strbuf_release(&matched->value);
	}
	string_list_clear(&config.vars, 1);
	string_list_clear(&values, 1);
	free(config.url.url);

	free((void *)config.section);
	return ret;
}

static char *default_user_config(void)
{
	struct strbuf buf = STRBUF_INIT;
	strbuf_addf(&buf,
		    _("# This is Git's per-user configuration file.\n"
		      "[user]\n"
		      "# Please adapt and uncomment the following lines:\n"
		      "#	name = %s\n"
		      "#	email = %s\n"),
		    ident_default_name(),
		    ident_default_email());
	return strbuf_detach(&buf, NULL);
}

int cmd_config(int argc, const char **argv, const char *prefix)
{
	int nongit = !startup_info->have_repository;
	char *value;

	given_config_source.file = getenv(CONFIG_ENVIRONMENT);

	argc = parse_options(argc, argv, prefix, builtin_config_options,
			     builtin_config_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (use_global_config + use_system_config + use_local_config +
	    !!given_config_source.file + !!given_config_source.blob > 1) {
		error("only one config file at a time.");
		usage_with_options(builtin_config_usage, builtin_config_options);
	}

	if (use_local_config && nongit)
		die(_("--local can only be used inside a git repository"));

	if (given_config_source.file &&
			!strcmp(given_config_source.file, "-")) {
		given_config_source.file = NULL;
		given_config_source.use_stdin = 1;
	}

	if (use_global_config) {
		char *user_config = expand_user_path("~/.gitconfig", 0);
		char *xdg_config = xdg_config_home("config");

		if (!user_config)
			/*
			 * It is unknown if HOME/.gitconfig exists, so
			 * we do not know if we should write to XDG
			 * location; error out even if XDG_CONFIG_HOME
			 * is set and points at a sane location.
			 */
			die("$HOME not set");

		if (access_or_warn(user_config, R_OK, 0) &&
		    xdg_config && !access_or_warn(xdg_config, R_OK, 0)) {
			given_config_source.file = xdg_config;
			free(user_config);
		} else {
			given_config_source.file = user_config;
			free(xdg_config);
		}
	}
	else if (use_system_config)
		given_config_source.file = git_etc_gitconfig();
	else if (use_local_config)
		given_config_source.file = git_pathdup("config");
	else if (given_config_source.file) {
		if (!is_absolute_path(given_config_source.file) && prefix)
			given_config_source.file =
				prefix_filename(prefix, given_config_source.file);
	}

	if (respect_includes_opt == -1)
		config_options.respect_includes = !given_config_source.file;
	else
		config_options.respect_includes = respect_includes_opt;
	if (!nongit) {
		config_options.commondir = get_git_common_dir();
		config_options.git_dir = get_git_dir();
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

	if ((actions & (ACTION_GET_COLOR|ACTION_GET_COLORBOOL)) && types) {
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
	if (omit_values &&
	    !(actions == ACTION_LIST || actions == ACTION_GET_REGEXP)) {
		error("--name-only is only applicable to --list or --get-regexp");
		usage_with_options(builtin_config_usage, builtin_config_options);
	}

	if (show_origin && !(actions &
		(ACTION_GET|ACTION_GET_ALL|ACTION_GET_REGEXP|ACTION_LIST))) {
		error("--show-origin is only applicable to --get, --get-all, "
			  "--get-regexp, and --list.");
		usage_with_options(builtin_config_usage, builtin_config_options);
	}

	if (actions == ACTION_LIST) {
		check_argc(argc, 0, 0);
		if (config_with_options(show_all_config, NULL,
					&given_config_source,
					&config_options) < 0) {
			if (given_config_source.file)
				die_errno("unable to read config file '%s'",
					  given_config_source.file);
			else
				die("error processing config file(s)");
		}
	}
	else if (actions == ACTION_EDIT) {
		char *config_file;

		check_argc(argc, 0, 0);
		if (!given_config_source.file && nongit)
			die("not in a git directory");
		if (given_config_source.use_stdin)
			die("editing stdin is not supported");
		if (given_config_source.blob)
			die("editing blobs is not supported");
		git_config(git_default_config, NULL);
		config_file = given_config_source.file ?
				xstrdup(given_config_source.file) :
				git_pathdup("config");
		if (use_global_config) {
			int fd = open(config_file, O_CREAT | O_EXCL | O_WRONLY, 0666);
			if (fd >= 0) {
				char *content = default_user_config();
				write_str_in_full(fd, content);
				free(content);
				close(fd);
			}
			else if (errno != EEXIST)
				die_errno(_("cannot create configuration file %s"), config_file);
		}
		launch_editor(config_file, NULL, NULL);
		free(config_file);
	}
	else if (actions == ACTION_SET) {
		int ret;
		check_write();
		check_argc(argc, 2, 2);
		value = normalize_value(argv[0], argv[1]);
		UNLEAK(value);
		ret = git_config_set_in_file_gently(given_config_source.file, argv[0], value);
		if (ret == CONFIG_NOTHING_SET)
			error(_("cannot overwrite multiple values with a single value\n"
			"       Use a regexp, --add or --replace-all to change %s."), argv[0]);
		return ret;
	}
	else if (actions == ACTION_SET_ALL) {
		check_write();
		check_argc(argc, 2, 3);
		value = normalize_value(argv[0], argv[1]);
		UNLEAK(value);
		return git_config_set_multivar_in_file_gently(given_config_source.file,
							      argv[0], value, argv[2], 0);
	}
	else if (actions == ACTION_ADD) {
		check_write();
		check_argc(argc, 2, 2);
		value = normalize_value(argv[0], argv[1]);
		UNLEAK(value);
		return git_config_set_multivar_in_file_gently(given_config_source.file,
							      argv[0], value,
							      CONFIG_REGEX_NONE, 0);
	}
	else if (actions == ACTION_REPLACE_ALL) {
		check_write();
		check_argc(argc, 2, 3);
		value = normalize_value(argv[0], argv[1]);
		UNLEAK(value);
		return git_config_set_multivar_in_file_gently(given_config_source.file,
							      argv[0], value, argv[2], 1);
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
	else if (actions == ACTION_GET_URLMATCH) {
		check_argc(argc, 2, 2);
		return get_urlmatch(argv[0], argv[1]);
	}
	else if (actions == ACTION_UNSET) {
		check_write();
		check_argc(argc, 1, 2);
		if (argc == 2)
			return git_config_set_multivar_in_file_gently(given_config_source.file,
								      argv[0], NULL, argv[1], 0);
		else
			return git_config_set_in_file_gently(given_config_source.file,
							     argv[0], NULL);
	}
	else if (actions == ACTION_UNSET_ALL) {
		check_write();
		check_argc(argc, 1, 2);
		return git_config_set_multivar_in_file_gently(given_config_source.file,
							      argv[0], NULL, argv[1], 1);
	}
	else if (actions == ACTION_RENAME_SECTION) {
		int ret;
		check_write();
		check_argc(argc, 2, 2);
		ret = git_config_rename_section_in_file(given_config_source.file,
							argv[0], argv[1]);
		if (ret < 0)
			return ret;
		if (ret == 0)
			die("No such section!");
	}
	else if (actions == ACTION_REMOVE_SECTION) {
		int ret;
		check_write();
		check_argc(argc, 1, 1);
		ret = git_config_rename_section_in_file(given_config_source.file,
							argv[0], NULL);
		if (ret < 0)
			return ret;
		if (ret == 0)
			die("No such section!");
	}
	else if (actions == ACTION_GET_COLOR) {
		check_argc(argc, 1, 2);
		get_color(argv[0], argv[1]);
	}
	else if (actions == ACTION_GET_COLORBOOL) {
		check_argc(argc, 1, 2);
		if (argc == 2)
			color_stdout_is_tty = git_config_bool("command line", argv[1]);
		return get_colorbool(argv[0], argc == 2);
	}

	return 0;
}
