#include "cache.h"
#include <regex.h>

static const char git_config_set_usage[] =
"git-repo-config [--get | --get-all | --replace-all | --unset | --unset-all] name [value [value_regex]]";

static char* key = NULL;
static char* value = NULL;
static regex_t* regexp = NULL;
static int do_all = 0;
static int do_not_match = 0;
static int seen = 0;

static int show_config(const char* key_, const char* value_)
{
	if (!strcmp(key_, key) &&
			(regexp == NULL ||
			 (do_not_match ^
			  !regexec(regexp, value_, 0, NULL, 0)))) {
		if (do_all) {
			printf("%s\n", value_);
			return 0;
		}
		if (seen > 0) {
			fprintf(stderr, "More than one value: %s\n", value);
			free(value);
		}
		value = strdup(value_);
		seen++;
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

	i = git_config(show_config);
	if (value) {
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
