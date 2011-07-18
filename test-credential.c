#include "cache.h"
#include "credential.h"
#include "string-list.h"
#include "parse-options.h"

int main(int argc, const char **argv)
{
	int reject = 0;
	struct credential c = { NULL };
	struct string_list methods = STRING_LIST_INIT_NODUP;
	const char *const usage[] = {
		"test-credential [options] [method...]",
		NULL
	};
	struct option options[] = {
		OPT_BOOLEAN(0, "reject", &reject, "reject"),
		OPT_STRING(0, "description", &c.description, "desc",
			   "description"),
		OPT_STRING(0, "unique", &c.unique, "token",
			   "unique"),
		OPT_STRING(0, "username", &c.username, "name", "username"),
		OPT_STRING(0, "password", &c.password, "pass", "password"),
		OPT_END()
	};
	int i;

	argc = parse_options(argc, argv, NULL, options, usage, 0);
	for (i = 0; i < argc; i++)
		string_list_append(&methods, argv[i]);
	/* credential_reject will try to free() */
	if (c.username)
		c.username = xstrdup(c.username);
	if (c.password)
		c.password = xstrdup(c.password);

	if (reject)
		credential_reject(&c, &methods);
	else
		credential_fill(&c, &methods);

	if (c.username)
		printf("username=%s\n", c.username);
	if (c.password)
		printf("password=%s\n", c.password);

	return 0;
}
