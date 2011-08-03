#include "cache.h"
#include "credential.h"
#include "parse-options.h"
#include "string-list.h"

int main(int argc, const char **argv)
{
	const char * const usage[] = {
		"git credential-getpass [options]",
		NULL
	};
	struct credential c = { NULL };
	int reject = 0;
	struct option options[] = {
		OPT_BOOLEAN(0, "reject", &reject,
			    "reject a stored credential"),
		OPT_STRING(0, "username", &c.username, "name",
			   "an existing username"),
		OPT_STRING(0, "description", &c.description, "desc",
			   "human-readable description of the credential"),
		OPT_STRING(0, "unique", &c.unique, "token",
			   "a unique context for the credential"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options, usage, 0);
	if (argc)
		usage_with_options(usage, options);

	if (reject)
		return 0;

	credential_getpass(&c);
	printf("username=%s\n", c.username);
	printf("password=%s\n", c.password);
	return 0;
}
