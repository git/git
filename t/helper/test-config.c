#include "test-tool.h"
#include "config.h"
#include "setup.h"
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
 * get -> print return value for the entered key
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
 * git_config_int -> iterate over all values using git_config() and print the
 *                   integer value for the entered key or die
 *
 * Examples:
 *
 * To print the value with highest priority for key "foo.bAr Baz.rock":
 * 	test-tool config get_value "foo.bAr Baz.rock"
 *
 */

static int iterate_cb(const char *var, const char *value,
		      const struct config_context *ctx,
		      void *data UNUSED)
{
	const struct key_value_info *kvi = ctx->kvi;
	static int nr;

	if (nr++)
		putchar('\n');

	printf("key=%s\n", var);
	printf("value=%s\n", value ? value : "(null)");
	printf("origin=%s\n", config_origin_type_name(kvi->origin_type));
	printf("name=%s\n", kvi->filename ? kvi->filename : "");
	printf("lno=%d\n", kvi->linenr);
	printf("scope=%s\n", config_scope_name(kvi->scope));

	return 0;
}

static int parse_int_cb(const char *var, const char *value,
			const struct config_context *ctx, void *data)
{
	const char *key_to_match = data;

	if (!strcmp(key_to_match, var)) {
		int parsed = git_config_int(value, value, ctx->kvi);
		printf("%d\n", parsed);
	}
	return 0;
}

static int early_config_cb(const char *var, const char *value,
			   const struct config_context *ctx UNUSED,
			   void *vdata)
{
	const char *key = vdata;

	if (!strcmp(key, var))
		printf("%s\n", value);

	return 0;
}

int cmd__config(int argc, const char **argv)
{
	int i, val;
	const char *v;
	const struct string_list *strptr;
	struct config_set cs;

	if (argc == 3 && !strcmp(argv[1], "read_early_config")) {
		read_early_config(early_config_cb, (void *)argv[2]);
		return 0;
	}

	setup_git_directory();

	git_configset_init(&cs);

	if (argc < 2) {
		fprintf(stderr, "Please, provide a command name on the command-line\n");
		goto exit1;
	} else if (argc == 3 && !strcmp(argv[1], "get_value")) {
		if (!git_config_get_value(argv[2], &v)) {
			if (!v)
				printf("(NULL)\n");
			else
				printf("%s\n", v);
			goto exit0;
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			goto exit1;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_value_multi")) {
		if (!git_config_get_value_multi(argv[2], &strptr)) {
			for (i = 0; i < strptr->nr; i++) {
				v = strptr->items[i].string;
				if (!v)
					printf("(NULL)\n");
				else
					printf("%s\n", v);
			}
			goto exit0;
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			goto exit1;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get")) {
		int ret;

		if (!(ret = git_config_get(argv[2])))
			goto exit0;
		else if (ret == 1)
			printf("Value not found for \"%s\"\n", argv[2]);
		else if (ret == -CONFIG_INVALID_KEY)
			printf("Key \"%s\" is invalid\n", argv[2]);
		else if (ret == -CONFIG_NO_SECTION_OR_NAME)
			printf("Key \"%s\" has no section\n", argv[2]);
		else
			/*
			 * A normal caller should just check "ret <
			 * 0", but for our own tests let's BUG() if
			 * our whitelist of git_config_parse_key()
			 * return values isn't exhaustive.
			 */
			BUG("Key \"%s\" has unknown return %d", argv[2], ret);
		goto exit1;
	} else if (argc == 3 && !strcmp(argv[1], "get_int")) {
		if (!git_config_get_int(argv[2], &val)) {
			printf("%d\n", val);
			goto exit0;
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			goto exit1;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_bool")) {
		if (!git_config_get_bool(argv[2], &val)) {
			printf("%d\n", val);
			goto exit0;
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			goto exit1;
		}
	} else if (argc == 3 && !strcmp(argv[1], "get_string")) {
		if (!git_config_get_string_tmp(argv[2], &v)) {
			printf("%s\n", v);
			goto exit0;
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			goto exit1;
		}
	} else if (!strcmp(argv[1], "configset_get_value")) {
		for (i = 3; i < argc; i++) {
			int err;
			if ((err = git_configset_add_file(&cs, argv[i]))) {
				fprintf(stderr, "Error (%d) reading configuration file %s.\n", err, argv[i]);
				goto exit2;
			}
		}
		if (!git_configset_get_value(&cs, argv[2], &v, NULL)) {
			if (!v)
				printf("(NULL)\n");
			else
				printf("%s\n", v);
			goto exit0;
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			goto exit1;
		}
	} else if (!strcmp(argv[1], "configset_get_value_multi")) {
		for (i = 3; i < argc; i++) {
			int err;
			if ((err = git_configset_add_file(&cs, argv[i]))) {
				fprintf(stderr, "Error (%d) reading configuration file %s.\n", err, argv[i]);
				goto exit2;
			}
		}
		if (!git_configset_get_value_multi(&cs, argv[2], &strptr)) {
			for (i = 0; i < strptr->nr; i++) {
				v = strptr->items[i].string;
				if (!v)
					printf("(NULL)\n");
				else
					printf("%s\n", v);
			}
			goto exit0;
		} else {
			printf("Value not found for \"%s\"\n", argv[2]);
			goto exit1;
		}
	} else if (!strcmp(argv[1], "iterate")) {
		git_config(iterate_cb, NULL);
		goto exit0;
	} else if (argc == 3 && !strcmp(argv[1], "git_config_int")) {
		git_config(parse_int_cb, (void *) argv[2]);
		goto exit0;
	}

	die("%s: Please check the syntax and the function name", argv[0]);

exit0:
	git_configset_clear(&cs);
	return 0;

exit1:
	git_configset_clear(&cs);
	return 1;

exit2:
	git_configset_clear(&cs);
	return 2;
}
