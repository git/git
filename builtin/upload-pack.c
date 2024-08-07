#include "builtin.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "pkt-line.h"
#include "parse-options.h"
#include "path.h"
#include "protocol.h"
#include "replace-object.h"
#include "upload-pack.h"
#include "serve.h"
#include "commit.h"
#include "environment.h"

static const char * const upload_pack_usage[] = {
	N_("git-upload-pack [--[no-]strict] [--timeout=<n>] [--stateless-rpc]\n"
	   "                [--advertise-refs] <directory>"),
	NULL
};

int cmd_upload_pack(int argc, const char **argv, const char *prefix)
{
	const char *dir;
	int strict = 0;
	int advertise_refs = 0;
	int stateless_rpc = 0;
	int timeout = 0;
	struct option options[] = {
		OPT_BOOL(0, "stateless-rpc", &stateless_rpc,
			 N_("quit after a single request/response exchange")),
		OPT_HIDDEN_BOOL(0, "http-backend-info-refs", &advertise_refs,
				N_("serve up the info/refs for git-http-backend")),
		OPT_ALIAS(0, "advertise-refs", "http-backend-info-refs"),
		OPT_BOOL(0, "strict", &strict,
			 N_("do not try <directory>/.git/ if <directory> is no Git directory")),
		OPT_INTEGER(0, "timeout", &timeout,
			    N_("interrupt transfer after <n> seconds of inactivity")),
		OPT_END()
	};

	packet_trace_identity("upload-pack");
	disable_replace_refs();
	save_commit_buffer = 0;
	xsetenv(NO_LAZY_FETCH_ENVIRONMENT, "1", 0);

	argc = parse_options(argc, argv, prefix, options, upload_pack_usage, 0);

	if (argc != 1)
		usage_with_options(upload_pack_usage, options);

	setup_path();

	dir = argv[0];

	if (!enter_repo(dir, strict))
		die("'%s' does not appear to be a git repository", dir);

	switch (determine_protocol_version_server()) {
	case protocol_v2:
		if (advertise_refs)
			protocol_v2_advertise_capabilities();
		else
			protocol_v2_serve_loop(stateless_rpc);
		break;
	case protocol_v1:
		/*
		 * v1 is just the original protocol with a version string,
		 * so just fall through after writing the version string.
		 */
		if (advertise_refs || !stateless_rpc)
			packet_write_fmt(1, "version 1\n");

		/* fallthrough */
	case protocol_v0:
		upload_pack(advertise_refs, stateless_rpc, timeout);
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	return 0;
}
