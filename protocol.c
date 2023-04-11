#include "cache.h"
#include "config.h"
#include "environment.h"
#include "protocol.h"
#include "trace2.h"

static enum protocol_version parse_protocol_version(const char *value)
{
	if (!strcmp(value, "0"))
		return protocol_v0;
	else if (!strcmp(value, "1"))
		return protocol_v1;
	else if (!strcmp(value, "2"))
		return protocol_v2;
	else
		return protocol_unknown_version;
}

enum protocol_version get_protocol_version_config(void)
{
	const char *value;
	const char *git_test_k = "GIT_TEST_PROTOCOL_VERSION";
	const char *git_test_v;

	if (!git_config_get_string_tmp("protocol.version", &value)) {
		enum protocol_version version = parse_protocol_version(value);

		if (version == protocol_unknown_version)
			die("unknown value for config 'protocol.version': %s",
			    value);

		return version;
	}

	git_test_v = getenv(git_test_k);
	if (git_test_v && *git_test_v) {
		enum protocol_version env = parse_protocol_version(git_test_v);

		if (env == protocol_unknown_version)
			die("unknown value for %s: %s", git_test_k, git_test_v);
		return env;
	}

	return protocol_v2;
}

enum protocol_version determine_protocol_version_server(void)
{
	const char *git_protocol = getenv(GIT_PROTOCOL_ENVIRONMENT);
	enum protocol_version version = protocol_v0;

	/*
	 * Determine which protocol version the client has requested.  Since
	 * multiple 'version' keys can be sent by the client, indicating that
	 * the client is okay to speak any of them, select the greatest version
	 * that the client has requested.  This is due to the assumption that
	 * the most recent protocol version will be the most state-of-the-art.
	 */
	if (git_protocol) {
		struct string_list list = STRING_LIST_INIT_DUP;
		const struct string_list_item *item;
		string_list_split(&list, git_protocol, ':', -1);

		for_each_string_list_item(item, &list) {
			const char *value;
			enum protocol_version v;

			if (skip_prefix(item->string, "version=", &value)) {
				v = parse_protocol_version(value);
				if (v > version)
					version = v;
			}
		}

		string_list_clear(&list, 0);
	}

	trace2_data_intmax("transfer", NULL, "negotiated-version", version);

	return version;
}

enum protocol_version determine_protocol_version_client(const char *server_response)
{
	enum protocol_version version = protocol_v0;

	if (skip_prefix(server_response, "version ", &server_response)) {
		version = parse_protocol_version(server_response);

		if (version == protocol_unknown_version)
			die("server is speaking an unknown protocol");
		if (version == protocol_v0)
			die("protocol error: server explicitly said version 0");
	}

	return version;
}
