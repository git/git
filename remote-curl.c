#include "git-compat-util.h"
#include "alloc.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "remote.h"
#include "connect.h"
#include "strbuf.h"
#include "walker.h"
#include "http.h"
#include "exec-cmd.h"
#include "run-command.h"
#include "pkt-line.h"
#include "string-list.h"
#include "sideband.h"
#include "strvec.h"
#include "credential.h"
#include "oid-array.h"
#include "send-pack.h"
#include "setup.h"
#include "protocol.h"
#include "quote.h"
#include "trace2.h"
#include "transport.h"
#include "write-or-die.h"

static struct remote *remote;
/* always ends with a trailing slash */
static struct strbuf url = STRBUF_INIT;

struct options {
	int verbosity;
	unsigned long depth;
	char *deepen_since;
	struct string_list deepen_not;
	struct string_list push_options;
	char *filter;
	unsigned progress : 1,
		check_self_contained_and_connected : 1,
		cloning : 1,
		update_shallow : 1,
		followtags : 1,
		dry_run : 1,
		thin : 1,
		/* One of the SEND_PACK_PUSH_CERT_* constants. */
		push_cert : 2,
		deepen_relative : 1,

		/* see documentation of corresponding flag in fetch-pack.h */
		from_promisor : 1,

		refetch : 1,
		atomic : 1,
		object_format : 1,
		force_if_includes : 1;
	const struct git_hash_algo *hash_algo;
};
static struct options options;
static struct string_list cas_options = STRING_LIST_INIT_DUP;

static int set_option(const char *name, const char *value)
{
	if (!strcmp(name, "verbosity")) {
		char *end;
		int v = strtol(value, &end, 10);
		if (value == end || *end)
			return -1;
		options.verbosity = v;
		return 0;
	}
	else if (!strcmp(name, "progress")) {
		if (!strcmp(value, "true"))
			options.progress = 1;
		else if (!strcmp(value, "false"))
			options.progress = 0;
		else
			return -1;
		return 0;
	}
	else if (!strcmp(name, "depth")) {
		char *end;
		unsigned long v = strtoul(value, &end, 10);
		if (value == end || *end)
			return -1;
		options.depth = v;
		return 0;
	}
	else if (!strcmp(name, "deepen-since")) {
		options.deepen_since = xstrdup(value);
		return 0;
	}
	else if (!strcmp(name, "deepen-not")) {
		string_list_append(&options.deepen_not, value);
		return 0;
	}
	else if (!strcmp(name, "deepen-relative")) {
		if (!strcmp(value, "true"))
			options.deepen_relative = 1;
		else if (!strcmp(value, "false"))
			options.deepen_relative = 0;
		else
			return -1;
		return 0;
	}
	else if (!strcmp(name, "followtags")) {
		if (!strcmp(value, "true"))
			options.followtags = 1;
		else if (!strcmp(value, "false"))
			options.followtags = 0;
		else
			return -1;
		return 0;
	}
	else if (!strcmp(name, "dry-run")) {
		if (!strcmp(value, "true"))
			options.dry_run = 1;
		else if (!strcmp(value, "false"))
			options.dry_run = 0;
		else
			return -1;
		return 0;
	}
	else if (!strcmp(name, "check-connectivity")) {
		if (!strcmp(value, "true"))
			options.check_self_contained_and_connected = 1;
		else if (!strcmp(value, "false"))
			options.check_self_contained_and_connected = 0;
		else
			return -1;
		return 0;
	}
	else if (!strcmp(name, "cas")) {
		struct strbuf val = STRBUF_INIT;
		strbuf_addstr(&val, "--force-with-lease=");
		if (*value != '"')
			strbuf_addstr(&val, value);
		else if (unquote_c_style(&val, value, NULL))
			return -1;
		string_list_append(&cas_options, val.buf);
		strbuf_release(&val);
		return 0;
	} else if (!strcmp(name, TRANS_OPT_FORCE_IF_INCLUDES)) {
		if (!strcmp(value, "true"))
			options.force_if_includes = 1;
		else if (!strcmp(value, "false"))
			options.force_if_includes = 0;
		else
			return -1;
		return 0;
	} else if (!strcmp(name, "cloning")) {
		if (!strcmp(value, "true"))
			options.cloning = 1;
		else if (!strcmp(value, "false"))
			options.cloning = 0;
		else
			return -1;
		return 0;
	} else if (!strcmp(name, "update-shallow")) {
		if (!strcmp(value, "true"))
			options.update_shallow = 1;
		else if (!strcmp(value, "false"))
			options.update_shallow = 0;
		else
			return -1;
		return 0;
	} else if (!strcmp(name, "pushcert")) {
		if (!strcmp(value, "true"))
			options.push_cert = SEND_PACK_PUSH_CERT_ALWAYS;
		else if (!strcmp(value, "false"))
			options.push_cert = SEND_PACK_PUSH_CERT_NEVER;
		else if (!strcmp(value, "if-asked"))
			options.push_cert = SEND_PACK_PUSH_CERT_IF_ASKED;
		else
			return -1;
		return 0;
	} else if (!strcmp(name, "atomic")) {
		if (!strcmp(value, "true"))
			options.atomic = 1;
		else if (!strcmp(value, "false"))
			options.atomic = 0;
		else
			return -1;
		return 0;
	} else if (!strcmp(name, "push-option")) {
		if (*value != '"')
			string_list_append(&options.push_options, value);
		else {
			struct strbuf unquoted = STRBUF_INIT;
			if (unquote_c_style(&unquoted, value, NULL) < 0)
				die(_("invalid quoting in push-option value: '%s'"), value);
			string_list_append_nodup(&options.push_options,
						 strbuf_detach(&unquoted, NULL));
		}
		return 0;
	} else if (!strcmp(name, "family")) {
		if (!strcmp(value, "ipv4"))
			git_curl_ipresolve = CURL_IPRESOLVE_V4;
		else if (!strcmp(value, "ipv6"))
			git_curl_ipresolve = CURL_IPRESOLVE_V6;
		else if (!strcmp(value, "all"))
			git_curl_ipresolve = CURL_IPRESOLVE_WHATEVER;
		else
			return -1;
		return 0;
	} else if (!strcmp(name, "from-promisor")) {
		options.from_promisor = 1;
		return 0;
	} else if (!strcmp(name, "refetch")) {
		options.refetch = 1;
		return 0;
	} else if (!strcmp(name, "filter")) {
		options.filter = xstrdup(value);
		return 0;
	} else if (!strcmp(name, "object-format")) {
		int algo;
		options.object_format = 1;
		if (strcmp(value, "true")) {
			algo = hash_algo_by_name(value);
			if (algo == GIT_HASH_UNKNOWN)
				die("unknown object format '%s'", value);
			options.hash_algo = &hash_algos[algo];
		}
		return 0;
	} else {
		return 1 /* unsupported */;
	}
}

struct discovery {
	char *service;
	char *buf_alloc;
	char *buf;
	size_t len;
	struct ref *refs;
	struct oid_array shallow;
	enum protocol_version version;
	unsigned proto_git : 1;
};
static struct discovery *last_discovery;

static struct ref *parse_git_refs(struct discovery *heads, int for_push)
{
	struct ref *list = NULL;
	struct packet_reader reader;

	packet_reader_init(&reader, -1, heads->buf, heads->len,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_GENTLE_ON_EOF |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	heads->version = discover_version(&reader);
	switch (heads->version) {
	case protocol_v2:
		/*
		 * Do nothing.  This isn't a list of refs but rather a
		 * capability advertisement.  Client would have run
		 * 'stateless-connect' so we'll dump this capability listing
		 * and let them request the refs themselves.
		 */
		break;
	case protocol_v1:
	case protocol_v0:
		get_remote_heads(&reader, &list, for_push ? REF_NORMAL : 0,
				 NULL, &heads->shallow);
		options.hash_algo = reader.hash_algo;
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	return list;
}

static const struct git_hash_algo *detect_hash_algo(struct discovery *heads)
{
	const char *p = memchr(heads->buf, '\t', heads->len);
	int algo;
	if (!p)
		return the_hash_algo;

	algo = hash_algo_by_length((p - heads->buf) / 2);
	if (algo == GIT_HASH_UNKNOWN)
		return NULL;
	return &hash_algos[algo];
}

static struct ref *parse_info_refs(struct discovery *heads)
{
	char *data, *start, *mid;
	char *ref_name;
	int i = 0;

	struct ref *refs = NULL;
	struct ref *ref = NULL;
	struct ref *last_ref = NULL;

	options.hash_algo = detect_hash_algo(heads);
	if (!options.hash_algo)
		die("%sinfo/refs not valid: could not determine hash algorithm; "
		    "is this a git repository?",
		    transport_anonymize_url(url.buf));

	data = heads->buf;
	start = NULL;
	mid = data;
	while (i < heads->len) {
		if (!start) {
			start = &data[i];
		}
		if (data[i] == '\t')
			mid = &data[i];
		if (data[i] == '\n') {
			if (mid - start != options.hash_algo->hexsz)
				die(_("%sinfo/refs not valid: is this a git repository?"),
				    transport_anonymize_url(url.buf));
			data[i] = 0;
			ref_name = mid + 1;
			ref = alloc_ref(ref_name);
			get_oid_hex_algop(start, &ref->old_oid, options.hash_algo);
			if (!refs)
				refs = ref;
			if (last_ref)
				last_ref->next = ref;
			last_ref = ref;
			start = NULL;
		}
		i++;
	}

	ref = alloc_ref("HEAD");
	if (!http_fetch_ref(url.buf, ref) &&
	    !resolve_remote_symref(ref, refs)) {
		ref->next = refs;
		refs = ref;
	} else {
		free(ref);
	}

	return refs;
}

static void free_discovery(struct discovery *d)
{
	if (d) {
		if (d == last_discovery)
			last_discovery = NULL;
		free(d->shallow.oid);
		free(d->buf_alloc);
		free_refs(d->refs);
		free(d->service);
		free(d);
	}
}

static int show_http_message(struct strbuf *type, struct strbuf *charset,
			     struct strbuf *msg)
{
	const char *p, *eol;

	/*
	 * We only show text/plain parts, as other types are likely
	 * to be ugly to look at on the user's terminal.
	 */
	if (strcmp(type->buf, "text/plain"))
		return -1;
	if (charset->len)
		strbuf_reencode(msg, charset->buf, get_log_output_encoding());

	strbuf_trim(msg);
	if (!msg->len)
		return -1;

	p = msg->buf;
	do {
		eol = strchrnul(p, '\n');
		fprintf(stderr, "remote: %.*s\n", (int)(eol - p), p);
		p = eol + 1;
	} while(*eol);
	return 0;
}

static int get_protocol_http_header(enum protocol_version version,
				    struct strbuf *header)
{
	if (version > 0) {
		strbuf_addf(header, GIT_PROTOCOL_HEADER ": version=%d",
			    version);

		return 1;
	}

	return 0;
}

static void check_smart_http(struct discovery *d, const char *service,
			     struct strbuf *type)
{
	const char *p;
	struct packet_reader reader;

	/*
	 * If we don't see x-$service-advertisement, then it's not smart-http.
	 * But once we do, we commit to it and assume any other protocol
	 * violations are hard errors.
	 */
	if (!skip_prefix(type->buf, "application/x-", &p) ||
	    !skip_prefix(p, service, &p) ||
	    strcmp(p, "-advertisement"))
		return;

	packet_reader_init(&reader, -1, d->buf, d->len,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_DIE_ON_ERR_PACKET);
	if (packet_reader_read(&reader) != PACKET_READ_NORMAL)
		die(_("invalid server response; expected service, got flush packet"));

	if (skip_prefix(reader.line, "# service=", &p) && !strcmp(p, service)) {
		/*
		 * The header can include additional metadata lines, up
		 * until a packet flush marker.  Ignore these now, but
		 * in the future we might start to scan them.
		 */
		for (;;) {
			packet_reader_read(&reader);
			if (reader.pktlen <= 0) {
				break;
			}
		}

		/*
		 * v0 smart http; callers expect us to soak up the
		 * service and header packets
		 */
		d->buf = reader.src_buffer;
		d->len = reader.src_len;
		d->proto_git = 1;

	} else if (!strcmp(reader.line, "version 2")) {
		/*
		 * v2 smart http; do not consume version packet, which will
		 * be handled elsewhere.
		 */
		d->proto_git = 1;

	} else {
		die(_("invalid server response; got '%s'"), reader.line);
	}
}

static struct discovery *discover_refs(const char *service, int for_push)
{
	struct strbuf type = STRBUF_INIT;
	struct strbuf charset = STRBUF_INIT;
	struct strbuf buffer = STRBUF_INIT;
	struct strbuf refs_url = STRBUF_INIT;
	struct strbuf effective_url = STRBUF_INIT;
	struct strbuf protocol_header = STRBUF_INIT;
	struct string_list extra_headers = STRING_LIST_INIT_DUP;
	struct discovery *last = last_discovery;
	int http_ret, maybe_smart = 0;
	struct http_get_options http_options;
	enum protocol_version version = get_protocol_version_config();

	if (last && !strcmp(service, last->service))
		return last;
	free_discovery(last);

	strbuf_addf(&refs_url, "%sinfo/refs", url.buf);
	if ((starts_with(url.buf, "http://") || starts_with(url.buf, "https://")) &&
	     git_env_bool("GIT_SMART_HTTP", 1)) {
		maybe_smart = 1;
		if (!strchr(url.buf, '?'))
			strbuf_addch(&refs_url, '?');
		else
			strbuf_addch(&refs_url, '&');
		strbuf_addf(&refs_url, "service=%s", service);
	}

	/*
	 * NEEDSWORK: If we are trying to use protocol v2 and we are planning
	 * to perform any operation that doesn't involve upload-pack (i.e., a
	 * fetch, ls-remote, etc), then fallback to v0 since we don't know how
	 * to do anything else (like push or remote archive) via v2.
	 */
	if (version == protocol_v2 && strcmp("git-upload-pack", service))
		version = protocol_v0;

	/* Add the extra Git-Protocol header */
	if (get_protocol_http_header(version, &protocol_header))
		string_list_append(&extra_headers, protocol_header.buf);

	memset(&http_options, 0, sizeof(http_options));
	http_options.content_type = &type;
	http_options.charset = &charset;
	http_options.effective_url = &effective_url;
	http_options.base_url = &url;
	http_options.extra_headers = &extra_headers;
	http_options.initial_request = 1;
	http_options.no_cache = 1;

	http_ret = http_get_strbuf(refs_url.buf, &buffer, &http_options);
	switch (http_ret) {
	case HTTP_OK:
		break;
	case HTTP_MISSING_TARGET:
		show_http_message(&type, &charset, &buffer);
		die(_("repository '%s' not found"),
		    transport_anonymize_url(url.buf));
	case HTTP_NOAUTH:
		show_http_message(&type, &charset, &buffer);
		die(_("Authentication failed for '%s'"),
		    transport_anonymize_url(url.buf));
	case HTTP_NOMATCHPUBLICKEY:
		show_http_message(&type, &charset, &buffer);
		die(_("unable to access '%s' with http.pinnedPubkey configuration: %s"),
		    transport_anonymize_url(url.buf), curl_errorstr);
	default:
		show_http_message(&type, &charset, &buffer);
		die(_("unable to access '%s': %s"),
		    transport_anonymize_url(url.buf), curl_errorstr);
	}

	if (options.verbosity && !starts_with(refs_url.buf, url.buf)) {
		char *u = transport_anonymize_url(url.buf);
		warning(_("redirecting to %s"), u);
		free(u);
	}

	last= xcalloc(1, sizeof(*last_discovery));
	last->service = xstrdup(service);
	last->buf_alloc = strbuf_detach(&buffer, &last->len);
	last->buf = last->buf_alloc;

	if (maybe_smart)
		check_smart_http(last, service, &type);

	if (last->proto_git)
		last->refs = parse_git_refs(last, for_push);
	else
		last->refs = parse_info_refs(last);

	strbuf_release(&refs_url);
	strbuf_release(&type);
	strbuf_release(&charset);
	strbuf_release(&effective_url);
	strbuf_release(&buffer);
	strbuf_release(&protocol_header);
	string_list_clear(&extra_headers, 0);
	last_discovery = last;
	return last;
}

static struct ref *get_refs(int for_push)
{
	struct discovery *heads;

	if (for_push)
		heads = discover_refs("git-receive-pack", for_push);
	else
		heads = discover_refs("git-upload-pack", for_push);

	return heads->refs;
}

static void output_refs(struct ref *refs)
{
	struct ref *posn;
	if (options.object_format && options.hash_algo) {
		printf(":object-format %s\n", options.hash_algo->name);
		repo_set_hash_algo(the_repository,
				hash_algo_by_ptr(options.hash_algo));
	}
	for (posn = refs; posn; posn = posn->next) {
		if (posn->symref)
			printf("@%s %s\n", posn->symref, posn->name);
		else
			printf("%s %s\n", hash_to_hex_algop(posn->old_oid.hash,
							    options.hash_algo),
					  posn->name);
	}
	printf("\n");
	fflush(stdout);
}

struct rpc_state {
	const char *service_name;
	char *service_url;
	char *hdr_content_type;
	char *hdr_accept;
	char *hdr_accept_language;
	char *protocol_header;
	char *buf;
	size_t alloc;
	size_t len;
	size_t pos;
	int in;
	int out;
	int any_written;
	unsigned gzip_request : 1;
	unsigned initial_buffer : 1;

	/*
	 * Whenever a pkt-line is read into buf, append the 4 characters
	 * denoting its length before appending the payload.
	 */
	unsigned write_line_lengths : 1;

	/*
	 * Used by rpc_out; initialize to 0. This is true if a flush has been
	 * read, but the corresponding line length (if write_line_lengths is
	 * true) and EOF have not been sent to libcurl. Since each flush marks
	 * the end of a request, each flush must be completely sent before any
	 * further reading occurs.
	 */
	unsigned flush_read_but_not_sent : 1;
};

#define RPC_STATE_INIT { 0 }

/*
 * Appends the result of reading from rpc->out to the string represented by
 * rpc->buf and rpc->len if there is enough space. Returns 1 if there was
 * enough space, 0 otherwise.
 *
 * If rpc->write_line_lengths is true, appends the line length as a 4-byte
 * hexadecimal string before appending the result described above.
 *
 * Writes the total number of bytes appended into appended.
 */
static int rpc_read_from_out(struct rpc_state *rpc, int options,
			     size_t *appended,
			     enum packet_read_status *status) {
	size_t left;
	char *buf;
	int pktlen_raw;

	if (rpc->write_line_lengths) {
		left = rpc->alloc - rpc->len - 4;
		buf = rpc->buf + rpc->len + 4;
	} else {
		left = rpc->alloc - rpc->len;
		buf = rpc->buf + rpc->len;
	}

	if (left < LARGE_PACKET_MAX)
		return 0;

	*status = packet_read_with_status(rpc->out, NULL, NULL, buf,
			left, &pktlen_raw, options);
	if (*status != PACKET_READ_EOF) {
		*appended = pktlen_raw + (rpc->write_line_lengths ? 4 : 0);
		rpc->len += *appended;
	}

	if (rpc->write_line_lengths) {
		switch (*status) {
		case PACKET_READ_EOF:
			if (!(options & PACKET_READ_GENTLE_ON_EOF))
				die(_("shouldn't have EOF when not gentle on EOF"));
			break;
		case PACKET_READ_NORMAL:
			set_packet_header(buf - 4, *appended);
			break;
		case PACKET_READ_DELIM:
			memcpy(buf - 4, "0001", 4);
			break;
		case PACKET_READ_FLUSH:
			memcpy(buf - 4, "0000", 4);
			break;
		case PACKET_READ_RESPONSE_END:
			die(_("remote server sent unexpected response end packet"));
		}
	}

	return 1;
}

static size_t rpc_out(void *ptr, size_t eltsize,
		size_t nmemb, void *buffer_)
{
	size_t max = eltsize * nmemb;
	struct rpc_state *rpc = buffer_;
	size_t avail = rpc->len - rpc->pos;
	enum packet_read_status status;

	if (!avail) {
		rpc->initial_buffer = 0;
		rpc->len = 0;
		rpc->pos = 0;
		if (!rpc->flush_read_but_not_sent) {
			if (!rpc_read_from_out(rpc, 0, &avail, &status))
				BUG("The entire rpc->buf should be larger than LARGE_PACKET_MAX");
			if (status == PACKET_READ_FLUSH)
				rpc->flush_read_but_not_sent = 1;
		}
		/*
		 * If flush_read_but_not_sent is true, we have already read one
		 * full request but have not fully sent it + EOF, which is why
		 * we need to refrain from reading.
		 */
	}
	if (rpc->flush_read_but_not_sent) {
		if (!avail) {
			/*
			 * The line length either does not need to be sent at
			 * all or has already been completely sent. Now we can
			 * return 0, indicating EOF, meaning that the flush has
			 * been fully sent.
			 */
			rpc->flush_read_but_not_sent = 0;
			return 0;
		}
		/*
		 * If avail is non-zero, the line length for the flush still
		 * hasn't been fully sent. Proceed with sending the line
		 * length.
		 */
	}

	if (max < avail)
		avail = max;
	memcpy(ptr, rpc->buf + rpc->pos, avail);
	rpc->pos += avail;
	return avail;
}

static int rpc_seek(void *clientp, curl_off_t offset, int origin)
{
	struct rpc_state *rpc = clientp;

	if (origin != SEEK_SET)
		BUG("rpc_seek only handles SEEK_SET, not %d", origin);

	if (rpc->initial_buffer) {
		if (offset < 0 || offset > rpc->len) {
			error("curl seek would be outside of rpc buffer");
			return CURL_SEEKFUNC_FAIL;
		}
		rpc->pos = offset;
		return CURL_SEEKFUNC_OK;
	}
	error(_("unable to rewind rpc post data - try increasing http.postBuffer"));
	return CURL_SEEKFUNC_FAIL;
}

struct check_pktline_state {
	char len_buf[4];
	int len_filled;
	int remaining;
};

static void check_pktline(struct check_pktline_state *state, const char *ptr, size_t size)
{
	while (size) {
		if (!state->remaining) {
			int digits_remaining = 4 - state->len_filled;
			if (digits_remaining > size)
				digits_remaining = size;
			memcpy(&state->len_buf[state->len_filled], ptr, digits_remaining);
			state->len_filled += digits_remaining;
			ptr += digits_remaining;
			size -= digits_remaining;

			if (state->len_filled == 4) {
				state->remaining = packet_length(state->len_buf);
				if (state->remaining < 0) {
					die(_("remote-curl: bad line length character: %.4s"), state->len_buf);
				} else if (state->remaining == 2) {
					die(_("remote-curl: unexpected response end packet"));
				} else if (state->remaining < 4) {
					state->remaining = 0;
				} else {
					state->remaining -= 4;
				}
				state->len_filled = 0;
			}
		}

		if (state->remaining) {
			int remaining = state->remaining;
			if (remaining > size)
				remaining = size;
			ptr += remaining;
			size -= remaining;
			state->remaining -= remaining;
		}
	}
}

struct rpc_in_data {
	struct rpc_state *rpc;
	struct active_request_slot *slot;
	int check_pktline;
	struct check_pktline_state pktline_state;
};

/*
 * A callback for CURLOPT_WRITEFUNCTION. The return value is the bytes consumed
 * from ptr.
 */
static size_t rpc_in(char *ptr, size_t eltsize,
		size_t nmemb, void *buffer_)
{
	size_t size = eltsize * nmemb;
	struct rpc_in_data *data = buffer_;
	long response_code;

	if (curl_easy_getinfo(data->slot->curl, CURLINFO_RESPONSE_CODE,
			      &response_code) != CURLE_OK)
		return size;
	if (response_code >= 300)
		return size;
	if (size)
		data->rpc->any_written = 1;
	if (data->check_pktline)
		check_pktline(&data->pktline_state, ptr, size);
	write_or_die(data->rpc->in, ptr, size);
	return size;
}

static int run_slot(struct active_request_slot *slot,
		    struct slot_results *results)
{
	int err;
	struct slot_results results_buf;

	if (!results)
		results = &results_buf;

	err = run_one_slot(slot, results);

	if (err != HTTP_OK && err != HTTP_REAUTH) {
		struct strbuf msg = STRBUF_INIT;
		if (results->http_code && results->http_code != 200)
			strbuf_addf(&msg, "HTTP %ld", results->http_code);
		if (results->curl_result != CURLE_OK) {
			if (msg.len)
				strbuf_addch(&msg, ' ');
			strbuf_addf(&msg, "curl %d", results->curl_result);
			if (curl_errorstr[0]) {
				strbuf_addch(&msg, ' ');
				strbuf_addstr(&msg, curl_errorstr);
			}
		}
		error(_("RPC failed; %s"), msg.buf);
		strbuf_release(&msg);
	}

	return err;
}

static int probe_rpc(struct rpc_state *rpc, struct slot_results *results)
{
	struct active_request_slot *slot;
	struct curl_slist *headers = http_copy_default_headers();
	struct strbuf buf = STRBUF_INIT;
	int err;

	slot = get_active_slot();

	headers = curl_slist_append(headers, rpc->hdr_content_type);
	headers = curl_slist_append(headers, rpc->hdr_accept);

	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(slot->curl, CURLOPT_POST, 1);
	curl_easy_setopt(slot->curl, CURLOPT_URL, rpc->service_url);
	curl_easy_setopt(slot->curl, CURLOPT_ENCODING, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, "0000");
	curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE, 4);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEDATA, &buf);

	err = run_slot(slot, results);

	curl_slist_free_all(headers);
	strbuf_release(&buf);
	return err;
}

static curl_off_t xcurl_off_t(size_t len)
{
	uintmax_t size = len;
	if (size > maximum_signed_value_of_type(curl_off_t))
		die(_("cannot handle pushes this big"));
	return (curl_off_t)size;
}

/*
 * If flush_received is true, do not attempt to read any more; just use what's
 * in rpc->buf.
 */
static int post_rpc(struct rpc_state *rpc, int stateless_connect, int flush_received)
{
	struct active_request_slot *slot;
	struct curl_slist *headers = http_copy_default_headers();
	int use_gzip = rpc->gzip_request;
	char *gzip_body = NULL;
	size_t gzip_size = 0;
	int err, large_request = 0;
	int needs_100_continue = 0;
	struct rpc_in_data rpc_in_data;

	/* Try to load the entire request, if we can fit it into the
	 * allocated buffer space we can use HTTP/1.0 and avoid the
	 * chunked encoding mess.
	 */
	if (!flush_received) {
		while (1) {
			size_t n;
			enum packet_read_status status;

			if (!rpc_read_from_out(rpc, 0, &n, &status)) {
				large_request = 1;
				use_gzip = 0;
				break;
			}
			if (status == PACKET_READ_FLUSH)
				break;
		}
	}

	if (large_request) {
		struct slot_results results;

		do {
			err = probe_rpc(rpc, &results);
			if (err == HTTP_REAUTH)
				credential_fill(&http_auth);
		} while (err == HTTP_REAUTH);
		if (err != HTTP_OK)
			return -1;

		if (results.auth_avail & CURLAUTH_GSSNEGOTIATE)
			needs_100_continue = 1;
	}

	headers = curl_slist_append(headers, rpc->hdr_content_type);
	headers = curl_slist_append(headers, rpc->hdr_accept);
	headers = curl_slist_append(headers, needs_100_continue ?
		"Expect: 100-continue" : "Expect:");

	/* Add Accept-Language header */
	if (rpc->hdr_accept_language)
		headers = curl_slist_append(headers, rpc->hdr_accept_language);

	/* Add the extra Git-Protocol header */
	if (rpc->protocol_header)
		headers = curl_slist_append(headers, rpc->protocol_header);

retry:
	slot = get_active_slot();

	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(slot->curl, CURLOPT_POST, 1);
	curl_easy_setopt(slot->curl, CURLOPT_URL, rpc->service_url);
	curl_easy_setopt(slot->curl, CURLOPT_ENCODING, "");

	if (large_request) {
		/* The request body is large and the size cannot be predicted.
		 * We must use chunked encoding to send it.
		 */
		headers = curl_slist_append(headers, "Transfer-Encoding: chunked");
		rpc->initial_buffer = 1;
		curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, rpc_out);
		curl_easy_setopt(slot->curl, CURLOPT_INFILE, rpc);
		curl_easy_setopt(slot->curl, CURLOPT_SEEKFUNCTION, rpc_seek);
		curl_easy_setopt(slot->curl, CURLOPT_SEEKDATA, rpc);
		if (options.verbosity > 1) {
			fprintf(stderr, "POST %s (chunked)\n", rpc->service_name);
			fflush(stderr);
		}

	} else if (gzip_body) {
		/*
		 * If we are looping to retry authentication, then the previous
		 * run will have set up the headers and gzip buffer already,
		 * and we just need to send it.
		 */
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, gzip_body);
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE_LARGE, xcurl_off_t(gzip_size));

	} else if (use_gzip && 1024 < rpc->len) {
		/* The client backend isn't giving us compressed data so
		 * we can try to deflate it ourselves, this may save on
		 * the transfer time.
		 */
		git_zstream stream;
		int ret;

		git_deflate_init_gzip(&stream, Z_BEST_COMPRESSION);
		gzip_size = git_deflate_bound(&stream, rpc->len);
		gzip_body = xmalloc(gzip_size);

		stream.next_in = (unsigned char *)rpc->buf;
		stream.avail_in = rpc->len;
		stream.next_out = (unsigned char *)gzip_body;
		stream.avail_out = gzip_size;

		ret = git_deflate(&stream, Z_FINISH);
		if (ret != Z_STREAM_END)
			die(_("cannot deflate request; zlib deflate error %d"), ret);

		ret = git_deflate_end_gently(&stream);
		if (ret != Z_OK)
			die(_("cannot deflate request; zlib end error %d"), ret);

		gzip_size = stream.total_out;

		headers = curl_slist_append(headers, "Content-Encoding: gzip");
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, gzip_body);
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE_LARGE, xcurl_off_t(gzip_size));

		if (options.verbosity > 1) {
			fprintf(stderr, "POST %s (gzip %lu to %lu bytes)\n",
				rpc->service_name,
				(unsigned long)rpc->len, (unsigned long)gzip_size);
			fflush(stderr);
		}
	} else {
		/* We know the complete request size in advance, use the
		 * more normal Content-Length approach.
		 */
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, rpc->buf);
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE_LARGE, xcurl_off_t(rpc->len));
		if (options.verbosity > 1) {
			fprintf(stderr, "POST %s (%lu bytes)\n",
				rpc->service_name, (unsigned long)rpc->len);
			fflush(stderr);
		}
	}

	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, rpc_in);
	rpc_in_data.rpc = rpc;
	rpc_in_data.slot = slot;
	rpc_in_data.check_pktline = stateless_connect;
	memset(&rpc_in_data.pktline_state, 0, sizeof(rpc_in_data.pktline_state));
	curl_easy_setopt(slot->curl, CURLOPT_WRITEDATA, &rpc_in_data);
	curl_easy_setopt(slot->curl, CURLOPT_FAILONERROR, 0);


	rpc->any_written = 0;
	err = run_slot(slot, NULL);
	if (err == HTTP_REAUTH && !large_request) {
		credential_fill(&http_auth);
		goto retry;
	}
	if (err != HTTP_OK)
		err = -1;

	if (!rpc->any_written)
		err = -1;

	if (rpc_in_data.pktline_state.len_filled)
		err = error(_("%d bytes of length header were received"), rpc_in_data.pktline_state.len_filled);
	if (rpc_in_data.pktline_state.remaining)
		err = error(_("%d bytes of body are still expected"), rpc_in_data.pktline_state.remaining);

	if (stateless_connect)
		packet_response_end(rpc->in);

	curl_slist_free_all(headers);
	free(gzip_body);
	return err;
}

static int rpc_service(struct rpc_state *rpc, struct discovery *heads,
		       const char **client_argv, const struct strbuf *preamble,
		       struct strbuf *rpc_result)
{
	const char *svc = rpc->service_name;
	struct strbuf buf = STRBUF_INIT;
	struct child_process client = CHILD_PROCESS_INIT;
	int err = 0;

	client.in = -1;
	client.out = -1;
	client.git_cmd = 1;
	strvec_pushv(&client.args, client_argv);
	if (start_command(&client))
		exit(1);
	write_or_die(client.in, preamble->buf, preamble->len);
	if (heads)
		write_or_die(client.in, heads->buf, heads->len);

	rpc->alloc = http_post_buffer;
	rpc->buf = xmalloc(rpc->alloc);
	rpc->in = client.in;
	rpc->out = client.out;

	strbuf_addf(&buf, "%s%s", url.buf, svc);
	rpc->service_url = strbuf_detach(&buf, NULL);

	rpc->hdr_accept_language = xstrdup_or_null(http_get_accept_language_header());

	strbuf_addf(&buf, "Content-Type: application/x-%s-request", svc);
	rpc->hdr_content_type = strbuf_detach(&buf, NULL);

	strbuf_addf(&buf, "Accept: application/x-%s-result", svc);
	rpc->hdr_accept = strbuf_detach(&buf, NULL);

	if (get_protocol_http_header(heads->version, &buf))
		rpc->protocol_header = strbuf_detach(&buf, NULL);
	else
		rpc->protocol_header = NULL;

	while (!err) {
		int n = packet_read(rpc->out, rpc->buf, rpc->alloc, 0);
		if (!n)
			break;
		rpc->pos = 0;
		rpc->len = n;
		err |= post_rpc(rpc, 0, 0);
	}

	close(client.in);
	client.in = -1;
	if (!err) {
		strbuf_read(rpc_result, client.out, 0);
	} else {
		char buf[4096];
		for (;;)
			if (xread(client.out, buf, sizeof(buf)) <= 0)
				break;
	}

	close(client.out);
	client.out = -1;

	err |= finish_command(&client);
	free(rpc->service_url);
	free(rpc->hdr_content_type);
	free(rpc->hdr_accept);
	free(rpc->hdr_accept_language);
	free(rpc->protocol_header);
	free(rpc->buf);
	strbuf_release(&buf);
	return err;
}

static int fetch_dumb(int nr_heads, struct ref **to_fetch)
{
	struct walker *walker;
	char **targets;
	int ret, i;

	ALLOC_ARRAY(targets, nr_heads);
	if (options.depth || options.deepen_since)
		die(_("dumb http transport does not support shallow capabilities"));
	for (i = 0; i < nr_heads; i++)
		targets[i] = xstrdup(oid_to_hex(&to_fetch[i]->old_oid));

	walker = get_http_walker(url.buf);
	walker->get_verbosely = options.verbosity >= 3;
	walker->get_progress = options.progress;
	walker->get_recover = 0;
	ret = walker_fetch(walker, nr_heads, targets, NULL, NULL);
	walker_free(walker);

	for (i = 0; i < nr_heads; i++)
		free(targets[i]);
	free(targets);

	return ret ? error(_("fetch failed.")) : 0;
}

static int fetch_git(struct discovery *heads,
	int nr_heads, struct ref **to_fetch)
{
	struct rpc_state rpc = RPC_STATE_INIT;
	struct strbuf preamble = STRBUF_INIT;
	int i, err;
	struct strvec args = STRVEC_INIT;
	struct strbuf rpc_result = STRBUF_INIT;

	strvec_pushl(&args, "fetch-pack", "--stateless-rpc",
		     "--stdin", "--lock-pack", NULL);
	if (options.followtags)
		strvec_push(&args, "--include-tag");
	if (options.thin)
		strvec_push(&args, "--thin");
	if (options.verbosity >= 3)
		strvec_pushl(&args, "-v", "-v", NULL);
	if (options.check_self_contained_and_connected)
		strvec_push(&args, "--check-self-contained-and-connected");
	if (options.cloning)
		strvec_push(&args, "--cloning");
	if (options.update_shallow)
		strvec_push(&args, "--update-shallow");
	if (!options.progress)
		strvec_push(&args, "--no-progress");
	if (options.depth)
		strvec_pushf(&args, "--depth=%lu", options.depth);
	if (options.deepen_since)
		strvec_pushf(&args, "--shallow-since=%s", options.deepen_since);
	for (i = 0; i < options.deepen_not.nr; i++)
		strvec_pushf(&args, "--shallow-exclude=%s",
			     options.deepen_not.items[i].string);
	if (options.deepen_relative && options.depth)
		strvec_push(&args, "--deepen-relative");
	if (options.from_promisor)
		strvec_push(&args, "--from-promisor");
	if (options.refetch)
		strvec_push(&args, "--refetch");
	if (options.filter)
		strvec_pushf(&args, "--filter=%s", options.filter);
	strvec_push(&args, url.buf);

	for (i = 0; i < nr_heads; i++) {
		struct ref *ref = to_fetch[i];
		if (!*ref->name)
			die(_("cannot fetch by sha1 over smart http"));
		packet_buf_write(&preamble, "%s %s\n",
				 oid_to_hex(&ref->old_oid), ref->name);
	}
	packet_buf_flush(&preamble);

	memset(&rpc, 0, sizeof(rpc));
	rpc.service_name = "git-upload-pack",
	rpc.gzip_request = 1;

	err = rpc_service(&rpc, heads, args.v, &preamble, &rpc_result);
	if (rpc_result.len)
		write_or_die(1, rpc_result.buf, rpc_result.len);
	strbuf_release(&rpc_result);
	strbuf_release(&preamble);
	strvec_clear(&args);
	return err;
}

static int fetch(int nr_heads, struct ref **to_fetch)
{
	struct discovery *d = discover_refs("git-upload-pack", 0);
	if (d->proto_git)
		return fetch_git(d, nr_heads, to_fetch);
	else
		return fetch_dumb(nr_heads, to_fetch);
}

static void parse_fetch(struct strbuf *buf)
{
	struct ref **to_fetch = NULL;
	struct ref *list_head = NULL;
	struct ref **list = &list_head;
	int alloc_heads = 0, nr_heads = 0;

	do {
		const char *p;
		if (skip_prefix(buf->buf, "fetch ", &p)) {
			const char *name;
			struct ref *ref;
			struct object_id old_oid;
			const char *q;

			if (parse_oid_hex(p, &old_oid, &q))
				die(_("protocol error: expected sha/ref, got '%s'"), p);
			if (*q == ' ')
				name = q + 1;
			else if (!*q)
				name = "";
			else
				die(_("protocol error: expected sha/ref, got '%s'"), p);

			ref = alloc_ref(name);
			oidcpy(&ref->old_oid, &old_oid);

			*list = ref;
			list = &ref->next;

			ALLOC_GROW(to_fetch, nr_heads + 1, alloc_heads);
			to_fetch[nr_heads++] = ref;
		}
		else
			die(_("http transport does not support %s"), buf->buf);

		strbuf_reset(buf);
		if (strbuf_getline_lf(buf, stdin) == EOF)
			return;
		if (!*buf->buf)
			break;
	} while (1);

	if (fetch(nr_heads, to_fetch))
		exit(128); /* error already reported */
	free_refs(list_head);
	free(to_fetch);

	printf("\n");
	fflush(stdout);
	strbuf_reset(buf);
}

static void parse_get(const char *arg)
{
	struct strbuf url = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	const char *space;

	space = strchr(arg, ' ');

	if (!space)
		die(_("protocol error: expected '<url> <path>', missing space"));

	strbuf_add(&url, arg, space - arg);
	strbuf_addstr(&path, space + 1);

	if (http_get_file(url.buf, path.buf, NULL))
		die(_("failed to download file at URL '%s'"), url.buf);

	strbuf_release(&url);
	strbuf_release(&path);
	printf("\n");
	fflush(stdout);
}

static int push_dav(int nr_spec, const char **specs)
{
	struct child_process child = CHILD_PROCESS_INIT;
	size_t i;

	child.git_cmd = 1;
	strvec_push(&child.args, "http-push");
	strvec_push(&child.args, "--helper-status");
	if (options.dry_run)
		strvec_push(&child.args, "--dry-run");
	if (options.verbosity > 1)
		strvec_push(&child.args, "--verbose");
	strvec_push(&child.args, url.buf);
	for (i = 0; i < nr_spec; i++)
		strvec_push(&child.args, specs[i]);

	if (run_command(&child))
		die(_("git-http-push failed"));
	return 0;
}

static int push_git(struct discovery *heads, int nr_spec, const char **specs)
{
	struct rpc_state rpc = RPC_STATE_INIT;
	int i, err;
	struct strvec args;
	struct string_list_item *cas_option;
	struct strbuf preamble = STRBUF_INIT;
	struct strbuf rpc_result = STRBUF_INIT;

	strvec_init(&args);
	strvec_pushl(&args, "send-pack", "--stateless-rpc", "--helper-status",
		     NULL);

	if (options.thin)
		strvec_push(&args, "--thin");
	if (options.dry_run)
		strvec_push(&args, "--dry-run");
	if (options.push_cert == SEND_PACK_PUSH_CERT_ALWAYS)
		strvec_push(&args, "--signed=yes");
	else if (options.push_cert == SEND_PACK_PUSH_CERT_IF_ASKED)
		strvec_push(&args, "--signed=if-asked");
	if (options.atomic)
		strvec_push(&args, "--atomic");
	if (options.verbosity == 0)
		strvec_push(&args, "--quiet");
	else if (options.verbosity > 1)
		strvec_push(&args, "--verbose");
	for (i = 0; i < options.push_options.nr; i++)
		strvec_pushf(&args, "--push-option=%s",
			     options.push_options.items[i].string);
	strvec_push(&args, options.progress ? "--progress" : "--no-progress");
	for_each_string_list_item(cas_option, &cas_options)
		strvec_push(&args, cas_option->string);
	strvec_push(&args, url.buf);

	if (options.force_if_includes)
		strvec_push(&args, "--force-if-includes");

	strvec_push(&args, "--stdin");
	for (i = 0; i < nr_spec; i++)
		packet_buf_write(&preamble, "%s\n", specs[i]);
	packet_buf_flush(&preamble);

	memset(&rpc, 0, sizeof(rpc));
	rpc.service_name = "git-receive-pack",

	err = rpc_service(&rpc, heads, args.v, &preamble, &rpc_result);
	if (rpc_result.len)
		write_or_die(1, rpc_result.buf, rpc_result.len);
	strbuf_release(&rpc_result);
	strbuf_release(&preamble);
	strvec_clear(&args);
	return err;
}

static int push(int nr_spec, const char **specs)
{
	struct discovery *heads = discover_refs("git-receive-pack", 1);
	int ret;

	if (heads->proto_git)
		ret = push_git(heads, nr_spec, specs);
	else
		ret = push_dav(nr_spec, specs);
	free_discovery(heads);
	return ret;
}

static void parse_push(struct strbuf *buf)
{
	struct strvec specs = STRVEC_INIT;
	int ret;

	do {
		const char *arg;
		if (skip_prefix(buf->buf, "push ", &arg))
			strvec_push(&specs, arg);
		else
			die(_("http transport does not support %s"), buf->buf);

		strbuf_reset(buf);
		if (strbuf_getline_lf(buf, stdin) == EOF)
			goto free_specs;
		if (!*buf->buf)
			break;
	} while (1);

	ret = push(specs.nr, specs.v);
	printf("\n");
	fflush(stdout);

	if (ret)
		exit(128); /* error already reported */

free_specs:
	strvec_clear(&specs);
}

static int stateless_connect(const char *service_name)
{
	struct discovery *discover;
	struct rpc_state rpc = RPC_STATE_INIT;
	struct strbuf buf = STRBUF_INIT;
	const char *accept_language;

	/*
	 * Run the info/refs request and see if the server supports protocol
	 * v2.  If and only if the server supports v2 can we successfully
	 * establish a stateless connection, otherwise we need to tell the
	 * client to fallback to using other transport helper functions to
	 * complete their request.
	 */
	discover = discover_refs(service_name, 0);
	if (discover->version != protocol_v2) {
		printf("fallback\n");
		fflush(stdout);
		return -1;
	} else {
		/* Stateless Connection established */
		printf("\n");
		fflush(stdout);
	}
	accept_language = http_get_accept_language_header();
	if (accept_language)
		rpc.hdr_accept_language = xstrfmt("%s", accept_language);

	rpc.service_name = service_name;
	rpc.service_url = xstrfmt("%s%s", url.buf, rpc.service_name);
	rpc.hdr_content_type = xstrfmt("Content-Type: application/x-%s-request", rpc.service_name);
	rpc.hdr_accept = xstrfmt("Accept: application/x-%s-result", rpc.service_name);
	if (get_protocol_http_header(discover->version, &buf)) {
		rpc.protocol_header = strbuf_detach(&buf, NULL);
	} else {
		rpc.protocol_header = NULL;
		strbuf_release(&buf);
	}
	rpc.buf = xmalloc(http_post_buffer);
	rpc.alloc = http_post_buffer;
	rpc.len = 0;
	rpc.pos = 0;
	rpc.in = 1;
	rpc.out = 0;
	rpc.any_written = 0;
	rpc.gzip_request = 1;
	rpc.initial_buffer = 0;
	rpc.write_line_lengths = 1;
	rpc.flush_read_but_not_sent = 0;

	/*
	 * Dump the capability listing that we got from the server earlier
	 * during the info/refs request.
	 */
	write_or_die(rpc.in, discover->buf, discover->len);

	/* Until we see EOF keep sending POSTs */
	while (1) {
		size_t avail;
		enum packet_read_status status;

		if (!rpc_read_from_out(&rpc, PACKET_READ_GENTLE_ON_EOF, &avail,
				       &status))
			BUG("The entire rpc->buf should be larger than LARGE_PACKET_MAX");
		if (status == PACKET_READ_EOF)
			break;
		if (post_rpc(&rpc, 1, status == PACKET_READ_FLUSH))
			/* We would have an err here */
			break;
		/* Reset the buffer for next request */
		rpc.len = 0;
	}

	free(rpc.service_url);
	free(rpc.hdr_content_type);
	free(rpc.hdr_accept);
	free(rpc.hdr_accept_language);
	free(rpc.protocol_header);
	free(rpc.buf);
	strbuf_release(&buf);

	return 0;
}

int cmd_main(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;
	int nongit;
	int ret = 1;

	setup_git_directory_gently(&nongit);
	if (argc < 2) {
		error(_("remote-curl: usage: git remote-curl <remote> [<url>]"));
		goto cleanup;
	}

	options.verbosity = 1;
	options.progress = !!isatty(2);
	options.thin = 1;
	string_list_init_dup(&options.deepen_not);
	string_list_init_dup(&options.push_options);

	/*
	 * Just report "remote-curl" here (folding all the various aliases
	 * ("git-remote-http", "git-remote-https", and etc.) here since they
	 * are all just copies of the same actual executable.
	 */
	trace2_cmd_name("remote-curl");

	remote = remote_get(argv[1]);

	if (argc > 2) {
		end_url_with_slash(&url, argv[2]);
	} else {
		end_url_with_slash(&url, remote->url[0]);
	}

	http_init(remote, url.buf, 0);

	do {
		const char *arg;

		if (strbuf_getline_lf(&buf, stdin) == EOF) {
			if (ferror(stdin))
				error(_("remote-curl: error reading command stream from git"));
			goto cleanup;
		}
		if (buf.len == 0)
			break;
		if (starts_with(buf.buf, "fetch ")) {
			if (nongit)
				die(_("remote-curl: fetch attempted without a local repo"));
			parse_fetch(&buf);

		} else if (!strcmp(buf.buf, "list") || starts_with(buf.buf, "list ")) {
			int for_push = !!strstr(buf.buf + 4, "for-push");
			output_refs(get_refs(for_push));

		} else if (starts_with(buf.buf, "push ")) {
			parse_push(&buf);

		} else if (skip_prefix(buf.buf, "option ", &arg)) {
			char *value = strchr(arg, ' ');
			int result;

			if (value)
				*value++ = '\0';
			else
				value = "true";

			result = set_option(arg, value);
			if (!result)
				printf("ok\n");
			else if (result < 0)
				printf("error invalid value\n");
			else
				printf("unsupported\n");
			fflush(stdout);

		} else if (skip_prefix(buf.buf, "get ", &arg)) {
			parse_get(arg);
			fflush(stdout);

		} else if (!strcmp(buf.buf, "capabilities")) {
			printf("stateless-connect\n");
			printf("fetch\n");
			printf("get\n");
			printf("option\n");
			printf("push\n");
			printf("check-connectivity\n");
			printf("object-format\n");
			printf("\n");
			fflush(stdout);
		} else if (skip_prefix(buf.buf, "stateless-connect ", &arg)) {
			if (!stateless_connect(arg))
				break;
		} else {
			error(_("remote-curl: unknown command '%s' from git"), buf.buf);
			goto cleanup;
		}
		strbuf_reset(&buf);
	} while (1);

	http_cleanup();
	ret = 0;
cleanup:
	strbuf_release(&buf);

	return ret;
}
