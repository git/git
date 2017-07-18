#include "git-compat-util.h"
#include "credential.h"
#include "builtin.h"

static const char usage_msg[] =
	"git credential [fill|approve|reject]";

int cmd_credential(int argc, const char **argv, const char *prefix)
{
	const char *op;
	struct credential c = CREDENTIAL_INIT;

	op = argv[1];
	if (!op)
		usage(usage_msg);

	if (credential_read(&c, stdin) < 0)
		die("unable to read credential from stdin");

	if (!strcmp(op, "fill")) {
		credential_fill(&c);
		credential_write(&c, stdout);
	} else if (!strcmp(op, "approve")) {
		credential_approve(&c);
	} else if (!strcmp(op, "reject")) {
		credential_reject(&c);
	} else {
		usage(usage_msg);
	}
	return 0;
}
