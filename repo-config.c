#include "cache.h"
#include <regex.h>

static const char git_config_set_usage[] =
"git-repo-config [ --bool | --int ] [--get | --get-all | --replace-all | --unset | --unset-all] name [value [value_regex]] | --list";

static char* key = NULL;
static char* value = NULL;
static regex_t* key_regexp = NULL;
static regex_t* regexp = NULL;
static int show_keys = 0;
static int use_key_regexp = 0;
static int do_all = 0;
static int do_not_match = 0;
static int seen = 0;
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
	if (value_ == NULL)
		value_ = "";

	if ((use_key_regexp || !strcmp(key_, key)) &&
			(!use_key_regexp ||
			 !regexec(key_regexp, key_, 0, NULL, 0)) &&
			(regexp == NULL ||
			 (do_not_match ^
			  !regexec(regexp, value_, 0, NULL, 0)))) {
		if (show_keys)
			printf("%s ", key_);
		if (seen > 0) {
			if (!do_all)
				fprintf(stderr, "More than one value: %s\n",
						value);
			free(value);
		}

		if (type == T_INT) {
			value = malloc(256);
			sprintf(value, "%d", git_config_int(key_, value_));
		} else if (type == T_BOOL) {
			value = malloc(256);
			sprintf(value, "%s", git_config_bool(key_, value_)
					     ? "true" : "false");
		} else {
			value = strdup(value_);
		}
		seen++;
		if (do_all)
			printf("%s\n", value);
	}
	return 0;
}

static int get_value(const char* key_, const char* regex_)
{
	int i;

	key = malloc(strlen(key_)+1);
	for (i = 0; key_[i]; i++)
		key[i] = tolower(key_[i]);
	key[i] = 0;

	if (use_key_regexp) {
		key_regexp = (regex_t*)malloc(sizeof(regex_t));
		if (regcomp(key_regexp, key, REG_EXTENDED)) {
			fprintf(stderr, "Invalid key pattern: %s\n", regex_);
			return -1;
		}
	}

	if (regex_) {
		if (regex_[0] == '!') {
			do_not_match = 1;
			regex_++;
		}

		regexp = (regex_t*)malloc(sizeof(regex_t));
		if (regcomp(regexp, regex_, REG_EXTENDED)) {
			fprintf(stderr, "Invalid pattern: %s\n", regex_);
			return -1;
		}
	}

	git_config(show_config);
	if (value) {
		if (!do_all)
			printf("%s\n", value);
		free(value);
	}
	free(key);
	if (regexp) {
		regfree(regexp);
		free(regexp);
	}

	if (do_all)
		return 0;

	return seen == 1 ? 0 : 1;
}

int main(int argc, const char **argv)
{
	setup_git_directory();

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
