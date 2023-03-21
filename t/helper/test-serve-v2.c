#include "test-tool.h"
#include "gettext.h"
#include "parse-options.h"
#include "serve.h"
#include "setup.h"

static char const * const serve_usage[] = {
	N_("test-tool serve-v2 [<options>]"),
	NULL
};

int cmd__serve_v2(int argc, const char **argv)
{
	int stateless_rpc = 0;
	int advertise_capabilities = 0;
	struct option options[] = {
		OPT_BOOL(0, "stateless-rpc", &stateless_rpc,
			 N_("quit after a single request/response exchange")),
		OPT_BOOL(0, "advertise-capabilities", &advertise_capabilities,
			 N_("exit immediately after advertising capabilities")),
		OPT_END()
	};
	const char *prefix = setup_git_directory();

	/* ignore all unknown cmdline switches for now */
	argc = parse_options(argc, argv, prefix, options, serve_usage,
			     PARSE_OPT_KEEP_DASHDASH |
			     PARSE_OPT_KEEP_UNKNOWN_OPT);

	if (advertise_capabilities)
		protocol_v2_advertise_capabilities();
	else
		protocol_v2_serve_loop(stateless_rpc);

	return 0;
}
