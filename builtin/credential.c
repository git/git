#include "git-compat-util.h"
#include "credential.h"
#include "builtin.h"
#include "config.h"

static const char usage_msg[] =
	"git credential (fill|approve|reject)";

int cmd_credential(int argc, const char **argv, const char *prefix UNUSED)
{
	const char *op;
	struct credential c = CREDENTIAL_INIT;

	git_config(git_default_config, NULL);

	if (argc != 2 || !strcmp(argv[1], "-h"))
		usage(usage_msg);
	op = argv[1];

	if (!strcmp(op, "capability")) {
		credential_set_all_capabilities(&c, CREDENTIAL_OP_INITIAL);
		credential_announce_capabilities(&c, stdout);
		return 0;
	}

	if (credential_read(&c, stdin, CREDENTIAL_OP_INITIAL) < 0)
		die("unable to read credential from stdin");

	if (!strcmp(op, "fill")) {
		credential_fill(&c, 0);
		credential_next_state(&c);
		credential_write(&c, stdout, CREDENTIAL_OP_RESPONSE);
	} else if (!strcmp(op, "approve")) {
		credential_set_all_capabilities(&c, CREDENTIAL_OP_HELPER);
		credential_approve(&c);
	} else if (!strcmp(op, "reject")) {
		credential_set_all_capabilities(&c, CREDENTIAL_OP_HELPER);
		credential_reject(&c);
	} else {
		usage(usage_msg);
	}

	credential_clear(&c);
	return 0;
}
