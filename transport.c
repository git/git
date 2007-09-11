#include "cache.h"
#include "transport.h"
#include "run-command.h"
#include "http.h"
#include "pkt-line.h"
#include "fetch-pack.h"
#include "walker.h"

/* Generic functions for using commit walkers */

static int fetch_objs_via_walker(const struct transport *transport,
				 int nr_objs, char **objs)
{
	char *dest = xstrdup(transport->url);
	struct walker *walker = transport->data;

	walker->get_all = 1;
	walker->get_tree = 1;
	walker->get_history = 1;
	walker->get_verbosely = transport->verbose;
	walker->get_recover = 0;

	if (walker_fetch(walker, nr_objs, objs, NULL, dest))
		die("Fetch failed.");

	free(dest);
	return 0;
}

static int disconnect_walker(struct transport *transport)
{
	struct walker *walker = transport->data;
	if (walker)
		walker_free(walker);
	return 0;
}

static const struct transport_ops rsync_transport;

static int curl_transport_push(struct transport *transport, int refspec_nr, const char **refspec, int flags) {
	const char **argv;
	int argc;
	int err;

	argv = xmalloc((refspec_nr + 11) * sizeof(char *));
	argv[0] = "http-push";
	argc = 1;
	if (flags & TRANSPORT_PUSH_ALL)
		argv[argc++] = "--all";
	if (flags & TRANSPORT_PUSH_FORCE)
		argv[argc++] = "--force";
	argv[argc++] = transport->url;
	while (refspec_nr--)
		argv[argc++] = *refspec++;
	argv[argc] = NULL;
	err = run_command_v_opt(argv, RUN_GIT_CMD);
	switch (err) {
	case -ERR_RUN_COMMAND_FORK:
		error("unable to fork for %s", argv[0]);
	case -ERR_RUN_COMMAND_EXEC:
		error("unable to exec %s", argv[0]);
		break;
	case -ERR_RUN_COMMAND_WAITPID:
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		error("%s died with strange error", argv[0]);
	}
	return !!err;
}

#ifndef NO_CURL
static int missing__target(int code, int result)
{
	return	/* file:// URL -- do we ever use one??? */
		(result == CURLE_FILE_COULDNT_READ_FILE) ||
		/* http:// and https:// URL */
		(code == 404 && result == CURLE_HTTP_RETURNED_ERROR) ||
		/* ftp:// URL */
		(code == 550 && result == CURLE_FTP_COULDNT_RETR_FILE)
		;
}

#define missing_target(a) missing__target((a)->http_code, (a)->curl_result)

static struct ref *get_refs_via_curl(const struct transport *transport)
{
	struct buffer buffer;
	char *data, *start, *mid;
	char *ref_name;
	char *refs_url;
	int i = 0;

	struct active_request_slot *slot;
	struct slot_results results;

	struct ref *refs = NULL;
	struct ref *ref = NULL;
	struct ref *last_ref = NULL;

	data = xmalloc(4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	refs_url = xmalloc(strlen(transport->url) + 11);
	sprintf(refs_url, "%s/info/refs", transport->url);

	http_init();

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, refs_url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			if (missing_target(&results)) {
				free(buffer.buffer);
				return NULL;
			} else {
				free(buffer.buffer);
				error("%s", curl_errorstr);
				return NULL;
			}
		}
	} else {
		free(buffer.buffer);
		error("Unable to start request");
		return NULL;
	}

	http_cleanup();

	data = buffer.buffer;
	start = NULL;
	mid = data;
	while (i < buffer.posn) {
		if (!start)
			start = &data[i];
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

	free(buffer.buffer);

	return refs;
}

#else

static struct ref *get_refs_via_curl(const struct transport *transport)
{
	die("Cannot fetch from '%s' without curl ...", transport->url);
	return NULL;
}

#endif

static const struct transport_ops curl_transport = {
	/* set_option */	NULL,
	/* get_refs_list */	get_refs_via_curl,
	/* fetch_refs */	NULL,
	/* fetch_objs */	fetch_objs_via_walker,
	/* push */		curl_transport_push,
	/* disconnect */	disconnect_walker
};

static const struct transport_ops bundle_transport = {
};

struct git_transport_data {
	unsigned thin : 1;
	unsigned keep : 1;

	int unpacklimit;

	int depth;

	const char *uploadpack;
	const char *receivepack;
};

static int set_git_option(struct transport *connection,
			  const char *name, const char *value)
{
	struct git_transport_data *data = connection->data;
	if (!strcmp(name, TRANS_OPT_UPLOADPACK)) {
		data->uploadpack = value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_RECEIVEPACK)) {
		data->receivepack = value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_THIN)) {
		data->thin = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_KEEP)) {
		data->keep = !!value;
		return 0;
	} else if (!strcmp(name, TRANS_OPT_UNPACKLIMIT)) {
		data->unpacklimit = atoi(value);
		return 0;
	} else if (!strcmp(name, TRANS_OPT_DEPTH)) {
		if (!value)
			data->depth = 0;
		else
			data->depth = atoi(value);
		return 0;
	}
	return 1;
}

static struct ref *get_refs_via_connect(const struct transport *transport)
{
	struct git_transport_data *data = transport->data;
	struct ref *refs;
	int fd[2];
	pid_t pid;
	char *dest = xstrdup(transport->url);

	pid = git_connect(fd, dest, data->uploadpack, 0);

	if (pid < 0)
		die("Failed to connect to \"%s\"", transport->url);

	get_remote_heads(fd[0], &refs, 0, NULL, 0);
	packet_flush(fd[1]);

	finish_connect(pid);

	free(dest);

	return refs;
}

static int fetch_refs_via_pack(const struct transport *transport,
			       int nr_heads, char **heads)
{
	struct git_transport_data *data = transport->data;
	struct ref *refs;
	char *dest = xstrdup(transport->url);
	struct fetch_pack_args args;

	args.uploadpack = data->uploadpack;
	args.quiet = 0;
	args.keep_pack = data->keep;
	args.unpacklimit = data->unpacklimit;
	args.use_thin_pack = data->thin;
	args.fetch_all = 0;
	args.verbose = transport->verbose;
	args.depth = data->depth;
	args.no_progress = 0;

	setup_fetch_pack(&args);

	refs = fetch_pack(dest, nr_heads, heads);

	// ???? check that refs got everything?

	/* free the memory used for the refs list ... */

	free_refs(refs);

	free(dest);
	return 0;
}

static int git_transport_push(struct transport *transport, int refspec_nr, const char **refspec, int flags) {
	struct git_transport_data *data = transport->data;
	const char **argv;
	char *rem;
	int argc;
	int err;

	argv = xmalloc((refspec_nr + 11) * sizeof(char *));
	argv[0] = "send-pack";
	argc = 1;
	if (flags & TRANSPORT_PUSH_ALL)
		argv[argc++] = "--all";
	if (flags & TRANSPORT_PUSH_FORCE)
		argv[argc++] = "--force";
	if (data->receivepack) {
		char *rp = xmalloc(strlen(data->receivepack) + 16);
		sprintf(rp, "--receive-pack=%s", data->receivepack);
		argv[argc++] = rp;
	}
	if (data->thin)
		argv[argc++] = "--thin";
	rem = xmalloc(strlen(transport->remote->name) + 10);
	sprintf(rem, "--remote=%s", transport->remote->name);
	argv[argc++] = rem;
	argv[argc++] = transport->url;
	while (refspec_nr--)
		argv[argc++] = *refspec++;
	argv[argc] = NULL;
	err = run_command_v_opt(argv, RUN_GIT_CMD);
	switch (err) {
	case -ERR_RUN_COMMAND_FORK:
		error("unable to fork for %s", argv[0]);
	case -ERR_RUN_COMMAND_EXEC:
		error("unable to exec %s", argv[0]);
		break;
	case -ERR_RUN_COMMAND_WAITPID:
	case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
	case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
	case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
		error("%s died with strange error", argv[0]);
	}
	return !!err;
}

static const struct transport_ops git_transport = {
	/* set_option */	set_git_option,
	/* get_refs_list */	get_refs_via_connect,
	/* fetch_refs */	fetch_refs_via_pack,
	/* fetch_objs */	NULL,
	/* push */		git_transport_push
};

static int is_local(const char *url)
{
	const char *colon = strchr(url, ':');
	const char *slash = strchr(url, '/');
	return !colon || (slash && slash < colon);
}

static int is_file(const char *url)
{
	struct stat buf;
	if (stat(url, &buf))
		return 0;
	return S_ISREG(buf.st_mode);
}

struct transport *transport_get(struct remote *remote, const char *url,
				int fetch)
{
	struct transport *ret = NULL;
	if (!prefixcmp(url, "rsync://")) {
		ret = xmalloc(sizeof(*ret));
		ret->data = NULL;
		ret->ops = &rsync_transport;
	} else if (!prefixcmp(url, "http://") || !prefixcmp(url, "https://") ||
		   !prefixcmp(url, "ftp://")) {
		ret = xmalloc(sizeof(*ret));
		ret->ops = &curl_transport;
		if (fetch)
			ret->data = get_http_walker(url);
		else
			ret->data = NULL;
	} else if (is_local(url) && is_file(url)) {
		ret = xmalloc(sizeof(*ret));
		ret->data = NULL;
		ret->ops = &bundle_transport;
	} else {
		struct git_transport_data *data = xcalloc(1, sizeof(*data));
		ret = xcalloc(1, sizeof(*ret));
		ret->data = data;
		data->thin = 1;
		data->uploadpack = "git-upload-pack";
		if (remote && remote->uploadpack)
			data->uploadpack = remote->uploadpack;
		data->receivepack = "git-receive-pack";
		if (remote && remote->receivepack)
			data->receivepack = remote->receivepack;
		data->unpacklimit = -1;
		ret->ops = &git_transport;
	}
	if (ret) {
		ret->remote = remote;
		ret->url = url;
		ret->remote_refs = NULL;
		ret->fetch = !!fetch;
	}
	return ret;
}

int transport_set_option(struct transport *transport,
			 const char *name, const char *value)
{
	int ret = 1;
	if (transport->ops->set_option)
		ret = transport->ops->set_option(transport, name, value);
	if (ret < 0)
		fprintf(stderr, "For '%s' option %s cannot be set to '%s'\n",
			transport->url, name, value);
	if (ret > 0)
		fprintf(stderr, "For '%s' option %s is ignored\n",
			transport->url, name);
	return ret;
}

int transport_push(struct transport *transport,
		   int refspec_nr, const char **refspec, int flags)
{
	if (!transport->ops->push)
		return 1;
	return transport->ops->push(transport, refspec_nr, refspec, flags);
}

struct ref *transport_get_remote_refs(struct transport *transport)
{
	if (!transport->remote_refs)
		transport->remote_refs =
			transport->ops->get_refs_list(transport);
	return transport->remote_refs;
}

#define PACK_HEADS_CHUNK_COUNT 256

int transport_fetch_refs(struct transport *transport, struct ref *refs)
{
	int i;
	int nr_heads = 0;
	char **heads = xmalloc(PACK_HEADS_CHUNK_COUNT * sizeof(char *));
	struct ref *rm;
	int use_objs = !transport->ops->fetch_refs;

	for (rm = refs; rm; rm = rm->next) {
		if (rm->peer_ref &&
		    !hashcmp(rm->peer_ref->old_sha1, rm->old_sha1))
			continue;
		if (use_objs) {
			heads[nr_heads++] = xstrdup(sha1_to_hex(rm->old_sha1));
		} else {
			heads[nr_heads++] = xstrdup(rm->name);
		}
		if (nr_heads % PACK_HEADS_CHUNK_COUNT == 0)
			heads = xrealloc(heads,
					 (nr_heads + PACK_HEADS_CHUNK_COUNT) *
					 sizeof(char *));
	}

	if (use_objs) {
		if (transport->ops->fetch_objs(transport, nr_heads, heads))
			return -1;
	} else {
		if (transport->ops->fetch_refs(transport, nr_heads, heads))
			return -1;
	}

	/* free the memory used for the heads list ... */
	for (i = 0; i < nr_heads; i++)
		free(heads[i]);
	free(heads);
	return 0;
}

int transport_disconnect(struct transport *transport)
{
	int ret = 0;
	if (transport->ops->disconnect)
		ret = transport->ops->disconnect(transport);
	free(transport);
	return ret;
}
