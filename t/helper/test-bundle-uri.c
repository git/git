#include "test-tool.h"
#include "parse-options.h"
#include "bundle-uri.h"
#include "gettext.h"
#include "strbuf.h"
#include "string-list.h"
#include "transport.h"
#include "ref-filter.h"
#include "remote.h"
#include "refs.h"

enum input_mode {
	KEY_VALUE_PAIRS,
	CONFIG_FILE,
};

static int cmd__bundle_uri_parse(int argc, const char **argv, enum input_mode mode)
{
	const char *key_value_usage[] = {
		"test-tool bundle-uri parse-key-values <input>",
		NULL
	};
	const char *config_usage[] = {
		"test-tool bundle-uri parse-config <input>",
		NULL
	};
	const char **usage = key_value_usage;
	struct option options[] = {
		OPT_END(),
	};
	struct strbuf sb = STRBUF_INIT;
	struct bundle_list list;
	int err = 0;
	FILE *fp;

	if (mode == CONFIG_FILE)
		usage = config_usage;

	argc = parse_options(argc, argv, NULL, options, usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	init_bundle_list(&list);

	list.baseURI = xstrdup("<uri>");

	switch (mode) {
	case KEY_VALUE_PAIRS:
		if (argc != 1)
			goto usage;
		fp = fopen(argv[0], "r");
		if (!fp)
			die("failed to open '%s'", argv[0]);
		while (strbuf_getline(&sb, fp) != EOF) {
			if (bundle_uri_parse_line(&list, sb.buf))
				err = error("bad line: '%s'", sb.buf);
		}
		fclose(fp);
		break;

	case CONFIG_FILE:
		if (argc != 1)
			goto usage;
		err = bundle_uri_parse_config_format("<uri>", argv[0], &list);
		break;
	}
	strbuf_release(&sb);

	print_bundle_list(stdout, &list);

	clear_bundle_list(&list);

	return !!err;

usage:
	usage_with_options(usage, options);
}

static int cmd_ls_remote(int argc, const char **argv)
{
	const char *dest;
	struct remote *remote;
	struct transport *transport;
	int status = 0;

	dest = argc > 1 ? argv[1] : NULL;

	remote = remote_get(dest);
	if (!remote) {
		if (dest)
			die(_("bad repository '%s'"), dest);
		die(_("no remote configured to get bundle URIs from"));
	}
	if (!remote->url_nr)
		die(_("remote '%s' has no configured URL"), dest);

	transport = transport_get(remote, NULL);
	if (transport_get_remote_bundle_uri(transport) < 0) {
		error(_("could not get the bundle-uri list"));
		status = 1;
		goto cleanup;
	}

	print_bundle_list(stdout, transport->bundles);

cleanup:
	if (transport_disconnect(transport))
		return 1;
	return status;
}

int cmd__bundle_uri(int argc, const char **argv)
{
	const char *usage[] = {
		"test-tool bundle-uri <subcommand> [<options>]",
		NULL
	};
	struct option options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL, options, usage,
			     PARSE_OPT_STOP_AT_NON_OPTION |
			     PARSE_OPT_KEEP_ARGV0);
	if (argc == 1)
		goto usage;

	if (!strcmp(argv[1], "parse-key-values"))
		return cmd__bundle_uri_parse(argc - 1, argv + 1, KEY_VALUE_PAIRS);
	if (!strcmp(argv[1], "parse-config"))
		return cmd__bundle_uri_parse(argc - 1, argv + 1, CONFIG_FILE);
	if (!strcmp(argv[1], "ls-remote"))
		return cmd_ls_remote(argc - 1, argv + 1);
	error("there is no test-tool bundle-uri tool '%s'", argv[1]);

usage:
	usage_with_options(usage, options);
}
