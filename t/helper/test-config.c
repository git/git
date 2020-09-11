#include "test-tool.h"
#include "cache.h"
#include "config.h"
#include "string-list.h"
#include "submodule-config.h"
#include "parse-options.h"

/*
 * This program exposes the C API of the configuration mechanism
 * as a set of simple commands in order to facilitate testing.
 *
 * Usage: test-tool config [--submodule=<path>] <cmd> [<args>]
 *
 * If --submodule=<path> is given, <cmd> will operate on the submodule at the
 * given <path>. This option is not valid for the commands: read_early_config,
 * configset_get_value and configset_get_value_multi.
 *
 * Possible cmds are:
 *
 * get_value -> prints the value with highest priority for the entered key
 *
 * get_value_multi -> prints all values for the entered key in increasing order
 *		     of priority
 *
 * get_int -> print integer value for the entered key or die
 *
 * get_bool -> print bool value for the entered key or die
 *
 * get_string -> print string value for the entered key or die
 *
 * configset_get_value -> returns value with the highest priority for the entered key
 * 			from a config_set constructed from files entered as arguments.
 *
 * configset_get_value_multi -> returns value_list for the entered key sorted in
 * 				ascending order of priority from a config_set
 * 				constructed from files entered as arguments.
 *
 * iterate -> iterate over all values using git_config(), and print some
 *            data for each
 *
 * Exit codes:
 *     0:   success
 *     1:   value not found for the given config key
 *     2:   config file path given as argument is inaccessible or doesn't exist
 *
 * Examples:
 *
 * To print the value with highest priority for key "foo.bAr Baz.rock":
 * 	test-tool config get_value "foo.bAr Baz.rock"
 *
 */

static int iterate_cb(const char *var, const char *value, void *data)
{
	static int nr;

	if (nr++)
		putchar('\n');

	printf("key=%s\n", var);
	printf("value=%s\n", value ? value : "(null)");
	printf("origin=%s\n", current_config_origin_type());
	printf("name=%s\n", current_config_name());
	printf("lno=%d\n", current_config_line());
	printf("scope=%s\n", config_scope_name(current_config_scope()));

	return 0;
}

static int early_config_cb(const char *var, const char *value, void *vdata)
{
	const char *key = vdata;

	if (!strcmp(key, var))
		printf("%s\n", value);

	return 0;
}

#define TC_VALUE_NOT_FOUND 1
#define TC_CONFIG_FILE_ERROR 2

static const char *test_config_usage[] = {
	"test-tool config [--submodule=<path>] <cmd> [<args>]",
	NULL
};

int cmd__config(int argc, const char **argv)
{
	int i, val, ret = 0;
	const char *v;
	const struct string_list *strptr;
	struct config_set cs;
	struct repository subrepo, *repo = the_repository;
	const char *subrepo_path = NULL;

	struct option options[] = {
		OPT_STRING(0, "submodule", &subrepo_path, "path",
			   "run <cmd> on the submodule at <path>"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options, test_config_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc < 2)
		usage_with_options(test_config_usage, options);

	if (argc == 3 && !strcmp(argv[1], "read_early_config")) {
		if (subrepo_path)
			die("cannot use --submodule with read_early_config");
		read_early_config(early_config_cb, (void *)argv[2]);
		return 0;
	}

	setup_git_directory();

	git_configset_init(&cs);

	if (subrepo_path) {
		const struct submodule *sub;

		sub = submodule_from_path(the_repository, &null_oid, subrepo_path);
		if (!sub || repo_submodule_init(&subrepo, the_repository, sub))
			die("invalid argument to --submodule: '%s'", subrepo_path);

		repo = &subrepo;
	}

	if (argc == 3 && !strcmp(argv[1], "get_value")) {
		if (!repo_config_get_value(repo, argv[2], &v)) {
			if (!v)
				printf("(NULL)\n");
			else
				printf("%s\n", v);
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_value_multi")) {
		strptr = repo_config_get_value_multi(repo, argv[2]);
		if (strptr) {
			for (i = 0; i < strptr->nr; i++) {
				v = strptr->items[i].string;
				if (!v)
					printf("(NULL)\n");
				else
					printf("%s\n", v);
			}
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_int")) {
		if (!repo_config_get_int(repo, argv[2], &val)) {
			printf("%d\n", val);
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_bool")) {
		if (!repo_config_get_bool(repo, argv[2], &val)) {
			printf("%d\n", val);
		} else {

			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_string")) {
		if (!repo_config_get_string_tmp(repo, argv[2], &v)) {
			printf("%s\n", v);
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc >= 3 && !strcmp(argv[1], "configset_get_value")) {
		if (subrepo_path)
			die("cannot use --submodule with configset_get_value");

		for (i = 3; i < argc; i++) {
			int err;
			if ((err = git_configset_add_file(&cs, argv[i]))) {
				fprintf(stderr, "Error (%d) reading configuration file %s.\n", err, argv[i]);
				ret = TC_CONFIG_FILE_ERROR;
				goto out;
			}
		}
		if (!git_configset_get_value(&cs, argv[2], &v)) {
			if (!v)
				printf("(NULL)\n");
			else
				printf("%s\n", v);
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc >= 3 && !strcmp(argv[1], "configset_get_value_multi")) {
		if (subrepo_path)
			die("cannot use --submodule with configset_get_value_multi");

		for (i = 3; i < argc; i++) {
			int err;
			if ((err = git_configset_add_file(&cs, argv[i]))) {
				fprintf(stderr, "Error (%d) reading configuration file %s.\n", err, argv[i]);
				ret = TC_CONFIG_FILE_ERROR;
				goto out;
			}
		}
		strptr = git_configset_get_value_multi(&cs, argv[2]);
		if (strptr) {
			for (i = 0; i < strptr->nr; i++) {
				v = strptr->items[i].string;
				if (!v)
					printf("(NULL)\n");
				else
					printf("%s\n", v);
			}
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (!strcmp(argv[1], "iterate")) {
		repo_config(repo, iterate_cb, NULL);
	} else {
		die("%s: Please check the syntax and the function name", argv[0]);
	}

out:
	git_configset_clear(&cs);
	if (repo != the_repository)
		repo_clear(repo);
	return ret;
}
