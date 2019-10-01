#include "cache.h"
#include "config.h"
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
#include "argv-array.h"
#include "credential.h"
#include "sha1-array.h"
#include "send-pack.h"
#include "protocol.h"
#include "quote.h"
#include "transport.h"

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
		from_promisor : 1,
		no_dependents : 1;
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
		strbuf_addf(&val, "--" CAS_OPT_NAME "=%s", value);
		string_list_append(&cas_options, val.buf);
		strbuf_release(&val);
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

#if LIBCURL_VERSION_NUM >= 0x070a08
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
#endif /* LIBCURL_VERSION_NUM >= 0x070a08 */
	} else if (!strcmp(name, "from-promisor")) {
		options.from_promisor = 1;
		return 0;
	} else if (!strcmp(name, "no-dependents")) {
		options.no_dependents = 1;
		return 0;
	} else if (!strcmp(name, "filter")) {
		options.filter = xstrdup(value);
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
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	return list;
}

static struct ref *parse_info_refs(struct discovery *heads)
{
	char *data, *start, *mid;
	char *ref_name;
	int i = 0;

	struct ref *refs = NULL;
	struct ref *ref = NULL;
	struct ref *last_ref = NULL;

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
			if (mid - start != the_hash_algo->hexsz)
				die(_("%sinfo/refs not valid: is this a git repository?"),
				    transport_anonymize_url(url.buf));
			data[i] = 0;
			ref_name = mid + 1;
			ref = alloc_ref(ref_name);
			get_oid_hex(start, &ref->old_oid);
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
	 * to perform a push, then fallback to v0 since the client doesn't know
	 * how to push yet using v2.
	 */
	if (version == protocol_v2 && !strcmp("git-receive-pack", service))
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
	for (posn = refs; posn; posn = posn->next) {
		if (posn->symref)
			printf("@%s %s\n", posn->symref, posn->name);
		else
			printf("%s %s\n", oid_to_hex(&posn->old_oid), posn->name);
	}
	printf("\n");
	fflush(stdout);
}

struct rpc_state {
	const char *service_name;
	char *service_url;
	char *hdr_content_type;
	char *hdr_accept;
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
		 * If avail is non-zerp, the line length for the flush still
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

#ifndef NO_CURL_IOCTL
static curlioerr rpc_ioctl(CURL *handle, int cmd, void *clientp)
{
	struct rpc_state *rpc = clientp;

	switch (cmd) {
	case CURLIOCMD_NOP:
		return CURLIOE_OK;

	case CURLIOCMD_RESTARTREAD:
		if (rpc->initial_buffer) {
			rpc->pos = 0;
			return CURLIOE_OK;
		}
		error(_("unable to rewind rpc post data - try increasing http.postBuffer"));
		return CURLIOE_FAILRESTART;

	default:
		return CURLIOE_UNKNOWNCMD;
	}
}
#endif

struct rpc_in_data {
	struct rpc_state *rpc;
	struct active_request_slot *slot;
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
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buf);

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
static int post_rpc(struct rpc_state *rpc, int flush_received)
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
#ifndef NO_CURL_IOCTL
		curl_easy_setopt(slot->curl, CURLOPT_IOCTLFUNCTION, rpc_ioctl);
		curl_easy_setopt(slot->curl, CURLOPT_IOCTLDATA, rpc);
#endif
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
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &rpc_in_data);
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
	client.argv = client_argv;
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

	strbuf_addf(&buf, "Content-Type: application/x-%s-request", svc);
	rpc->hdr_content_type = strbuf_detach(&buf, NULL);

	strbuf_addf(&buf, "Accept: application/x-%s-result", svc);
	rpc->hdr_accept = strbuf_detach(&buf, NULL);

	if (get_protocol_http_header(heads->version, &buf))
		rpc->protocol_header = strbuf_detach(&buf, NULL);
	else
		rpc->protocol_header = NULL;

	while (!err) {
		int n = packet_read(rpc->out, NULL, NULL, rpc->buf, rpc->alloc, 0);
		if (!n)
			break;
		rpc->pos = 0;
		rpc->len = n;
		err |= post_rpc(rpc, 0);
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
	struct rpc_state rpc;
	struct strbuf preamble = STRBUF_INIT;
	int i, err;
	struct argv_array args = ARGV_ARRAY_INIT;
	struct strbuf rpc_result = STRBUF_INIT;

	argv_array_pushl(&args, "fetch-pack", "--stateless-rpc",
			 "--stdin", "--lock-pack", NULL);
	if (options.followtags)
		argv_array_push(&args, "--include-tag");
	if (options.thin)
		argv_array_push(&args, "--thin");
	if (options.verbosity >= 3)
		argv_array_pushl(&args, "-v", "-v", NULL);
	if (options.check_self_contained_and_connected)
		argv_array_push(&args, "--check-self-contained-and-connected");
	if (options.cloning)
		argv_array_push(&args, "--cloning");
	if (options.update_shallow)
		argv_array_push(&args, "--update-shallow");
	if (!options.progress)
		argv_array_push(&args, "--no-progress");
	if (options.depth)
		argv_array_pushf(&args, "--depth=%lu", options.depth);
	if (options.deepen_since)
		argv_array_pushf(&args, "--shallow-since=%s", options.deepen_since);
	for (i = 0; i < options.deepen_not.nr; i++)
		argv_array_pushf(&args, "--shallow-exclude=%s",
				 options.deepen_not.items[i].string);
	if (options.deepen_relative && options.depth)
		argv_array_push(&args, "--deepen-relative");
	if (options.from_promisor)
		argv_array_push(&args, "--from-promisor");
	if (options.no_dependents)
		argv_array_push(&args, "--no-dependents");
	if (options.filter)
		argv_array_pushf(&args, "--filter=%s", options.filter);
	argv_array_push(&args, url.buf);

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

	err = rpc_service(&rpc, heads, args.argv, &preamble, &rpc_result);
	if (rpc_result.len)
		write_or_die(1, rpc_result.buf, rpc_result.len);
	strbuf_release(&rpc_result);
	strbuf_release(&preamble);
	argv_array_clear(&args);
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

static int push_dav(int nr_spec, char **specs)
{
	struct child_process child = CHILD_PROCESS_INIT;
	size_t i;

	child.git_cmd = 1;
	argv_array_push(&child.args, "http-push");
	argv_array_push(&child.args, "--helper-status");
	if (options.dry_run)
		argv_array_push(&child.args, "--dry-run");
	if (options.verbosity > 1)
		argv_array_push(&child.args, "--verbose");
	argv_array_push(&child.args, url.buf);
	for (i = 0; i < nr_spec; i++)
		argv_array_push(&child.args, specs[i]);

	if (run_command(&child))
		die(_("git-http-push failed"));
	return 0;
}

static int push_git(struct discovery *heads, int nr_spec, char **specs)
{
	struct rpc_state rpc;
	int i, err;
	struct argv_array args;
	struct string_list_item *cas_option;
	struct strbuf preamble = STRBUF_INIT;
	struct strbuf rpc_result = STRBUF_INIT;

	argv_array_init(&args);
	argv_array_pushl(&args, "send-pack", "--stateless-rpc", "--helper-status",
			 NULL);

	if (options.thin)
		argv_array_push(&args, "--thin");
	if (options.dry_run)
		argv_array_push(&args, "--dry-run");
	if (options.push_cert == SEND_PACK_PUSH_CERT_ALWAYS)
		argv_array_push(&args, "--signed=yes");
	else if (options.push_cert == SEND_PACK_PUSH_CERT_IF_ASKED)
		argv_array_push(&args, "--signed=if-asked");
	if (options.verbosity == 0)
		argv_array_push(&args, "--quiet");
	else if (options.verbosity > 1)
		argv_array_push(&args, "--verbose");
	for (i = 0; i < options.push_options.nr; i++)
		argv_array_pushf(&args, "--push-option=%s",
				 options.push_options.items[i].string);
	argv_array_push(&args, options.progress ? "--progress" : "--no-progress");
	for_each_string_list_item(cas_option, &cas_options)
		argv_array_push(&args, cas_option->string);
	argv_array_push(&args, url.buf);

	argv_array_push(&args, "--stdin");
	for (i = 0; i < nr_spec; i++)
		packet_buf_write(&preamble, "%s\n", specs[i]);
	packet_buf_flush(&preamble);

	memset(&rpc, 0, sizeof(rpc));
	rpc.service_name = "git-receive-pack",

	err = rpc_service(&rpc, heads, args.argv, &preamble, &rpc_result);
	if (rpc_result.len)
		write_or_die(1, rpc_result.buf, rpc_result.len);
	strbuf_release(&rpc_result);
	strbuf_release(&preamble);
	argv_array_clear(&args);
	return err;
}

static int push(int nr_spec, char **specs)
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
	char **specs = NULL;
	int alloc_spec = 0, nr_spec = 0, i, ret;

	do {
		if (starts_with(buf->buf, "push ")) {
			ALLOC_GROW(specs, nr_spec + 1, alloc_spec);
			specs[nr_spec++] = xstrdup(buf->buf + 5);
		}
		else
			die(_("http transport does not support %s"), buf->buf);

		strbuf_reset(buf);
		if (strbuf_getline_lf(buf, stdin) == EOF)
			goto free_specs;
		if (!*buf->buf)
			break;
	} while (1);

	ret = push(nr_spec, specs);
	printf("\n");
	fflush(stdout);

	if (ret)
		exit(128); /* error already reported */

 free_specs:
	for (i = 0; i < nr_spec; i++)
		free(specs[i]);
	free(specs);
}

static int stateless_connect(const char *service_name)
{
	struct discovery *discover;
	struct rpc_state rpc;
	struct strbuf buf = STRBUF_INIT;

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
		if (post_rpc(&rpc, status == PACKET_READ_FLUSH))
			/* We would have an err here */
			break;
		/* Reset the buffer for next request */
		rpc.len = 0;
	}

	free(rpc.service_url);
	free(rpc.hdr_content_type);
	free(rpc.hdr_accept);
	free(rpc.protocol_header);
	free(rpc.buf);
	strbuf_release(&buf);

	return 0;
}

int cmd_main(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;
	int nongit;

	setup_git_directory_gently(&nongit);
	if (argc < 2) {
		error(_("remote-curl: usage: git remote-curl <remote> [<url>]"));
		return 1;
	}

	options.verbosity = 1;
	options.progress = !!isatty(2);
	options.thin = 1;
	string_list_init(&options.deepen_not, 1);
	string_list_init(&options.push_options, 1);

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
			return 1;
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

		} else if (!strcmp(buf.buf, "capabilities")) {
			printf("stateless-connect\n");
			printf("fetch\n");
			printf("option\n");
			printf("push\n");
			printf("check-connectivity\n");
			printf("\n");
			fflush(stdout);
		} else if (skip_prefix(buf.buf, "stateless-connect ", &arg)) {
			if (!stateless_connect(arg))
				break;
		} else {
			error(_("remote-curl: unknown command '%s' from git"), buf.buf);
			return 1;
		}
		strbuf_reset(&buf);
	} while (1);

	http_cleanup();

	return 0;
}
