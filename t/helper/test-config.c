#include "test-tool.h"
#include "cache.h"
#include "config.h"
#include "string-list.h"

/*
 * This program exposes the C API of the configuration mechanism
 * as a set of simple commands in order to facilitate testing.
 *
 * Reads stdin and prints result of command to stdout:
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
	struct config_set cs;
	enum test_config_exit_code ret = TC_SUCCESS;

	if (argc == 3 && !strcmp(argv[1], "read_early_config")) {
		read_early_config(early_config_cb, (void *)argv[2]);
		return TC_SUCCESS;
	}

	setup_git_directory();

	git_configset_init(&cs);

	if (argc < 2)
		goto print_usage_error;

	if (argc == 3 && !strcmp(argv[1], "get_value")) {
		if (!git_config_get_value(argv[2], &v)) {
			if (!v)
				printf("(NULL)\n");
			else
				printf("%s\n", v);
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_value_multi")) {
		strptr = git_config_get_value_multi(argv[2]);
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
		if (!git_config_get_int(argv[2], &val)) {
			printf("%d\n", val);
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_bool")) {
		if (!git_config_get_bool(argv[2], &val)) {
			printf("%d\n", val);
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_string")) {
		if (!git_config_get_string_const(argv[2], &v)) {
			printf("%s\n", v);
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			ret = TC_VALUE_NOT_FOUND;
		}
	} else if (argc >= 3 && !strcmp(argv[1], "configset_get_value")) {
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
		git_config(iterate_cb, NULL);
	} else {
print_usage_error:
		fprintf(stderr, "Invalid syntax. Usage: test-tool config <cmd> [args]\n");
		ret = TC_USAGE_ERROR;
	}

out:
	git_configset_clear(&cs);
	return ret;
}
