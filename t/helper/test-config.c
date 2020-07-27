#include "test-tool.h"
#include "cache.h"
#include "config.h"
#include "string-list.h"
#include "submodule-config.h"

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
 *     129: test-config usage error
 *
 * Note: tests may also expect 128 for die() calls in the config machinery.
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

enum test_config_exit_code {
	TC_SUCCESS = 0,
	TC_VALUE_NOT_FOUND = 1,
	TC_CONFIG_FILE_ERROR = 2,
	TC_USAGE_ERROR = 129,
};

int cmd__config(int argc, const char **argv)
{
	int i, val;
	const char *v;
	const struct string_list *strptr;
	struct config_set cs = { .hash_initialized = 0 };
	enum test_config_exit_code ret = TC_SUCCESS;
	struct repository *repo = the_repository;
	const char *subrepo_path = NULL;

	argc--; /* skip over "config" */
	argv++;

	if (argc == 0)
		goto print_usage_error;

	if (skip_prefix(*argv, "--submodule=", &subrepo_path)) {
		argc--;
		argv++;
		if (argc == 0)
			goto print_usage_error;
	}

	if (argc == 2 && !strcmp(argv[0], "read_early_config")) {
		if (subrepo_path) {
			fprintf(stderr, "Cannot use --submodule with read_early_config\n");
			return TC_USAGE_ERROR;
		}
		read_early_config(early_config_cb, (void *)argv[1]);
		return TC_SUCCESS;
	}

	setup_git_directory();
	git_configset_init(&cs);

	if (subrepo_path) {
		const struct submodule *sub;
		struct repository *subrepo = xcalloc(1, sizeof(*repo));

		sub = submodule_from_path(the_repository, &null_oid, subrepo_path);
		if (!sub || repo_submodule_init(subrepo, the_repository, sub)) {
			fprintf(stderr, "Invalid argument to --submodule: '%s'\n",
				subrepo_path);
			free(subrepo);
			ret = TC_USAGE_ERROR;
			goto out;
		}
		repo = subrepo;
	}

	if (argc == 2 && !strcmp(argv[0], "get_value")) {
		if (!repo_config_get_value(repo, argv[1], &v)) {
			if (!v)
				printf("(NULL)\n");
			else
				printf("%s\n", v);
		} else {
			printf("Value not found for \"%s\"\n", argv[1]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 2 && !strcmp(argv[0], "get_value_multi")) {
		strptr = repo_config_get_value_multi(repo, argv[1]);
		if (strptr) {
			for (i = 0; i < strptr->nr; i++) {
				v = strptr->items[i].string;
				if (!v)
					printf("(NULL)\n");
				else
					printf("%s\n", v);
			}
		} else {
			printf("Value not found for \"%s\"\n", argv[1]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 2 && !strcmp(argv[0], "get_int")) {
		if (!repo_config_get_int(repo, argv[1], &val)) {
			printf("%d\n", val);
		} else {
			printf("Value not found for \"%s\"\n", argv[1]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 2 && !strcmp(argv[0], "get_bool")) {
		if (!repo_config_get_bool(repo, argv[1], &val)) {
			printf("%d\n", val);
		} else {

			printf("Value not found for \"%s\"\n", argv[1]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 2 && !strcmp(argv[0], "get_string")) {
		if (!repo_config_get_string_const(repo, argv[1], &v)) {
			printf("%s\n", v);
		} else {
			printf("Value not found for \"%s\"\n", argv[1]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc >= 2 && !strcmp(argv[0], "configset_get_value")) {
		if (subrepo_path) {
			fprintf(stderr, "Cannot use --submodule with configset_get_value\n");
			ret = TC_USAGE_ERROR;
			goto out;
		}
		for (i = 2; i < argc; i++) {
			int err;
			if ((err = git_configset_add_file(&cs, argv[i]))) {
				fprintf(stderr, "Error (%d) reading configuration file %s.\n", err, argv[i]);
				ret = TC_CONFIG_FILE_ERROR;
				goto out;
			}
		}
		if (!git_configset_get_value(&cs, argv[1], &v)) {
			if (!v)
				printf("(NULL)\n");
			else
				printf("%s\n", v);
		} else {
			printf("Value not found for \"%s\"\n", argv[1]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc >= 2 && !strcmp(argv[0], "configset_get_value_multi")) {
		if (subrepo_path) {
			fprintf(stderr, "Cannot use --submodule with configset_get_value_multi\n");
			ret = TC_USAGE_ERROR;
			goto out;
		}
		for (i = 2; i < argc; i++) {
			int err;
			if ((err = git_configset_add_file(&cs, argv[i]))) {
				fprintf(stderr, "Error (%d) reading configuration file %s.\n", err, argv[i]);
				ret = TC_CONFIG_FILE_ERROR;
				goto out;
			}
		}
		strptr = git_configset_get_value_multi(&cs, argv[1]);
		if (strptr) {
			for (i = 0; i < strptr->nr; i++) {
				v = strptr->items[i].string;
				if (!v)
					printf("(NULL)\n");
				else
					printf("%s\n", v);
			}
		} else {
			printf("Value not found for \"%s\"\n", argv[1]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (!strcmp(argv[0], "iterate")) {
		repo_config(repo, iterate_cb, NULL);
	} else {
print_usage_error:
		fprintf(stderr, "Invalid syntax. Usage: test-tool config"
				" [--submodule=<path>] <cmd> [args]\n");
		ret = TC_USAGE_ERROR;
	}

out:
	git_configset_clear(&cs);
	if (repo != the_repository) {
		repo_clear(repo);
		free(repo);
	}
	return ret;
}
