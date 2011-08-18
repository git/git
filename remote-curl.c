#include "cache.h"
#include "remote.h"
#include "strbuf.h"
#include "walker.h"
#include "http.h"
#include "exec_cmd.h"
#include "run-command.h"
#include "pkt-line.h"
#include "sideband.h"

static struct remote *remote;
static const char *url; /* always ends with a trailing slash */

struct options {
	int verbosity;
	unsigned long depth;
	unsigned progress : 1,
		followtags : 1,
		dry_run : 1,
		thin : 1;
};
static struct options options;

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
	else {
		return 1 /* unsupported */;
	}
}

struct discovery {
	const char *service;
	char *buf_alloc;
	char *buf;
	size_t len;
	unsigned proto_git : 1;
};
static struct discovery *last_discovery;

static void free_discovery(struct discovery *d)
{
	if (d) {
		if (d == last_discovery)
			last_discovery = NULL;
		free(d->buf_alloc);
		free(d);
	}
}

static struct discovery* discover_refs(const char *service)
{
	struct strbuf buffer = STRBUF_INIT;
	struct discovery *last = last_discovery;
	char *refs_url;
	int http_ret, is_http = 0, proto_git_candidate = 1;

	if (last && !strcmp(service, last->service))
		return last;
	free_discovery(last);

	strbuf_addf(&buffer, "%sinfo/refs", url);
	if (!prefixcmp(url, "http://") || !prefixcmp(url, "https://")) {
		is_http = 1;
		if (!strchr(url, '?'))
			strbuf_addch(&buffer, '?');
		else
			strbuf_addch(&buffer, '&');
		strbuf_addf(&buffer, "service=%s", service);
	}
	refs_url = strbuf_detach(&buffer, NULL);

	http_ret = http_get_strbuf(refs_url, &buffer, HTTP_NO_CACHE);

	/* try again with "plain" url (no ? or & appended) */
	if (http_ret != HTTP_OK) {
		free(refs_url);
		strbuf_reset(&buffer);

		proto_git_candidate = 0;
		strbuf_addf(&buffer, "%sinfo/refs", url);
		refs_url = strbuf_detach(&buffer, NULL);

		http_ret = http_get_strbuf(refs_url, &buffer, HTTP_NO_CACHE);
	}

	switch (http_ret) {
	case HTTP_OK:
		break;
	case HTTP_MISSING_TARGET:
		die("%s not found: did you run git update-server-info on the"
		    " server?", refs_url);
	case HTTP_NOAUTH:
		die("Authentication failed");
	default:
		http_error(refs_url, http_ret);
		die("HTTP request failed");
	}

	last= xcalloc(1, sizeof(*last_discovery));
	last->service = service;
	last->buf_alloc = strbuf_detach(&buffer, &last->len);
	last->buf = last->buf_alloc;

	if (is_http && proto_git_candidate
		&& 5 <= last->len && last->buf[4] == '#') {
		/* smart HTTP response; validate that the service
		 * pkt-line matches our request.
		 */
		struct strbuf exp = STRBUF_INIT;

		if (packet_get_line(&buffer, &last->buf, &last->len) <= 0)
			die("%s has invalid packet header", refs_url);
		if (buffer.len && buffer.buf[buffer.len - 1] == '\n')
			strbuf_setlen(&buffer, buffer.len - 1);

		strbuf_addf(&exp, "# service=%s", service);
		if (strbuf_cmp(&exp, &buffer))
			die("invalid server response; got '%s'", buffer.buf);
		strbuf_release(&exp);

		/* The header can include additional metadata lines, up
		 * until a packet flush marker.  Ignore these now, but
		 * in the future we might start to scan them.
		 */
		strbuf_reset(&buffer);
		while (packet_get_line(&buffer, &last->buf, &last->len) > 0)
			strbuf_reset(&buffer);

		last->proto_git = 1;
	}

	free(refs_url);
	strbuf_release(&buffer);
	last_discovery = last;
	return last;
}

static int write_discovery(int in, int out, void *data)
{
	struct discovery *heads = data;
	int err = 0;
	if (write_in_full(out, heads->buf, heads->len) != heads->len)
		err = 1;
	close(out);
	return err;
}

static struct ref *parse_git_refs(struct discovery *heads)
{
	struct ref *list = NULL;
	struct async async;

	memset(&async, 0, sizeof(async));
	async.proc = write_discovery;
	async.data = heads;
	async.out = -1;

	if (start_async(&async))
		die("cannot start thread to parse advertised refs");
	get_remote_heads(async.out, &list, 0, NULL, 0, NULL);
	close(async.out);
	if (finish_async(&async))
		die("ref parsing thread failed");
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
			if (mid - start != 40)
				die("%sinfo/refs not valid: is this a git repository?", url);
			data[i] = 0;
			ref_name = mid + 1;
			ref = xmalloc(sizeof(struct ref) +
				      strlen(ref_name) + 1);
			memset(ref, 0, sizeof(struct ref));
			strcpy(ref->name, ref_name);
			get_sha1_hex(start, ref->old_sha1);
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
	if (!http_fetch_ref(url, ref) &&
	    !resolve_remote_symref(ref, refs)) {
		ref->next = refs;
		refs = ref;
	} else {
		free(ref);
	}

	return refs;
}

static struct ref *get_refs(int for_push)
{
	struct discovery *heads;

	if (for_push)
		heads = discover_refs("git-receive-pack");
	else
		heads = discover_refs("git-upload-pack");

	if (heads->proto_git)
		return parse_git_refs(heads);
	return parse_info_refs(heads);
}

static void output_refs(struct ref *refs)
{
	struct ref *posn;
	for (posn = refs; posn; posn = posn->next) {
		if (posn->symref)
			printf("@%s %s\n", posn->symref, posn->name);
		else
			printf("%s %s\n", sha1_to_hex(posn->old_sha1), posn->name);
	}
	printf("\n");
	fflush(stdout);
	free_refs(refs);
}

struct rpc_state {
	const char *service_name;
	const char **argv;
	char *service_url;
	char *hdr_content_type;
	char *hdr_accept;
	char *buf;
	size_t alloc;
	size_t len;
	size_t pos;
	int in;
	int out;
	struct strbuf result;
	unsigned gzip_request : 1;
	unsigned initial_buffer : 1;
};

static size_t rpc_out(void *ptr, size_t eltsize,
		size_t nmemb, void *buffer_)
{
	size_t max = eltsize * nmemb;
	struct rpc_state *rpc = buffer_;
	size_t avail = rpc->len - rpc->pos;

	if (!avail) {
		rpc->initial_buffer = 0;
		avail = packet_read_line(rpc->out, rpc->buf, rpc->alloc);
		if (!avail)
			return 0;
		rpc->pos = 0;
		rpc->len = avail;
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
		fprintf(stderr, "Unable to rewind rpc post data - try increasing http.postBuffer\n");
		return CURLIOE_FAILRESTART;

	default:
		return CURLIOE_UNKNOWNCMD;
	}
}
#endif

static size_t rpc_in(char *ptr, size_t eltsize,
		size_t nmemb, void *buffer_)
{
	size_t size = eltsize * nmemb;
	struct rpc_state *rpc = buffer_;
	write_or_die(rpc->in, ptr, size);
	return size;
}

static int run_slot(struct active_request_slot *slot)
{
	int err = 0;
	struct slot_results results;

	slot->results = &results;
	slot->curl_result = curl_easy_perform(slot->curl);
	finish_active_slot(slot);

	if (results.curl_result != CURLE_OK) {
		err |= error("RPC failed; result=%d, HTTP code = %ld",
			results.curl_result, results.http_code);
	}

	return err;
}

static int probe_rpc(struct rpc_state *rpc)
{
	struct active_request_slot *slot;
	struct curl_slist *headers = NULL;
	struct strbuf buf = STRBUF_INIT;
	int err;

	slot = get_active_slot();

	headers = curl_slist_append(headers, rpc->hdr_content_type);
	headers = curl_slist_append(headers, rpc->hdr_accept);

	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(slot->curl, CURLOPT_POST, 1);
	curl_easy_setopt(slot->curl, CURLOPT_URL, rpc->service_url);
	curl_easy_setopt(slot->curl, CURLOPT_ENCODING, "");
	curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, "0000");
	curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE, 4);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buf);

	err = run_slot(slot);

	curl_slist_free_all(headers);
	strbuf_release(&buf);
	return err;
}

static int post_rpc(struct rpc_state *rpc)
{
	struct active_request_slot *slot;
	struct curl_slist *headers = NULL;
	int use_gzip = rpc->gzip_request;
	char *gzip_body = NULL;
	int err, large_request = 0;

	/* Try to load the entire request, if we can fit it into the
	 * allocated buffer space we can use HTTP/1.0 and avoid the
	 * chunked encoding mess.
	 */
	while (1) {
		size_t left = rpc->alloc - rpc->len;
		char *buf = rpc->buf + rpc->len;
		int n;

		if (left < LARGE_PACKET_MAX) {
			large_request = 1;
			use_gzip = 0;
			break;
		}

		n = packet_read_line(rpc->out, buf, left);
		if (!n)
			break;
		rpc->len += n;
	}

	if (large_request) {
		err = probe_rpc(rpc);
		if (err)
			return err;
	}

	slot = get_active_slot();

	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(slot->curl, CURLOPT_POST, 1);
	curl_easy_setopt(slot->curl, CURLOPT_URL, rpc->service_url);
	curl_easy_setopt(slot->curl, CURLOPT_ENCODING, "");

	headers = curl_slist_append(headers, rpc->hdr_content_type);
	headers = curl_slist_append(headers, rpc->hdr_accept);
	headers = curl_slist_append(headers, "Expect:");

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

	} else if (use_gzip && 1024 < rpc->len) {
		/* The client backend isn't giving us compressed data so
		 * we can try to deflate it ourselves, this may save on.
		 * the transfer time.
		 */
		size_t size;
		git_zstream stream;
		int ret;

		memset(&stream, 0, sizeof(stream));
		git_deflate_init_gzip(&stream, Z_BEST_COMPRESSION);
		size = git_deflate_bound(&stream, rpc->len);
		gzip_body = xmalloc(size);

		stream.next_in = (unsigned char *)rpc->buf;
		stream.avail_in = rpc->len;
		stream.next_out = (unsigned char *)gzip_body;
		stream.avail_out = size;

		ret = git_deflate(&stream, Z_FINISH);
		if (ret != Z_STREAM_END)
			die("cannot deflate request; zlib deflate error %d", ret);

		ret = git_deflate_end_gently(&stream);
		if (ret != Z_OK)
			die("cannot deflate request; zlib end error %d", ret);

		size = stream.total_out;

		headers = curl_slist_append(headers, "Content-Encoding: gzip");
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, gzip_body);
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE, size);

		if (options.verbosity > 1) {
			fprintf(stderr, "POST %s (gzip %lu to %lu bytes)\n",
				rpc->service_name,
				(unsigned long)rpc->len, (unsigned long)size);
			fflush(stderr);
		}
	} else {
		/* We know the complete request size in advance, use the
		 * more normal Content-Length approach.
		 */
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, rpc->buf);
		curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE, rpc->len);
		if (options.verbosity > 1) {
			fprintf(stderr, "POST %s (%lu bytes)\n",
				rpc->service_name, (unsigned long)rpc->len);
			fflush(stderr);
		}
	}

	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, rpc_in);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, rpc);

	err = run_slot(slot);

	curl_slist_free_all(headers);
	free(gzip_body);
	return err;
}

static int rpc_service(struct rpc_state *rpc, struct discovery *heads)
{
	const char *svc = rpc->service_name;
	struct strbuf buf = STRBUF_INIT;
	struct child_process client;
	int err = 0;

	memset(&client, 0, sizeof(client));
	client.in = -1;
	client.out = -1;
	client.git_cmd = 1;
	client.argv = rpc->argv;
	if (start_command(&client))
		exit(1);
	if (heads)
		write_or_die(client.in, heads->buf, heads->len);

	rpc->alloc = http_post_buffer;
	rpc->buf = xmalloc(rpc->alloc);
	rpc->in = client.in;
	rpc->out = client.out;
	strbuf_init(&rpc->result, 0);

	strbuf_addf(&buf, "%s%s", url, svc);
	rpc->service_url = strbuf_detach(&buf, NULL);

	strbuf_addf(&buf, "Content-Type: application/x-%s-request", svc);
	rpc->hdr_content_type = strbuf_detach(&buf, NULL);

	strbuf_addf(&buf, "Accept: application/x-%s-result", svc);
	rpc->hdr_accept = strbuf_detach(&buf, NULL);

	while (!err) {
		int n = packet_read_line(rpc->out, rpc->buf, rpc->alloc);
		if (!n)
			break;
		rpc->pos = 0;
		rpc->len = n;
		err |= post_rpc(rpc);
	}

	close(client.in);
	client.in = -1;
	strbuf_read(&rpc->result, client.out, 0);

	close(client.out);
	client.out = -1;

	err |= finish_command(&client);
	free(rpc->service_url);
	free(rpc->hdr_content_type);
	free(rpc->hdr_accept);
	free(rpc->buf);
	strbuf_release(&buf);
	return err;
}

static int fetch_dumb(int nr_heads, struct ref **to_fetch)
{
	struct walker *walker;
	char **targets = xmalloc(nr_heads * sizeof(char*));
	int ret, i;

	if (options.depth)
		die("dumb http transport does not support --depth");
	for (i = 0; i < nr_heads; i++)
		targets[i] = xstrdup(sha1_to_hex(to_fetch[i]->old_sha1));

	walker = get_http_walker(url);
	walker->get_all = 1;
	walker->get_tree = 1;
	walker->get_history = 1;
	walker->get_verbosely = options.verbosity >= 3;
	walker->get_recover = 0;
	ret = walker_fetch(walker, nr_heads, targets, NULL, NULL);
	walker_free(walker);

	for (i = 0; i < nr_heads; i++)
		free(targets[i]);
	free(targets);

	return ret ? error("Fetch failed.") : 0;
}

static int fetch_git(struct discovery *heads,
	int nr_heads, struct ref **to_fetch)
{
	struct rpc_state rpc;
	char *depth_arg = NULL;
	const char **argv;
	int argc = 0, i, err;

	argv = xmalloc((15 + nr_heads) * sizeof(char*));
	argv[argc++] = "fetch-pack";
	argv[argc++] = "--stateless-rpc";
	argv[argc++] = "--lock-pack";
	if (options.followtags)
		argv[argc++] = "--include-tag";
	if (options.thin)
		argv[argc++] = "--thin";
	if (options.verbosity >= 3) {
		argv[argc++] = "-v";
		argv[argc++] = "-v";
	}
	if (!options.progress)
		argv[argc++] = "--no-progress";
	if (options.depth) {
		struct strbuf buf = STRBUF_INIT;
		strbuf_addf(&buf, "--depth=%lu", options.depth);
		depth_arg = strbuf_detach(&buf, NULL);
		argv[argc++] = depth_arg;
	}
	argv[argc++] = url;
	for (i = 0; i < nr_heads; i++) {
		struct ref *ref = to_fetch[i];
		if (!ref->name || !*ref->name)
			die("cannot fetch by sha1 over smart http");
		argv[argc++] = ref->name;
	}
	argv[argc++] = NULL;

	memset(&rpc, 0, sizeof(rpc));
	rpc.service_name = "git-upload-pack",
	rpc.argv = argv;
	rpc.gzip_request = 1;

	err = rpc_service(&rpc, heads);
	if (rpc.result.len)
		safe_write(1, rpc.result.buf, rpc.result.len);
	strbuf_release(&rpc.result);
	free(argv);
	free(depth_arg);
	return err;
}

static int fetch(int nr_heads, struct ref **to_fetch)
{
	struct discovery *d = discover_refs("git-upload-pack");
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
		if (!prefixcmp(buf->buf, "fetch ")) {
			char *p = buf->buf + strlen("fetch ");
			char *name;
			struct ref *ref;
			unsigned char old_sha1[20];

			if (strlen(p) < 40 || get_sha1_hex(p, old_sha1))
				die("protocol error: expected sha/ref, got %s'", p);
			if (p[40] == ' ')
				name = p + 41;
			else if (!p[40])
				name = "";
			else
				die("protocol error: expected sha/ref, got %s'", p);

			ref = alloc_ref(name);
			hashcpy(ref->old_sha1, old_sha1);

			*list = ref;
			list = &ref->next;

			ALLOC_GROW(to_fetch, nr_heads + 1, alloc_heads);
			to_fetch[nr_heads++] = ref;
		}
		else
			die("http transport does not support %s", buf->buf);

		strbuf_reset(buf);
		if (strbuf_getline(buf, stdin, '\n') == EOF)
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
	const char **argv = xmalloc((10 + nr_spec) * sizeof(char*));
	int argc = 0, i;

	argv[argc++] = "http-push";
	argv[argc++] = "--helper-status";
	if (options.dry_run)
		argv[argc++] = "--dry-run";
	if (options.verbosity > 1)
		argv[argc++] = "--verbose";
	argv[argc++] = url;
	for (i = 0; i < nr_spec; i++)
		argv[argc++] = specs[i];
	argv[argc++] = NULL;

	if (run_command_v_opt(argv, RUN_GIT_CMD))
		die("git-%s failed", argv[0]);
	free(argv);
	return 0;
}

static int push_git(struct discovery *heads, int nr_spec, char **specs)
{
	struct rpc_state rpc;
	const char **argv;
	int argc = 0, i, err;

	argv = xmalloc((10 + nr_spec) * sizeof(char*));
	argv[argc++] = "send-pack";
	argv[argc++] = "--stateless-rpc";
	argv[argc++] = "--helper-status";
	if (options.thin)
		argv[argc++] = "--thin";
	if (options.dry_run)
		argv[argc++] = "--dry-run";
	if (options.verbosity < 0)
		argv[argc++] = "--quiet";
	else if (options.verbosity > 1)
		argv[argc++] = "--verbose";
	argv[argc++] = url;
	for (i = 0; i < nr_spec; i++)
		argv[argc++] = specs[i];
	argv[argc++] = NULL;

	memset(&rpc, 0, sizeof(rpc));
	rpc.service_name = "git-receive-pack",
	rpc.argv = argv;

	err = rpc_service(&rpc, heads);
	if (rpc.result.len)
		safe_write(1, rpc.result.buf, rpc.result.len);
	strbuf_release(&rpc.result);
	free(argv);
	return err;
}

static int push(int nr_spec, char **specs)
{
	struct discovery *heads = discover_refs("git-receive-pack");
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
	int alloc_spec = 0, nr_spec = 0, i;

	do {
		if (!prefixcmp(buf->buf, "push ")) {
			ALLOC_GROW(specs, nr_spec + 1, alloc_spec);
			specs[nr_spec++] = xstrdup(buf->buf + 5);
		}
		else
			die("http transport does not support %s", buf->buf);

		strbuf_reset(buf);
		if (strbuf_getline(buf, stdin, '\n') == EOF)
			goto free_specs;
		if (!*buf->buf)
			break;
	} while (1);

	if (push(nr_spec, specs))
		exit(128); /* error already reported */

	printf("\n");
	fflush(stdout);

 free_specs:
	for (i = 0; i < nr_spec; i++)
		free(specs[i]);
	free(specs);
}

int main(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;
	int nongit;

	git_extract_argv0_path(argv[0]);
	setup_git_directory_gently(&nongit);
	if (argc < 2) {
		fprintf(stderr, "Remote needed\n");
		return 1;
	}

	options.verbosity = 1;
	options.progress = !!isatty(2);
	options.thin = 1;

	remote = remote_get(argv[1]);

	if (argc > 2) {
		end_url_with_slash(&buf, argv[2]);
	} else {
		end_url_with_slash(&buf, remote->url[0]);
	}

	url = strbuf_detach(&buf, NULL);

	http_init(remote);

	do {
		if (strbuf_getline(&buf, stdin, '\n') == EOF) {
			if (ferror(stdin))
				fprintf(stderr, "Error reading command stream\n");
			else
				fprintf(stderr, "Unexpected end of command stream\n");
			return 1;
		}
		if (buf.len == 0)
			break;
		if (!prefixcmp(buf.buf, "fetch ")) {
			if (nongit)
				die("Fetch attempted without a local repo");
			parse_fetch(&buf);

		} else if (!strcmp(buf.buf, "list") || !prefixcmp(buf.buf, "list ")) {
			int for_push = !!strstr(buf.buf + 4, "for-push");
			output_refs(get_refs(for_push));

		} else if (!prefixcmp(buf.buf, "push ")) {
			parse_push(&buf);

		} else if (!prefixcmp(buf.buf, "option ")) {
			char *name = buf.buf + strlen("option ");
			char *value = strchr(name, ' ');
			int result;

			if (value)
				*value++ = '\0';
			else
				value = "true";

			result = set_option(name, value);
			if (!result)
				printf("ok\n");
			else if (result < 0)
				printf("error invalid value\n");
			else
				printf("unsupported\n");
			fflush(stdout);

		} else if (!strcmp(buf.buf, "capabilities")) {
			printf("fetch\n");
			printf("option\n");
			printf("push\n");
			printf("\n");
			fflush(stdout);
		} else {
			fprintf(stderr, "Unknown command '%s'\n", buf.buf);
			return 1;
		}
		strbuf_reset(&buf);
	} while (1);

	http_cleanup();

	return 0;
}
