#include "cache.h"
#include "credential.h"
#include "string-list.h"

static const char usage_msg[] =
"test-credential <fill|approve|reject> [helper...]";

int main(int argc, const char **argv)
{
	const char *op;
	struct credential c = CREDENTIAL_INIT;
	int i;

	op = argv[1];
	if (!op)
		usage(usage_msg);
	for (i = 2; i < argc; i++)
		string_list_append(&c.helpers, argv[i]);

	if (credential_read(&c, stdin) < 0)
		die("unable to read credential from stdin");

	if (!strcmp(op, "fill")) {
		credential_fill(&c);
		if (c.username)
			printf("username=%s\n", c.username);
		if (c.password)
			printf("password=%s\n", c.password);
	}
	else if (!strcmp(op, "approve"))
		credential_approve(&c);
	else if (!strcmp(op, "reject"))
		credential_reject(&c);
	else
		usage(usage_msg);

	return 0;
}
