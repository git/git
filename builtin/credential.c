#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "credential.h"
#include "builtin.h"
#include "environment.h"
#include "config.h"

static const char usage_msg[] =
	"git credential (fill|approve|reject|capability)";

static int cmd_credential_fill(int argc UNUSED, const char **argv UNUSED,
			       const char *prefix UNUSED,
			       struct repository *repo UNUSED)
{
	struct credential c = CREDENTIAL_INIT;

	if (credential_read(&c, stdin, CREDENTIAL_OP_INITIAL) < 0)
		die("unable to read credential from stdin");

	credential_fill(the_repository, &c, 0);
	credential_next_state(&c);
	credential_write(&c, stdout, CREDENTIAL_OP_RESPONSE);

	credential_clear(&c);
	return 0;
}

static int cmd_credential_approve(int argc UNUSED, const char **argv UNUSED,
				  const char *prefix UNUSED,
				  struct repository *repo UNUSED)
{
	struct credential c = CREDENTIAL_INIT;

	if (credential_read(&c, stdin, CREDENTIAL_OP_INITIAL) < 0)
		die("unable to read credential from stdin");

	credential_set_all_capabilities(&c, CREDENTIAL_OP_HELPER);
	credential_approve(the_repository, &c);

	credential_clear(&c);
	return 0;
}

static int cmd_credential_reject(int argc UNUSED, const char **argv UNUSED,
				 const char *prefix UNUSED,
				 struct repository *repo UNUSED)
{
	struct credential c = CREDENTIAL_INIT;

	if (credential_read(&c, stdin, CREDENTIAL_OP_INITIAL) < 0)
		die("unable to read credential from stdin");

	credential_set_all_capabilities(&c, CREDENTIAL_OP_HELPER);
	credential_reject(the_repository, &c);

	credential_clear(&c);
	return 0;
}

static int cmd_credential_capability(int argc UNUSED,
				     const char **argv UNUSED,
				     const char *prefix UNUSED,
				     struct repository *repo UNUSED)
{
	struct credential c = CREDENTIAL_INIT;

	credential_set_all_capabilities(&c, CREDENTIAL_OP_INITIAL);
	credential_announce_capabilities(&c, stdout);

	credential_clear(&c);
	return 0;
}

int cmd_credential(int argc,
		   const char **argv,
		   const char *prefix,
		   struct repository *repo)
{
	const char *op;

	repo_config(the_repository, git_default_config, NULL);

	show_usage_if_asked(argc, argv, usage_msg);
	if (argc != 2)
		usage(usage_msg);
	op = argv[1];

	if (!strcmp(op, "fill"))
		return cmd_credential_fill(argc - 1, argv + 1, prefix, repo);
	if (!strcmp(op, "approve"))
		return cmd_credential_approve(argc - 1, argv + 1, prefix, repo);
	if (!strcmp(op, "reject"))
		return cmd_credential_reject(argc - 1, argv + 1, prefix, repo);
	if (!strcmp(op, "capability"))
		return cmd_credential_capability(argc - 1, argv + 1, prefix,
						 repo);

	usage(usage_msg);
}
