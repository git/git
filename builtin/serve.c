#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "serve.h"

static char const * const serve_usage[] = {
	N_("git serve [<options>]"),
	NULL
};

int cmd_serve(int argc, const char **argv, const char *prefix)
{
	struct serve_options opts = SERVE_OPTIONS_INIT;

	struct option options[] = {
		OPT_BOOL(0, "stateless-rpc", &opts.stateless_rpc,
			 N_("quit after a single request/response exchange")),
		OPT_BOOL(0, "advertise-capabilities", &opts.advertise_capabilities,
			 N_("exit immediately after advertising capabilities")),
		OPT_END()
	};

	/* ignore all unknown cmdline switches for now */
	argc = parse_options(argc, argv, prefix, options, serve_usage,
			     PARSE_OPT_KEEP_DASHDASH |
			     PARSE_OPT_KEEP_UNKNOWN);
	serve(&opts);

	return 0;
}
