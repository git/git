/*
 * Builtin "git agent-orient"
 *
 * Produces a structured orientation summary for an agent dropped
 * into an unknown repository.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "agent.h"
#include "parse-options.h"
#include "strbuf.h"

static const char * const builtin_agent_orient_usage[] = {
	N_("git agent-orient [--max-tokens=<n>]"),
	NULL
};

int cmd_agent_orient(int argc,
		     const char **argv,
		     const char *prefix,
		     struct repository *repo UNUSED)
{
	int max_tokens = 2000;
	struct strbuf out = STRBUF_INIT;
	struct option options[] = {
		OPT_INTEGER(0, "max-tokens", &max_tokens,
			  N_("maximum approximate tokens to output")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     builtin_agent_orient_usage, 0);

	agent_orient_repo(&out, max_tokens);
	printf("%s", out.buf);
	strbuf_release(&out);
	return 0;
}
