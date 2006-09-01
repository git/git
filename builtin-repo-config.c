#include "builtin.h"
#include "cache.h"
#include <regex.h>

static const char git_config_set_usage[] =
"git-repo-config [ --bool | --int ] [--get | --get-all | --get-regexp | --replace-all | --unset | --unset-all] name [value [value_regex]] | --list";

static char *key;
static regex_t *key_regexp;
static regex_t *regexp;
static int show_keys;
static int use_key_regexp;
static int do_all;
static int do_not_match;
static int seen;
static enum { T_RAW, T_INT, T_BOOL } type = T_RAW;

static int show_all_config(const char *key_, const char *value_)
{
	if (value_)
		printf("%s=%s\n", key_, value_);
	else
		printf("%s\n", key_);
	return 0;
}

static int show_config(const char* key_, const char* value_)
{
	char value[256];
	const char *vptr = value;
	int dup_error = 0;

	if (!use_key_regexp && strcmp(key_, key))
		return 0;
	if (use_key_regexp && regexec(key_regexp, key_, 0, NULL, 0))
		return 0;
	if (regexp != NULL &&
			 (do_not_match ^
			  regexec(regexp, (value_?value_:""), 0, NULL, 0)))
		return 0;

	if (show_keys)
		printf("%s ", key_);
	if (seen && !do_all)
		dup_error = 1;
	if (type == T_INT)
		sprintf(value, "%d", git_config_int(key_, value_?value_:""));
	else if (type == T_BOOL)
		vptr = git_config_bool(key_, value_) ? "true" : "false";
	else
		vptr = value_?value_:"";
	seen++;
	if (dup_error) {
		error("More than one value for the key %s: %s",
				key_, vptr);
	}
	else
		printf("%s\n", vptr);

	return 0;
}

static int get_value(const char* key_, const char* regex_)
{
	int ret = -1;
	char *tl;
	char *global = NULL, *repo_config = NULL;
	const char *local;

	local = getenv("GIT_CONFIG");
	if (!local) {
		const char *home = getenv("HOME");
		local = getenv("GIT_CONFIG_LOCAL");
		if (!local)
			local = repo_config = strdup(git_path("config"));
		if (home)
			global = strdup(mkpath("%s/.gitconfig", home));
	}

	key = strdup(key_);
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

	if (do_all && global)
		git_config_from_file(show_config, global);
	git_config_from_file(show_config, local);
	if (!do_all && !seen && global)
		git_config_from_file(show_config, global);

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

int cmd_repo_config(int argc, const char **argv, const char *prefix)
{
	int nongit = 0;
	setup_git_directory_gently(&nongit);

	while (1 < argc) {
		if (!strcmp(argv[1], "--int"))
			type = T_INT;
		else if (!strcmp(argv[1], "--bool"))
			type = T_BOOL;
		else if (!strcmp(argv[1], "--list") || !strcmp(argv[1], "-l"))
			return git_config(show_all_config);
		else
			break;
		argc--;
		argv++;
	}

	switch (argc) {
	case 2:
		return get_value(argv[1], NULL);
	case 3:
		if (!strcmp(argv[1], "--unset"))
			return git_config_set(argv[2], NULL);
		else if (!strcmp(argv[1], "--unset-all"))
			return git_config_set_multivar(argv[2], NULL, NULL, 1);
		else if (!strcmp(argv[1], "--get"))
			return get_value(argv[2], NULL);
		else if (!strcmp(argv[1], "--get-all")) {
			do_all = 1;
			return get_value(argv[2], NULL);
		} else if (!strcmp(argv[1], "--get-regexp")) {
			show_keys = 1;
			use_key_regexp = 1;
			do_all = 1;
			return get_value(argv[2], NULL);
		} else

			return git_config_set(argv[1], argv[2]);
	case 4:
		if (!strcmp(argv[1], "--unset"))
			return git_config_set_multivar(argv[2], NULL, argv[3], 0);
		else if (!strcmp(argv[1], "--unset-all"))
			return git_config_set_multivar(argv[2], NULL, argv[3], 1);
		else if (!strcmp(argv[1], "--get"))
			return get_value(argv[2], argv[3]);
		else if (!strcmp(argv[1], "--get-all")) {
			do_all = 1;
			return get_value(argv[2], argv[3]);
		} else if (!strcmp(argv[1], "--get-regexp")) {
			show_keys = 1;
			use_key_regexp = 1;
			do_all = 1;
			return get_value(argv[2], argv[3]);
		} else if (!strcmp(argv[1], "--replace-all"))

			return git_config_set_multivar(argv[2], argv[3], NULL, 1);
		else

			return git_config_set_multivar(argv[1], argv[2], argv[3], 0);
	case 5:
		if (!strcmp(argv[1], "--replace-all"))
			return git_config_set_multivar(argv[2], argv[3], argv[4], 1);
	case 1:
	default:
		usage(git_config_set_usage);
	}
	return 0;
}
