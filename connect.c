#include "git-compat-util.h"
#include "cache.h"
#include "config.h"
#include "pkt-line.h"
#include "quote.h"
#include "refs.h"
#include "run-command.h"
#include "remote.h"
#include "connect.h"
#include "url.h"
#include "string-list.h"
#include "oid-array.h"
#include "transport.h"
#include "strbuf.h"
#include "version.h"
#include "protocol.h"
#include "alias.h"
#include "bundle-uri.h"

static char *server_capabilities_v1;
static struct strvec server_capabilities_v2 = STRVEC_INIT;
static const char *next_server_feature_value(const char *feature, int *len, int *offset);

static int check_ref(const char *name, unsigned int flags)
{
	if (!flags)
		return 1;

	if (!skip_prefix(name, "refs/", &name))
		return 0;

	/* REF_NORMAL means that we don't want the magic fake tag refs */
	if ((flags & REF_NORMAL) && check_refname_format(name, 0))
		return 0;

	/* REF_HEADS means that we want regular branch heads */
	if ((flags & REF_HEADS) && starts_with(name, "heads/"))
		return 1;

	/* REF_TAGS means that we want tags */
	if ((flags & REF_TAGS) && starts_with(name, "tags/"))
		return 1;

	/* All type bits clear means that we are ok with anything */
	return !(flags & ~REF_NORMAL);
}

int check_ref_type(const struct ref *ref, int flags)
{
	return check_ref(ref->name, flags);
}

static NORETURN void die_initial_contact(int unexpected)
{
	/*
	 * A hang-up after seeing some response from the other end
	 * means that it is unexpected, as we know the other end is
	 * willing to talk to us.  A hang-up before seeing any
	 * response does not necessarily mean an ACL problem, though.
	 */
	if (unexpected)
		die(_("the remote end hung up upon initial contact"));
	else
		die(_("Could not read from remote repository.\n\n"
		      "Please make sure you have the correct access rights\n"
		      "and the repository exists."));
}

/* Checks if the server supports the capability 'c' */
int server_supports_v2(const char *c, int die_on_error)
{
	int i;

	for (i = 0; i < server_capabilities_v2.nr; i++) {
		const char *out;
		if (skip_prefix(server_capabilities_v2.v[i], c, &out) &&
		    (!*out || *out == '='))
			return 1;
	}

	if (die_on_error)
		die(_("server doesn't support '%s'"), c);

	return 0;
}

int server_feature_v2(const char *c, const char **v)
{
	int i;

	for (i = 0; i < server_capabilities_v2.nr; i++) {
		const char *out;
		if (skip_prefix(server_capabilities_v2.v[i], c, &out) &&
		    (*out == '=')) {
			*v = out + 1;
			return 1;
		}
	}
	return 0;
}

int server_supports_feature(const char *c, const char *feature,
			    int die_on_error)
{
	int i;

	for (i = 0; i < server_capabilities_v2.nr; i++) {
		const char *out;
		if (skip_prefix(server_capabilities_v2.v[i], c, &out) &&
		    (!*out || *(out++) == '=')) {
			if (parse_feature_request(out, feature))
				return 1;
			else
				break;
		}
	}

	if (die_on_error)
		die(_("server doesn't support feature '%s'"), feature);

	return 0;
}

static void process_capabilities_v2(struct packet_reader *reader)
{
	while (packet_reader_read(reader) == PACKET_READ_NORMAL)
		strvec_push(&server_capabilities_v2, reader->line);

	if (reader->status != PACKET_READ_FLUSH)
		die(_("expected flush after capabilities"));
}

enum protocol_version discover_version(struct packet_reader *reader)
{
	enum protocol_version version = protocol_unknown_version;

	/*
	 * Peek the first line of the server's response to
	 * determine the protocol version the server is speaking.
	 */
	switch (packet_reader_peek(reader)) {
	case PACKET_READ_EOF:
		die_initial_contact(0);
	case PACKET_READ_FLUSH:
	case PACKET_READ_DELIM:
	case PACKET_READ_RESPONSE_END:
		version = protocol_v0;
		break;
	case PACKET_READ_NORMAL:
		version = determine_protocol_version_client(reader->line);
		break;
	}

	switch (version) {
	case protocol_v2:
		process_capabilities_v2(reader);
		break;
	case protocol_v1:
		/* Read the peeked version line */
		packet_reader_read(reader);
		break;
	case protocol_v0:
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	trace2_data_intmax("transfer", NULL, "negotiated-version", version);

	return version;
}

static void parse_one_symref_info(struct string_list *symref, const char *val, int len)
{
	char *sym, *target;
	struct string_list_item *item;

	if (!len)
		return; /* just "symref" */
	/* e.g. "symref=HEAD:refs/heads/master" */
	sym = xmemdupz(val, len);
	target = strchr(sym, ':');
	if (!target)
		/* just "symref=something" */
		goto reject;
	*(target++) = '\0';
	if (check_refname_format(sym, REFNAME_ALLOW_ONELEVEL) ||
	    check_refname_format(target, REFNAME_ALLOW_ONELEVEL))
		/* "symref=bogus:pair */
		goto reject;
	item = string_list_append_nodup(symref, sym);
	item->util = target;
	return;
reject:
	free(sym);
	return;
}

static void annotate_refs_with_symref_info(struct ref *ref)
{
	struct string_list symref = STRING_LIST_INIT_DUP;
	int offset = 0;

	while (1) {
		int len;
		const char *val;

		val = next_server_feature_value("symref", &len, &offset);
		if (!val)
			break;
		parse_one_symref_info(&symref, val, len);
	}
	string_list_sort(&symref);

	for (; ref; ref = ref->next) {
		struct string_list_item *item;
		item = string_list_lookup(&symref, ref->name);
		if (!item)
			continue;
		ref->symref = xstrdup((char *)item->util);
	}
	string_list_clear(&symref, 0);
}

static void process_capabilities(struct packet_reader *reader, int *linelen)
{
	const char *feat_val;
	int feat_len;
	const char *line = reader->line;
	int nul_location = strlen(line);
	if (nul_location == *linelen)
		return;
	server_capabilities_v1 = xstrdup(line + nul_location + 1);
	*linelen = nul_location;

	feat_val = server_feature_value("object-format", &feat_len);
	if (feat_val) {
		char *hash_name = xstrndup(feat_val, feat_len);
		int hash_algo = hash_algo_by_name(hash_name);
		if (hash_algo != GIT_HASH_UNKNOWN)
			reader->hash_algo = &hash_algos[hash_algo];
		free(hash_name);
	} else {
		reader->hash_algo = &hash_algos[GIT_HASH_SHA1];
	}
}

static int process_dummy_ref(const struct packet_reader *reader)
{
	const char *line = reader->line;
	struct object_id oid;
	const char *name;

	if (parse_oid_hex_algop(line, &oid, &name, reader->hash_algo))
		return 0;
	if (*name != ' ')
		return 0;
	name++;

	return oideq(null_oid(), &oid) && !strcmp(name, "capabilities^{}");
}

static void check_no_capabilities(const char *line, int len)
{
	if (strlen(line) != len)
		warning(_("ignoring capabilities after first line '%s'"),
			line + strlen(line));
}

static int process_ref(const struct packet_reader *reader, int len,
		       struct ref ***list, unsigned int flags,
		       struct oid_array *extra_have)
{
	const char *line = reader->line;
	struct object_id old_oid;
	const char *name;

	if (parse_oid_hex_algop(line, &old_oid, &name, reader->hash_algo))
		return 0;
	if (*name != ' ')
		return 0;
	name++;

	if (extra_have && !strcmp(name, ".have")) {
		oid_array_append(extra_have, &old_oid);
	} else if (!strcmp(name, "capabilities^{}")) {
		die(_("protocol error: unexpected capabilities^{}"));
	} else if (check_ref(name, flags)) {
		struct ref *ref = alloc_ref(name);
		oidcpy(&ref->old_oid, &old_oid);
		**list = ref;
		*list = &ref->next;
	}
	check_no_capabilities(line, len);
	return 1;
}

static int process_shallow(const struct packet_reader *reader, int len,
			   struct oid_array *shallow_points)
{
	const char *line = reader->line;
	const char *arg;
	struct object_id old_oid;

	if (!skip_prefix(line, "shallow ", &arg))
		return 0;

	if (get_oid_hex_algop(arg, &old_oid, reader->hash_algo))
		die(_("protocol error: expected shallow sha-1, got '%s'"), arg);
	if (!shallow_points)
		die(_("repository on the other end cannot be shallow"));
	oid_array_append(shallow_points, &old_oid);
	check_no_capabilities(line, len);
	return 1;
}

enum get_remote_heads_state {
	EXPECTING_FIRST_REF = 0,
	EXPECTING_REF,
	EXPECTING_SHALLOW,
	EXPECTING_DONE,
};

/*
 * Read all the refs from the other end
 */
struct ref **get_remote_heads(struct packet_reader *reader,
			      struct ref **list, unsigned int flags,
			      struct oid_array *extra_have,
			      struct oid_array *shallow_points)
{
	struct ref **orig_list = list;
	int len = 0;
	enum get_remote_heads_state state = EXPECTING_FIRST_REF;

	*list = NULL;

	while (state != EXPECTING_DONE) {
		switch (packet_reader_read(reader)) {
		case PACKET_READ_EOF:
			die_initial_contact(1);
		case PACKET_READ_NORMAL:
			len = reader->pktlen;
			break;
		case PACKET_READ_FLUSH:
			state = EXPECTING_DONE;
			break;
		case PACKET_READ_DELIM:
		case PACKET_READ_RESPONSE_END:
			die(_("invalid packet"));
		}

		switch (state) {
		case EXPECTING_FIRST_REF:
			process_capabilities(reader, &len);
			if (process_dummy_ref(reader)) {
				state = EXPECTING_SHALLOW;
				break;
			}
			state = EXPECTING_REF;
			/* fallthrough */
		case EXPECTING_REF:
			if (process_ref(reader, len, &list, flags, extra_have))
				break;
			state = EXPECTING_SHALLOW;
			/* fallthrough */
		case EXPECTING_SHALLOW:
			if (process_shallow(reader, len, shallow_points))
				break;
			die(_("protocol error: unexpected '%s'"), reader->line);
		case EXPECTING_DONE:
			break;
		}
	}

	annotate_refs_with_symref_info(*orig_list);

	return list;
}

/* Returns 1 when a valid ref has been added to `list`, 0 otherwise */
static int process_ref_v2(struct packet_reader *reader, struct ref ***list,
			  const char **unborn_head_target)
{
	int ret = 1;
	int i = 0;
	struct object_id old_oid;
	struct ref *ref;
	struct string_list line_sections = STRING_LIST_INIT_DUP;
	const char *end;
	const char *line = reader->line;

	/*
	 * Ref lines have a number of fields which are space deliminated.  The
	 * first field is the OID of the ref.  The second field is the ref
	 * name.  Subsequent fields (symref-target and peeled) are optional and
	 * don't have a particular order.
	 */
	if (string_list_split(&line_sections, line, ' ', -1) < 2) {
		ret = 0;
		goto out;
	}

	if (!strcmp("unborn", line_sections.items[i].string)) {
		i++;
		if (unborn_head_target &&
		    !strcmp("HEAD", line_sections.items[i++].string)) {
			/*
			 * Look for the symref target (if any). If found,
			 * return it to the caller.
			 */
			for (; i < line_sections.nr; i++) {
				const char *arg = line_sections.items[i].string;

				if (skip_prefix(arg, "symref-target:", &arg)) {
					*unborn_head_target = xstrdup(arg);
					break;
				}
			}
		}
		goto out;
	}
	if (parse_oid_hex_algop(line_sections.items[i++].string, &old_oid, &end, reader->hash_algo) ||
	    *end) {
		ret = 0;
		goto out;
	}

	ref = alloc_ref(line_sections.items[i++].string);

	memcpy(ref->old_oid.hash, old_oid.hash, reader->hash_algo->rawsz);
	**list = ref;
	*list = &ref->next;

	for (; i < line_sections.nr; i++) {
		const char *arg = line_sections.items[i].string;
		if (skip_prefix(arg, "symref-target:", &arg))
			ref->symref = xstrdup(arg);

		if (skip_prefix(arg, "peeled:", &arg)) {
			struct object_id peeled_oid;
			char *peeled_name;
			struct ref *peeled;
			if (parse_oid_hex_algop(arg, &peeled_oid, &end,
						reader->hash_algo) || *end) {
				ret = 0;
				goto out;
			}

			peeled_name = xstrfmt("%s^{}", ref->name);
			peeled = alloc_ref(peeled_name);

			memcpy(peeled->old_oid.hash, peeled_oid.hash,
			       reader->hash_algo->rawsz);
			**list = peeled;
			*list = &peeled->next;

			free(peeled_name);
		}
	}

out:
	string_list_clear(&line_sections, 0);
	return ret;
}

void check_stateless_delimiter(int stateless_rpc,
			      struct packet_reader *reader,
			      const char *error)
{
	if (!stateless_rpc)
		return; /* not in stateless mode, no delimiter expected */
	if (packet_reader_read(reader) != PACKET_READ_RESPONSE_END)
		die("%s", error);
}

static void send_capabilities(int fd_out, struct packet_reader *reader)
{
	const char *hash_name;

	if (server_supports_v2("agent", 0))
		packet_write_fmt(fd_out, "agent=%s", git_user_agent_sanitized());

	if (server_feature_v2("object-format", &hash_name)) {
		int hash_algo = hash_algo_by_name(hash_name);
		if (hash_algo == GIT_HASH_UNKNOWN)
			die(_("unknown object format '%s' specified by server"), hash_name);
		reader->hash_algo = &hash_algos[hash_algo];
		packet_write_fmt(fd_out, "object-format=%s", reader->hash_algo->name);
	} else {
		reader->hash_algo = &hash_algos[GIT_HASH_SHA1];
	}
}

int get_remote_bundle_uri(int fd_out, struct packet_reader *reader,
			  struct bundle_list *bundles, int stateless_rpc)
{
	int line_nr = 1;

	/* Assert bundle-uri support */
	server_supports_v2("bundle-uri", 1);

	/* (Re-)send capabilities */
	send_capabilities(fd_out, reader);

	/* Send command */
	packet_write_fmt(fd_out, "command=bundle-uri\n");
	packet_delim(fd_out);

	/* Send options */
	if (git_env_bool("GIT_TEST_PROTOCOL_BAD_BUNDLE_URI", 0))
		packet_write_fmt(fd_out, "test-bad-client\n");
	packet_flush(fd_out);

	/* Process response from server */
	while (packet_reader_read(reader) == PACKET_READ_NORMAL) {
		const char *line = reader->line;
		line_nr++;

		if (!bundle_uri_parse_line(bundles, line))
			continue;

		return error(_("error on bundle-uri response line %d: %s"),
			     line_nr, line);
	}

	if (reader->status != PACKET_READ_FLUSH)
		return error(_("expected flush after bundle-uri listing"));

	/*
	 * Might die(), but obscure enough that that's OK, e.g. in
	 * serve.c we'll call BUG() on its equivalent (the
	 * PACKET_READ_RESPONSE_END check).
	 */
	check_stateless_delimiter(stateless_rpc, reader,
				  _("expected response end packet after ref listing"));

	return 0;
}

struct ref **get_remote_refs(int fd_out, struct packet_reader *reader,
			     struct ref **list, int for_push,
			     struct transport_ls_refs_options *transport_options,
			     const struct string_list *server_options,
			     int stateless_rpc)
{
	int i;
	struct strvec *ref_prefixes = transport_options ?
		&transport_options->ref_prefixes : NULL;
	const char **unborn_head_target = transport_options ?
		&transport_options->unborn_head_target : NULL;
	*list = NULL;

	if (server_supports_v2("ls-refs", 1))
		packet_write_fmt(fd_out, "command=ls-refs\n");

	/* Send capabilities */
	send_capabilities(fd_out, reader);

	if (server_options && server_options->nr &&
	    server_supports_v2("server-option", 1))
		for (i = 0; i < server_options->nr; i++)
			packet_write_fmt(fd_out, "server-option=%s",
					 server_options->items[i].string);

	packet_delim(fd_out);
	/* When pushing we don't want to request the peeled tags */
	if (!for_push)
		packet_write_fmt(fd_out, "peel\n");
	packet_write_fmt(fd_out, "symrefs\n");
	if (server_supports_feature("ls-refs", "unborn", 0))
		packet_write_fmt(fd_out, "unborn\n");
	for (i = 0; ref_prefixes && i < ref_prefixes->nr; i++) {
		packet_write_fmt(fd_out, "ref-prefix %s\n",
				 ref_prefixes->v[i]);
	}
	packet_flush(fd_out);

	/* Process response from server */
	while (packet_reader_read(reader) == PACKET_READ_NORMAL) {
		if (!process_ref_v2(reader, &list, unborn_head_target))
			die(_("invalid ls-refs response: %s"), reader->line);
	}

	if (reader->status != PACKET_READ_FLUSH)
		die(_("expected flush after ref listing"));

	check_stateless_delimiter(stateless_rpc, reader,
				  _("expected response end packet after ref listing"));

	return list;
}

const char *parse_feature_value(const char *feature_list, const char *feature, int *lenp, int *offset)
{
	int len;

	if (!feature_list)
		return NULL;

	len = strlen(feature);
	if (offset)
		feature_list += *offset;
	while (*feature_list) {
		const char *found = strstr(feature_list, feature);
		if (!found)
			return NULL;
		if (feature_list == found || isspace(found[-1])) {
			const char *value = found + len;
			/* feature with no value (e.g., "thin-pack") */
			if (!*value || isspace(*value)) {
				if (lenp)
					*lenp = 0;
				if (offset)
					*offset = found + len - feature_list;
				return value;
			}
			/* feature with a value (e.g., "agent=git/1.2.3") */
			else if (*value == '=') {
				int end;

				value++;
				end = strcspn(value, " \t\n");
				if (lenp)
					*lenp = end;
				if (offset)
					*offset = value + end - feature_list;
				return value;
			}
			/*
			 * otherwise we matched a substring of another feature;
			 * keep looking
			 */
		}
		feature_list = found + 1;
	}
	return NULL;
}

int server_supports_hash(const char *desired, int *feature_supported)
{
	int offset = 0;
	int len;
	const char *hash;

	hash = next_server_feature_value("object-format", &len, &offset);
	if (feature_supported)
		*feature_supported = !!hash;
	if (!hash) {
		hash = hash_algos[GIT_HASH_SHA1].name;
		len = strlen(hash);
	}
	while (hash) {
		if (!xstrncmpz(desired, hash, len))
			return 1;

		hash = next_server_feature_value("object-format", &len, &offset);
	}
	return 0;
}

int parse_feature_request(const char *feature_list, const char *feature)
{
	return !!parse_feature_value(feature_list, feature, NULL, NULL);
}

static const char *next_server_feature_value(const char *feature, int *len, int *offset)
{
	return parse_feature_value(server_capabilities_v1, feature, len, offset);
}

const char *server_feature_value(const char *feature, int *len)
{
	return parse_feature_value(server_capabilities_v1, feature, len, NULL);
}

int server_supports(const char *feature)
{
	return !!server_feature_value(feature, NULL);
}

enum protocol {
	PROTO_LOCAL = 1,
	PROTO_FILE,
	PROTO_SSH,
	PROTO_GIT
};

int url_is_local_not_ssh(const char *url)
{
	const char *colon = strchr(url, ':');
	const char *slash = strchr(url, '/');
	return !colon || (slash && slash < colon) ||
		(has_dos_drive_prefix(url) && is_valid_path(url));
}

static const char *prot_name(enum protocol protocol)
{
	switch (protocol) {
		case PROTO_LOCAL:
		case PROTO_FILE:
			return "file";
		case PROTO_SSH:
			return "ssh";
		case PROTO_GIT:
			return "git";
		default:
			return "unknown protocol";
	}
}

static enum protocol get_protocol(const char *name)
{
	if (!strcmp(name, "ssh"))
		return PROTO_SSH;
	if (!strcmp(name, "git"))
		return PROTO_GIT;
	if (!strcmp(name, "git+ssh")) /* deprecated - do not use */
		return PROTO_SSH;
	if (!strcmp(name, "ssh+git")) /* deprecated - do not use */
		return PROTO_SSH;
	if (!strcmp(name, "file"))
		return PROTO_FILE;
	die(_("protocol '%s' is not supported"), name);
}

static char *host_end(char **hoststart, int removebrackets)
{
	char *host = *hoststart;
	char *end;
	char *start = strstr(host, "@[");
	if (start)
		start++; /* Jump over '@' */
	else
		start = host;
	if (start[0] == '[') {
		end = strchr(start + 1, ']');
		if (end) {
			if (removebrackets) {
				*end = 0;
				memmove(start, start + 1, end - start);
				end++;
			}
		} else
			end = host;
	} else
		end = host;
	return end;
}

#define STR_(s)	# s
#define STR(s)	STR_(s)

static void get_host_and_port(char **host, const char **port)
{
	char *colon, *end;
	end = host_end(host, 1);
	colon = strchr(end, ':');
	if (colon) {
		long portnr = strtol(colon + 1, &end, 10);
		if (end != colon + 1 && *end == '\0' && 0 <= portnr && portnr < 65536) {
			*colon = 0;
			*port = colon + 1;
		} else if (!colon[1]) {
			*colon = 0;
		}
	}
}

static void enable_keepalive(int sockfd)
{
	int ka = 1;

	if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka)) < 0)
		error_errno(_("unable to set SO_KEEPALIVE on socket"));
}

#ifndef NO_IPV6

static const char *ai_name(const struct addrinfo *ai)
{
	static char addr[NI_MAXHOST];
	if (getnameinfo(ai->ai_addr, ai->ai_addrlen, addr, sizeof(addr), NULL, 0,
			NI_NUMERICHOST) != 0)
		xsnprintf(addr, sizeof(addr), "(unknown)");

	return addr;
}

/*
 * Returns a connected socket() fd, or else die()s.
 */
static int git_tcp_connect_sock(char *host, int flags)
{
	struct strbuf error_message = STRBUF_INIT;
	int sockfd = -1;
	const char *port = STR(DEFAULT_GIT_PORT);
	struct addrinfo hints, *ai0, *ai;
	int gai;
	int cnt = 0;

	get_host_and_port(&host, &port);
	if (!*port)
		port = "<none>";

	memset(&hints, 0, sizeof(hints));
	if (flags & CONNECT_IPV4)
		hints.ai_family = AF_INET;
	else if (flags & CONNECT_IPV6)
		hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, _("Looking up %s ... "), host);

	gai = getaddrinfo(host, port, &hints, &ai);
	if (gai)
		die(_("unable to look up %s (port %s) (%s)"), host, port, gai_strerror(gai));

	if (flags & CONNECT_VERBOSE)
		/* TRANSLATORS: this is the end of "Looking up %s ... " */
		fprintf(stderr, _("done.\nConnecting to %s (port %s) ... "), host, port);

	for (ai0 = ai; ai; ai = ai->ai_next, cnt++) {
		sockfd = socket(ai->ai_family,
				ai->ai_socktype, ai->ai_protocol);
		if ((sockfd < 0) ||
		    (connect(sockfd, ai->ai_addr, ai->ai_addrlen) < 0)) {
			strbuf_addf(&error_message, "%s[%d: %s]: errno=%s\n",
				    host, cnt, ai_name(ai), strerror(errno));
			if (0 <= sockfd)
				close(sockfd);
			sockfd = -1;
			continue;
		}
		if (flags & CONNECT_VERBOSE)
			fprintf(stderr, "%s ", ai_name(ai));
		break;
	}

	freeaddrinfo(ai0);

	if (sockfd < 0)
		die(_("unable to connect to %s:\n%s"), host, error_message.buf);

	enable_keepalive(sockfd);

	if (flags & CONNECT_VERBOSE)
		/* TRANSLATORS: this is the end of "Connecting to %s (port %s) ... " */
		fprintf_ln(stderr, _("done."));

	strbuf_release(&error_message);

	return sockfd;
}

#else /* NO_IPV6 */

/*
 * Returns a connected socket() fd, or else die()s.
 */
static int git_tcp_connect_sock(char *host, int flags)
{
	struct strbuf error_message = STRBUF_INIT;
	int sockfd = -1;
	const char *port = STR(DEFAULT_GIT_PORT);
	char *ep;
	struct hostent *he;
	struct sockaddr_in sa;
	char **ap;
	unsigned int nport;
	int cnt;

	get_host_and_port(&host, &port);

	if (flags & CONNECT_VERBOSE)
		fprintf(stderr, _("Looking up %s ... "), host);

	he = gethostbyname(host);
	if (!he)
		die(_("unable to look up %s (%s)"), host, hstrerror(h_errno));
	nport = strtoul(port, &ep, 10);
	if ( ep == port || *ep ) {
		/* Not numeric */
		struct servent *se = getservbyname(port,"tcp");
		if ( !se )
			die(_("unknown port %s"), port);
		nport = se->s_port;
	}

	if (flags & CONNECT_VERBOSE)
		/* TRANSLATORS: this is the end of "Looking up %s ... " */
		fprintf(stderr, _("done.\nConnecting to %s (port %s) ... "), host, port);

	for (cnt = 0, ap = he->h_addr_list; *ap; ap++, cnt++) {
		memset(&sa, 0, sizeof sa);
		sa.sin_family = he->h_addrtype;
		sa.sin_port = htons(nport);
		memcpy(&sa.sin_addr, *ap, he->h_length);

		sockfd = socket(he->h_addrtype, SOCK_STREAM, 0);
		if ((sockfd < 0) ||
		    connect(sockfd, (struct sockaddr *)&sa, sizeof sa) < 0) {
			strbuf_addf(&error_message, "%s[%d: %s]: errno=%s\n",
				host,
				cnt,
				inet_ntoa(*(struct in_addr *)&sa.sin_addr),
				strerror(errno));
			if (0 <= sockfd)
				close(sockfd);
			sockfd = -1;
			continue;
		}
		if (flags & CONNECT_VERBOSE)
			fprintf(stderr, "%s ",
				inet_ntoa(*(struct in_addr *)&sa.sin_addr));
		break;
	}

	if (sockfd < 0)
		die(_("unable to connect to %s:\n%s"), host, error_message.buf);

	enable_keepalive(sockfd);

	if (flags & CONNECT_VERBOSE)
		/* TRANSLATORS: this is the end of "Connecting to %s (port %s) ... " */
		fprintf_ln(stderr, _("done."));

	return sockfd;
}

#endif /* NO_IPV6 */


/*
 * Dummy child_process returned by git_connect() if the transport protocol
 * does not need fork(2).
 */
static struct child_process no_fork = CHILD_PROCESS_INIT;

int git_connection_is_socket(struct child_process *conn)
{
	return conn == &no_fork;
}

static struct child_process *git_tcp_connect(int fd[2], char *host, int flags)
{
	int sockfd = git_tcp_connect_sock(host, flags);

	fd[0] = sockfd;
	fd[1] = dup(sockfd);

	return &no_fork;
}


static char *git_proxy_command;

static int git_proxy_command_options(const char *var, const char *value,
		void *cb)
{
	if (!strcmp(var, "core.gitproxy")) {
		const char *for_pos;
		int matchlen = -1;
		int hostlen;
		const char *rhost_name = cb;
		int rhost_len = strlen(rhost_name);

		if (git_proxy_command)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		/* [core]
		 * ;# matches www.kernel.org as well
		 * gitproxy = netcatter-1 for kernel.org
		 * gitproxy = netcatter-2 for sample.xz
		 * gitproxy = netcatter-default
		 */
		for_pos = strstr(value, " for ");
		if (!for_pos)
			/* matches everybody */
			matchlen = strlen(value);
		else {
			hostlen = strlen(for_pos + 5);
			if (rhost_len < hostlen)
				matchlen = -1;
			else if (!strncmp(for_pos + 5,
					  rhost_name + rhost_len - hostlen,
					  hostlen) &&
				 ((rhost_len == hostlen) ||
				  rhost_name[rhost_len - hostlen -1] == '.'))
				matchlen = for_pos - value;
			else
				matchlen = -1;
		}
		if (0 <= matchlen) {
			/* core.gitproxy = none for kernel.org */
			if (matchlen == 4 &&
			    !memcmp(value, "none", 4))
				matchlen = 0;
			git_proxy_command = xmemdupz(value, matchlen);
		}
		return 0;
	}

	return git_default_config(var, value, cb);
}

static int git_use_proxy(const char *host)
{
	git_proxy_command = getenv("GIT_PROXY_COMMAND");
	git_config(git_proxy_command_options, (void*)host);
	return (git_proxy_command && *git_proxy_command);
}

static struct child_process *git_proxy_connect(int fd[2], char *host)
{
	const char *port = STR(DEFAULT_GIT_PORT);
	struct child_process *proxy;

	get_host_and_port(&host, &port);

	if (looks_like_command_line_option(host))
		die(_("strange hostname '%s' blocked"), host);
	if (looks_like_command_line_option(port))
		die(_("strange port '%s' blocked"), port);

	proxy = xmalloc(sizeof(*proxy));
	child_process_init(proxy);
	strvec_push(&proxy->args, git_proxy_command);
	strvec_push(&proxy->args, host);
	strvec_push(&proxy->args, port);
	proxy->in = -1;
	proxy->out = -1;
	if (start_command(proxy))
		die(_("cannot start proxy %s"), git_proxy_command);
	fd[0] = proxy->out; /* read from proxy stdout */
	fd[1] = proxy->in;  /* write to proxy stdin */
	return proxy;
}

static char *get_port(char *host)
{
	char *end;
	char *p = strchr(host, ':');

	if (p) {
		long port = strtol(p + 1, &end, 10);
		if (end != p + 1 && *end == '\0' && 0 <= port && port < 65536) {
			*p = '\0';
			return p+1;
		}
	}

	return NULL;
}

/*
 * Extract protocol and relevant parts from the specified connection URL.
 * The caller must free() the returned strings.
 */
static enum protocol parse_connect_url(const char *url_orig, char **ret_host,
				       char **ret_path)
{
	char *url;
	char *host, *path;
	char *end;
	int separator = '/';
	enum protocol protocol = PROTO_LOCAL;

	if (is_url(url_orig))
		url = url_decode(url_orig);
	else
		url = xstrdup(url_orig);

	host = strstr(url, "://");
	if (host) {
		*host = '\0';
		protocol = get_protocol(url);
		host += 3;
	} else {
		host = url;
		if (!url_is_local_not_ssh(url)) {
			protocol = PROTO_SSH;
			separator = ':';
		}
	}

	/*
	 * Don't do destructive transforms as protocol code does
	 * '[]' unwrapping in get_host_and_port()
	 */
	end = host_end(&host, 0);

	if (protocol == PROTO_LOCAL)
		path = end;
	else if (protocol == PROTO_FILE && *host != '/' &&
		 !has_dos_drive_prefix(host) &&
		 offset_1st_component(host - 2) > 1)
		path = host - 2; /* include the leading "//" */
	else if (protocol == PROTO_FILE && has_dos_drive_prefix(end))
		path = end; /* "file://$(pwd)" may be "file://C:/projects/repo" */
	else
		path = strchr(end, separator);

	if (!path || !*path)
		die(_("no path specified; see 'git help pull' for valid url syntax"));

	/*
	 * null-terminate hostname and point path to ~ for URL's like this:
	 *    ssh://host.xz/~user/repo
	 */

	end = path; /* Need to \0 terminate host here */
	if (separator == ':')
		path++; /* path starts after ':' */
	if (protocol == PROTO_GIT || protocol == PROTO_SSH) {
		if (path[1] == '~')
			path++;
	}

	path = xstrdup(path);
	*end = '\0';

	*ret_host = xstrdup(host);
	*ret_path = path;
	free(url);
	return protocol;
}

static const char *get_ssh_command(void)
{
	const char *ssh;

	if ((ssh = getenv("GIT_SSH_COMMAND")))
		return ssh;

	if (!git_config_get_string_tmp("core.sshcommand", &ssh))
		return ssh;

	return NULL;
}

enum ssh_variant {
	VARIANT_AUTO,
	VARIANT_SIMPLE,
	VARIANT_SSH,
	VARIANT_PLINK,
	VARIANT_PUTTY,
	VARIANT_TORTOISEPLINK,
};

static void override_ssh_variant(enum ssh_variant *ssh_variant)
{
	const char *variant = getenv("GIT_SSH_VARIANT");

	if (!variant && git_config_get_string_tmp("ssh.variant", &variant))
		return;

	if (!strcmp(variant, "auto"))
		*ssh_variant = VARIANT_AUTO;
	else if (!strcmp(variant, "plink"))
		*ssh_variant = VARIANT_PLINK;
	else if (!strcmp(variant, "putty"))
		*ssh_variant = VARIANT_PUTTY;
	else if (!strcmp(variant, "tortoiseplink"))
		*ssh_variant = VARIANT_TORTOISEPLINK;
	else if (!strcmp(variant, "simple"))
		*ssh_variant = VARIANT_SIMPLE;
	else
		*ssh_variant = VARIANT_SSH;
}

static enum ssh_variant determine_ssh_variant(const char *ssh_command,
					      int is_cmdline)
{
	enum ssh_variant ssh_variant = VARIANT_AUTO;
	const char *variant;
	char *p = NULL;

	override_ssh_variant(&ssh_variant);

	if (ssh_variant != VARIANT_AUTO)
		return ssh_variant;

	if (!is_cmdline) {
		p = xstrdup(ssh_command);
		variant = basename(p);
	} else {
		const char **ssh_argv;

		p = xstrdup(ssh_command);
		if (split_cmdline(p, &ssh_argv) > 0) {
			variant = basename((char *)ssh_argv[0]);
			/*
			 * At this point, variant points into the buffer
			 * referenced by p, hence we do not need ssh_argv
			 * any longer.
			 */
			free(ssh_argv);
		} else {
			free(p);
			return ssh_variant;
		}
	}

	if (!strcasecmp(variant, "ssh") ||
	    !strcasecmp(variant, "ssh.exe"))
		ssh_variant = VARIANT_SSH;
	else if (!strcasecmp(variant, "plink") ||
		 !strcasecmp(variant, "plink.exe"))
		ssh_variant = VARIANT_PLINK;
	else if (!strcasecmp(variant, "tortoiseplink") ||
		 !strcasecmp(variant, "tortoiseplink.exe"))
		ssh_variant = VARIANT_TORTOISEPLINK;

	free(p);
	return ssh_variant;
}

/*
 * Open a connection using Git's native protocol.
 *
 * The caller is responsible for freeing hostandport, but this function may
 * modify it (for example, to truncate it to remove the port part).
 */
static struct child_process *git_connect_git(int fd[2], char *hostandport,
					     const char *path, const char *prog,
					     enum protocol_version version,
					     int flags)
{
	struct child_process *conn;
	struct strbuf request = STRBUF_INIT;
	/*
	 * Set up virtual host information based on where we will
	 * connect, unless the user has overridden us in
	 * the environment.
	 */
	char *target_host = getenv("GIT_OVERRIDE_VIRTUAL_HOST");
	if (target_host)
		target_host = xstrdup(target_host);
	else
		target_host = xstrdup(hostandport);

	transport_check_allowed("git");
	if (strchr(target_host, '\n') || strchr(path, '\n'))
		die(_("newline is forbidden in git:// hosts and repo paths"));

	/*
	 * These underlying connection commands die() if they
	 * cannot connect.
	 */
	if (git_use_proxy(hostandport))
		conn = git_proxy_connect(fd, hostandport);
	else
		conn = git_tcp_connect(fd, hostandport, flags);
	/*
	 * Separate original protocol components prog and path
	 * from extended host header with a NUL byte.
	 *
	 * Note: Do not add any other headers here!  Doing so
	 * will cause older git-daemon servers to crash.
	 */
	strbuf_addf(&request,
		    "%s %s%chost=%s%c",
		    prog, path, 0,
		    target_host, 0);

	/* If using a new version put that stuff here after a second null byte */
	if (version > 0) {
		strbuf_addch(&request, '\0');
		strbuf_addf(&request, "version=%d%c",
			    version, '\0');
	}

	packet_write(fd[1], request.buf, request.len);

	free(target_host);
	strbuf_release(&request);
	return conn;
}

/*
 * Append the appropriate environment variables to `env` and options to
 * `args` for running ssh in Git's SSH-tunneled transport.
 */
static void push_ssh_options(struct strvec *args, struct strvec *env,
			     enum ssh_variant variant, const char *port,
			     enum protocol_version version, int flags)
{
	if (variant == VARIANT_SSH &&
	    version > 0) {
		strvec_push(args, "-o");
		strvec_push(args, "SendEnv=" GIT_PROTOCOL_ENVIRONMENT);
		strvec_pushf(env, GIT_PROTOCOL_ENVIRONMENT "=version=%d",
			     version);
	}

	if (flags & CONNECT_IPV4) {
		switch (variant) {
		case VARIANT_AUTO:
			BUG("VARIANT_AUTO passed to push_ssh_options");
		case VARIANT_SIMPLE:
			die(_("ssh variant 'simple' does not support -4"));
		case VARIANT_SSH:
		case VARIANT_PLINK:
		case VARIANT_PUTTY:
		case VARIANT_TORTOISEPLINK:
			strvec_push(args, "-4");
		}
	} else if (flags & CONNECT_IPV6) {
		switch (variant) {
		case VARIANT_AUTO:
			BUG("VARIANT_AUTO passed to push_ssh_options");
		case VARIANT_SIMPLE:
			die(_("ssh variant 'simple' does not support -6"));
		case VARIANT_SSH:
		case VARIANT_PLINK:
		case VARIANT_PUTTY:
		case VARIANT_TORTOISEPLINK:
			strvec_push(args, "-6");
		}
	}

	if (variant == VARIANT_TORTOISEPLINK)
		strvec_push(args, "-batch");

	if (port) {
		switch (variant) {
		case VARIANT_AUTO:
			BUG("VARIANT_AUTO passed to push_ssh_options");
		case VARIANT_SIMPLE:
			die(_("ssh variant 'simple' does not support setting port"));
		case VARIANT_SSH:
			strvec_push(args, "-p");
			break;
		case VARIANT_PLINK:
		case VARIANT_PUTTY:
		case VARIANT_TORTOISEPLINK:
			strvec_push(args, "-P");
		}

		strvec_push(args, port);
	}
}

/* Prepare a child_process for use by Git's SSH-tunneled transport. */
static void fill_ssh_args(struct child_process *conn, const char *ssh_host,
			  const char *port, enum protocol_version version,
			  int flags)
{
	const char *ssh;
	enum ssh_variant variant;

	if (looks_like_command_line_option(ssh_host))
		die(_("strange hostname '%s' blocked"), ssh_host);

	ssh = get_ssh_command();
	if (ssh) {
		variant = determine_ssh_variant(ssh, 1);
	} else {
		/*
		 * GIT_SSH is the no-shell version of
		 * GIT_SSH_COMMAND (and must remain so for
		 * historical compatibility).
		 */
		conn->use_shell = 0;

		ssh = getenv("GIT_SSH");
		if (!ssh)
			ssh = "ssh";
		variant = determine_ssh_variant(ssh, 0);
	}

	if (variant == VARIANT_AUTO) {
		struct child_process detect = CHILD_PROCESS_INIT;

		detect.use_shell = conn->use_shell;
		detect.no_stdin = detect.no_stdout = detect.no_stderr = 1;

		strvec_push(&detect.args, ssh);
		strvec_push(&detect.args, "-G");
		push_ssh_options(&detect.args, &detect.env,
				 VARIANT_SSH, port, version, flags);
		strvec_push(&detect.args, ssh_host);

		variant = run_command(&detect) ? VARIANT_SIMPLE : VARIANT_SSH;
	}

	strvec_push(&conn->args, ssh);
	push_ssh_options(&conn->args, &conn->env, variant, port, version,
			 flags);
	strvec_push(&conn->args, ssh_host);
}

/*
 * This returns the dummy child_process `no_fork` if the transport protocol
 * does not need fork(2), or a struct child_process object if it does.  Once
 * done, finish the connection with finish_connect() with the value returned
 * from this function (it is safe to call finish_connect() with NULL to
 * support the former case).
 *
 * If it returns, the connect is successful; it just dies on errors (this
 * will hopefully be changed in a libification effort, to return NULL when
 * the connection failed).
 */
struct child_process *git_connect(int fd[2], const char *url,
				  const char *prog, int flags)
{
	char *hostandport, *path;
	struct child_process *conn;
	enum protocol protocol;
	enum protocol_version version = get_protocol_version_config();

	/*
	 * NEEDSWORK: If we are trying to use protocol v2 and we are planning
	 * to perform a push, then fallback to v0 since the client doesn't know
	 * how to push yet using v2.
	 */
	if (version == protocol_v2 && !strcmp("git-receive-pack", prog))
		version = protocol_v0;

	/* Without this we cannot rely on waitpid() to tell
	 * what happened to our children.
	 */
	signal(SIGCHLD, SIG_DFL);

	protocol = parse_connect_url(url, &hostandport, &path);
	if ((flags & CONNECT_DIAG_URL) && (protocol != PROTO_SSH)) {
		printf("Diag: url=%s\n", url ? url : "NULL");
		printf("Diag: protocol=%s\n", prot_name(protocol));
		printf("Diag: hostandport=%s\n", hostandport ? hostandport : "NULL");
		printf("Diag: path=%s\n", path ? path : "NULL");
		conn = NULL;
	} else if (protocol == PROTO_GIT) {
		conn = git_connect_git(fd, hostandport, path, prog, version, flags);
		conn->trace2_child_class = "transport/git";
	} else {
		struct strbuf cmd = STRBUF_INIT;
		const char *const *var;

		conn = xmalloc(sizeof(*conn));
		child_process_init(conn);

		if (looks_like_command_line_option(path))
			die(_("strange pathname '%s' blocked"), path);

		strbuf_addstr(&cmd, prog);
		strbuf_addch(&cmd, ' ');
		sq_quote_buf(&cmd, path);

		/* remove repo-local variables from the environment */
		for (var = local_repo_env; *var; var++)
			strvec_push(&conn->env, *var);

		conn->use_shell = 1;
		conn->in = conn->out = -1;
		if (protocol == PROTO_SSH) {
			char *ssh_host = hostandport;
			const char *port = NULL;
			transport_check_allowed("ssh");
			get_host_and_port(&ssh_host, &port);

			if (!port)
				port = get_port(ssh_host);

			if (flags & CONNECT_DIAG_URL) {
				printf("Diag: url=%s\n", url ? url : "NULL");
				printf("Diag: protocol=%s\n", prot_name(protocol));
				printf("Diag: userandhost=%s\n", ssh_host ? ssh_host : "NULL");
				printf("Diag: port=%s\n", port ? port : "NONE");
				printf("Diag: path=%s\n", path ? path : "NULL");

				free(hostandport);
				free(path);
				free(conn);
				strbuf_release(&cmd);
				return NULL;
			}
			conn->trace2_child_class = "transport/ssh";
			fill_ssh_args(conn, ssh_host, port, version, flags);
		} else {
			transport_check_allowed("file");
			conn->trace2_child_class = "transport/file";
			if (version > 0) {
				strvec_pushf(&conn->env,
					     GIT_PROTOCOL_ENVIRONMENT "=version=%d",
					     version);
			}
		}
		strvec_push(&conn->args, cmd.buf);

		if (start_command(conn))
			die(_("unable to fork"));

		fd[0] = conn->out; /* read from child's stdout */
		fd[1] = conn->in;  /* write to child's stdin */
		strbuf_release(&cmd);
	}
	free(hostandport);
	free(path);
	return conn;
}

int finish_connect(struct child_process *conn)
{
	int code;
	if (!conn || git_connection_is_socket(conn))
		return 0;

	code = finish_command(conn);
	free(conn);
	return code;
}
