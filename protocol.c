#include "cache.h"
#include "config.h"
#include "protocol.h"

static enum protocol_version *allowed_versions;
static int nr_allowed_versions;
static int alloc_allowed_versions;
static int version_registration_locked = 0;

static const char protocol_v0_string[] = "0";
static const char protocol_v1_string[] = "1";
static const char protocol_v2_string[] = "2";

static enum protocol_version parse_protocol_version(const char *value)
{
	if (!strcmp(value, protocol_v0_string))
		return protocol_v0;
	else if (!strcmp(value, protocol_v1_string))
		return protocol_v1;
	else if (!strcmp(value, protocol_v2_string))
		return protocol_v2;
	else
		return protocol_unknown_version;
}

/* Return the text representation of a wire protocol version. */
static const char *format_protocol_version(enum protocol_version version)
{
	switch (version) {
	case protocol_v0:
		return protocol_v0_string;
	case protocol_v1:
		return protocol_v1_string;
	case protocol_v2:
		return protocol_v2_string;
	case protocol_unknown_version:
		die(_("Unrecognized protocol version"));
	}
	die(_("Unrecognized protocol_version"));
}

enum protocol_version get_protocol_version_config(void)
{
	const char *value;
	if (!git_config_get_string_const("protocol.version", &value)) {
		enum protocol_version version = parse_protocol_version(value);

		if (version == protocol_unknown_version)
			die("unknown value for config 'protocol.version': %s",
			    value);

		return version;
	}

	return protocol_v0;
}

void register_allowed_protocol_version(enum protocol_version version)
{
	if (version_registration_locked)
		BUG("late attempt to register an allowed protocol version");

	ALLOC_GROW(allowed_versions, nr_allowed_versions + 1,
		   alloc_allowed_versions);
	allowed_versions[nr_allowed_versions++] = version;
}

void register_allowed_protocol_versions_from_env(void)
{
	const char *git_protocol = getenv(GIT_PROTOCOL_ENVIRONMENT);
	const char *version_str;
	struct string_list version_list = STRING_LIST_INIT_DUP;
	struct string_list_item *version;

	if (!git_protocol)
		return;

	string_list_split(&version_list, git_protocol, ':', -1);
	for_each_string_list_item(version, &version_list) {
		if (skip_prefix(version->string, "version=", &version_str))
			register_allowed_protocol_version(
				parse_protocol_version(version_str));
	}
	string_list_clear(&version_list, 0);
}

static int is_allowed_protocol_version(enum protocol_version version)
{
	int i;
	version_registration_locked = 1;
	for (i = 0; i < nr_allowed_versions; i++)
		if (version == allowed_versions[i])
			return 1;
	return 0;
}

void get_client_protocol_version_advertisement(struct strbuf *advert)
{
	int i, tmp_nr = nr_allowed_versions;
	enum protocol_version *tmp_allowed_versions, config_version;
	strbuf_reset(advert);

	version_registration_locked = 1;

	config_version = get_protocol_version_config();
	if (config_version == protocol_v0) {
		strbuf_addstr(advert, "version=0");
		return;
	}

	if (tmp_nr > 0) {
		ALLOC_ARRAY(tmp_allowed_versions, tmp_nr);
		copy_array(tmp_allowed_versions, allowed_versions, tmp_nr,
			   sizeof(enum protocol_version));
	} else {
		ALLOC_ARRAY(tmp_allowed_versions, 1);
		tmp_nr = 1;
		tmp_allowed_versions[0] = config_version;
	}

	if (tmp_allowed_versions[0] != config_version)
		for (i = 1; i < nr_allowed_versions; i++)
			if (tmp_allowed_versions[i] == config_version) {
				SWAP(tmp_allowed_versions[0],
				     tmp_allowed_versions[i]);
			}

	strbuf_addf(advert, "version=%s",
		    format_protocol_version(tmp_allowed_versions[0]));
	for (i = 1; i < tmp_nr; i++)
		strbuf_addf(advert, ":version=%s",
			    format_protocol_version(tmp_allowed_versions[i]));

	free(tmp_allowed_versions);
}

enum protocol_version determine_protocol_version_server(void)
{
	const char *git_protocol = getenv(GIT_PROTOCOL_ENVIRONMENT);
	enum protocol_version version = protocol_v0;

	/*
	 * Determine which protocol version the client has requested.  Since
	 * multiple 'version' keys can be sent by the client, indicating that
	 * the client is okay to speak any of them, select the first
	 * recognizable version that the client has requested.  This is due to
	 * the assumption that the protocol versions will be listed in
	 * preference order.
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
				if (v != protocol_unknown_version &&
				    is_allowed_protocol_version(v)) {
					version = v;
					break;
				}
			}
		}

		string_list_clear(&list, 0);
	}

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
