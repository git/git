#include "cache.h"
#include "remote.h"
#include "strbuf.h"
#include "walker.h"
#include "http.h"
#include "exec_cmd.h"
#include "run-command.h"
#include "pkt-line.h"

static struct remote *remote;
static const char *url;
static struct walker *walker;

struct options {
	int verbosity;
	unsigned long depth;
	unsigned progress : 1,
		followtags : 1,
		dry_run : 1;
};
static struct options options;

static void init_walker(void)
{
	if (!walker)
		walker = get_http_walker(url, remote);
}

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
		return 1 /* TODO implement later */;
	}
	else if (!strcmp(name, "depth")) {
		char *end;
		unsigned long v = strtoul(value, &end, 10);
		if (value == end || *end)
			return -1;
		options.depth = v;
		return 1 /* TODO implement later */;
	}
	else if (!strcmp(name, "followtags")) {
		if (!strcmp(value, "true"))
			options.followtags = 1;
		else if (!strcmp(value, "false"))
			options.followtags = 0;
		else
			return -1;
		return 1 /* TODO implement later */;
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
	int http_ret, is_http = 0;

	if (last && !strcmp(service, last->service))
		return last;
	free_discovery(last);

	strbuf_addf(&buffer, "%s/info/refs", url);
	if (!prefixcmp(url, "http://") || !prefixcmp(url, "https://")) {
		is_http = 1;
		if (!strchr(url, '?'))
			strbuf_addch(&buffer, '?');
		else
			strbuf_addch(&buffer, '&');
		strbuf_addf(&buffer, "service=%s", service);
	}
	refs_url = strbuf_detach(&buffer, NULL);

	init_walker();
	http_ret = http_get_strbuf(refs_url, &buffer, HTTP_NO_CACHE);
	switch (http_ret) {
	case HTTP_OK:
		break;
	case HTTP_MISSING_TARGET:
		die("%s not found: did you run git update-server-info on the"
		    " server?", refs_url);
	default:
		http_error(refs_url, http_ret);
		die("HTTP request failed");
	}

	last= xcalloc(1, sizeof(*last_discovery));
	last->service = service;
	last->buf_alloc = strbuf_detach(&buffer, &last->len);
	last->buf = last->buf_alloc;

	if (is_http && 5 <= last->len && last->buf[4] == '#') {
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

static int write_discovery(int fd, void *data)
{
	struct discovery *heads = data;
	int err = 0;
	if (write_in_full(fd, heads->buf, heads->len) != heads->len)
		err = 1;
	close(fd);
	return err;
}

static struct ref *parse_git_refs(struct discovery *heads)
{
	struct ref *list = NULL;
	struct async async;

	memset(&async, 0, sizeof(async));
	async.proc = write_discovery;
	async.data = heads;

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

	init_walker();
	ref = alloc_ref("HEAD");
	if (!walker->fetch_ref(walker, ref) &&
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

static int fetch_dumb(int nr_heads, struct ref **to_fetch)
{
	char **targets = xmalloc(nr_heads * sizeof(char*));
	int ret, i;

	for (i = 0; i < nr_heads; i++)
		targets[i] = xstrdup(sha1_to_hex(to_fetch[i]->old_sha1));

	init_walker();
	walker->get_all = 1;
	walker->get_tree = 1;
	walker->get_history = 1;
	walker->get_verbosely = options.verbosity >= 3;
	walker->get_recover = 0;
	ret = walker_fetch(walker, nr_heads, targets, NULL, NULL);

	for (i = 0; i < nr_heads; i++)
		free(targets[i]);
	free(targets);

	return ret ? error("Fetch failed.") : 0;
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

	if (fetch_dumb(nr_heads, to_fetch))
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
			return;
		if (!*buf->buf)
			break;
	} while (1);

	if (push_dav(nr_spec, specs))
		exit(128); /* error already reported */
	for (i = 0; i < nr_spec; i++)
		free(specs[i]);
	free(specs);

	printf("\n");
	fflush(stdout);
}

int main(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;

	git_extract_argv0_path(argv[0]);
	setup_git_directory();
	if (argc < 2) {
		fprintf(stderr, "Remote needed\n");
		return 1;
	}

	options.verbosity = 1;
	options.progress = !!isatty(2);

	remote = remote_get(argv[1]);

	if (argc > 2) {
		url = argv[2];
	} else {
		url = remote->url[0];
	}

	do {
		if (strbuf_getline(&buf, stdin, '\n') == EOF)
			break;
		if (!prefixcmp(buf.buf, "fetch ")) {
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
			return 1;
		}
		strbuf_reset(&buf);
	} while (1);
	return 0;
}
