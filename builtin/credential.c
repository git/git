#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "credential.h"
#include "builtin.h"
#include "environment.h"
#include "config.h"
#include "gettext.h"
#include "parse-options.h"

static const char * const credential_usage[] = {
	N_("git credential (fill|approve|reject|capability)"),
	NULL,
};

static const char * const credential_fill_usage[] = {
	N_("git credential fill"),
	NULL,
};

static const char * const credential_approve_usage[] = {
	N_("git credential approve"),
	NULL,
};

static const char * const credential_reject_usage[] = {
	N_("git credential reject"),
	NULL,
};

static const char * const credential_capability_usage[] = {
	N_("git credential capability"),
	NULL,
};

static void parse_no_args(int argc, const char **argv, const char *prefix,
			  const char * const *usagestr)
{
	struct option options[] = {
		OPT_END()
	};

	if (parse_options(argc, argv, prefix, options, usagestr, 0))
		usage_msg_opt(_("too many arguments"), usagestr, options);
}

static int cmd_credential_fill(int argc, const char **argv, const char *prefix,
			       struct repository *repo UNUSED)
{
	struct credential c = CREDENTIAL_INIT;

	parse_no_args(argc, argv, prefix, credential_fill_usage);

	if (credential_read(&c, stdin, CREDENTIAL_OP_INITIAL) < 0)
		die("unable to read credential from stdin");

	credential_fill(the_repository, &c, 0);
	credential_next_state(&c);
	credential_write(&c, stdout, CREDENTIAL_OP_RESPONSE);

	credential_clear(&c);
	return 0;
}

static int cmd_credential_approve(int argc, const char **argv,
				  const char *prefix,
				  struct repository *repo UNUSED)
{
	struct credential c = CREDENTIAL_INIT;

	parse_no_args(argc, argv, prefix, credential_approve_usage);

	if (credential_read(&c, stdin, CREDENTIAL_OP_INITIAL) < 0)
		die("unable to read credential from stdin");

	credential_set_all_capabilities(&c, CREDENTIAL_OP_HELPER);
	credential_approve(the_repository, &c);

	credential_clear(&c);
	return 0;
}

static int cmd_credential_reject(int argc, const char **argv,
				 const char *prefix,
				 struct repository *repo UNUSED)
{
	struct credential c = CREDENTIAL_INIT;

	parse_no_args(argc, argv, prefix, credential_reject_usage);

	if (credential_read(&c, stdin, CREDENTIAL_OP_INITIAL) < 0)
		die("unable to read credential from stdin");

	credential_set_all_capabilities(&c, CREDENTIAL_OP_HELPER);
	credential_reject(the_repository, &c);

	credential_clear(&c);
	return 0;
}

static int cmd_credential_capability(int argc, const char **argv,
				     const char *prefix,
				     struct repository *repo UNUSED)
{
	struct credential c = CREDENTIAL_INIT;

	parse_no_args(argc, argv, prefix, credential_capability_usage);

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
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("fill", &fn, cmd_credential_fill),
		OPT_SUBCOMMAND("approve", &fn, cmd_credential_approve),
		OPT_SUBCOMMAND("reject", &fn, cmd_credential_reject),
		OPT_SUBCOMMAND("capability", &fn, cmd_credential_capability),
		OPT_END()
	};

	repo_config(the_repository, git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, options, credential_usage, 0);

	return fn(argc, argv, prefix, repo);
}
