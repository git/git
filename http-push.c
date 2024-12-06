#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "environment.h"
#include "hex.h"
#include "repository.h"
#include "commit.h"
#include "tag.h"
#include "blob.h"
#include "http.h"
#include "diff.h"
#include "revision.h"
#include "remote.h"
#include "list-objects.h"
#include "setup.h"
#include "sigchain.h"
#include "strvec.h"
#include "tree.h"
#include "tree-walk.h"
#include "url.h"
#include "packfile.h"
#include "object-store-ll.h"
#include "commit-reach.h"

#ifdef EXPAT_NEEDS_XMLPARSE_H
#include <xmlparse.h>
#else
#include <expat.h>
#endif

static const char http_push_usage[] =
"git http-push [--all] [--dry-run] [--force] [--verbose] <remote> [<head>...]\n";

#ifndef XML_STATUS_OK
enum XML_Status {
  XML_STATUS_OK = 1,
  XML_STATUS_ERROR = 0
};
#define XML_STATUS_OK    1
#define XML_STATUS_ERROR 0
#endif

#define PREV_BUF_SIZE 4096

/* DAV methods */
#define DAV_LOCK "LOCK"
#define DAV_MKCOL "MKCOL"
#define DAV_MOVE "MOVE"
#define DAV_PROPFIND "PROPFIND"
#define DAV_PUT "PUT"
#define DAV_UNLOCK "UNLOCK"
#define DAV_DELETE "DELETE"

/* DAV lock flags */
#define DAV_PROP_LOCKWR (1u << 0)
#define DAV_PROP_LOCKEX (1u << 1)
#define DAV_LOCK_OK (1u << 2)

/* DAV XML properties */
#define DAV_CTX_LOCKENTRY ".multistatus.response.propstat.prop.supportedlock.lockentry"
#define DAV_CTX_LOCKTYPE_WRITE ".multistatus.response.propstat.prop.supportedlock.lockentry.locktype.write"
#define DAV_CTX_LOCKTYPE_EXCLUSIVE ".multistatus.response.propstat.prop.supportedlock.lockentry.lockscope.exclusive"
#define DAV_ACTIVELOCK_OWNER ".prop.lockdiscovery.activelock.owner.href"
#define DAV_ACTIVELOCK_TIMEOUT ".prop.lockdiscovery.activelock.timeout"
#define DAV_ACTIVELOCK_TOKEN ".prop.lockdiscovery.activelock.locktoken.href"
#define DAV_PROPFIND_RESP ".multistatus.response"
#define DAV_PROPFIND_NAME ".multistatus.response.href"
#define DAV_PROPFIND_COLLECTION ".multistatus.response.propstat.prop.resourcetype.collection"

/* DAV request body templates */
#define PROPFIND_SUPPORTEDLOCK_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:propfind xmlns:D=\"DAV:\">\n<D:prop xmlns:R=\"%s\">\n<D:supportedlock/>\n</D:prop>\n</D:propfind>"
#define PROPFIND_ALL_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:propfind xmlns:D=\"DAV:\">\n<D:allprop/>\n</D:propfind>"
#define LOCK_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:lockinfo xmlns:D=\"DAV:\">\n<D:lockscope><D:exclusive/></D:lockscope>\n<D:locktype><D:write/></D:locktype>\n<D:owner>\n<D:href>mailto:%s</D:href>\n</D:owner>\n</D:lockinfo>"

#define LOCK_TIME 600
#define LOCK_REFRESH 30

/* Remember to update object flag allocation in object.h */
#define LOCAL    (1u<<11)
#define REMOTE   (1u<<12)
#define FETCHING (1u<<13)
#define PUSHING  (1u<<14)

/* We allow "recursive" symbolic refs. Only within reason, though */
#define MAXDEPTH 5

static int pushing;
static int aborted;
static signed char remote_dir_exists[256];

static int push_verbosely;
static int push_all = MATCH_REFS_NONE;
static int force_all;
static int dry_run;
static int helper_status;

static struct object_list *objects;

struct repo {
	char *url;
	char *path;
	int path_len;
	int has_info_refs;
	int can_update_info_refs;
	int has_info_packs;
	struct packed_git *packs;
	struct remote_lock *locks;
};

static struct repo *repo;

enum transfer_state {
	NEED_FETCH,
	RUN_FETCH_LOOSE,
	RUN_FETCH_PACKED,
	NEED_PUSH,
	RUN_MKCOL,
	RUN_PUT,
	RUN_MOVE,
	ABORTED,
	COMPLETE
};

struct transfer_request {
	struct object *obj;
	struct packed_git *target;
	char *url;
	char *dest;
	struct remote_lock *lock;
	struct curl_slist *headers;
	struct buffer buffer;
	enum transfer_state state;
	CURLcode curl_result;
	char errorstr[CURL_ERROR_SIZE];
	long http_code;
	void *userData;
	struct active_request_slot *slot;
	struct transfer_request *next;
};

static struct transfer_request *request_queue_head;

struct xml_ctx {
	char *name;
	int len;
	char *cdata;
	void (*userFunc)(struct xml_ctx *ctx, int tag_closed);
	void *userData;
};

struct remote_lock {
	char *url;
	char *owner;
	char *token;
	char tmpfile_suffix[GIT_MAX_HEXSZ + 1];
	time_t start_time;
	long timeout;
	int refreshing;
	struct remote_lock *next;
};

/* Flags that control remote_ls processing */
#define PROCESS_FILES (1u << 0)
#define PROCESS_DIRS  (1u << 1)
#define RECURSIVE     (1u << 2)

/* Flags that remote_ls passes to callback functions */
#define IS_DIR (1u << 0)

struct remote_ls_ctx {
	char *path;
	void (*userFunc)(struct remote_ls_ctx *ls);
	void *userData;
	int flags;
	char *dentry_name;
	int dentry_flags;
	struct remote_ls_ctx *parent;
};

/* get_dav_token_headers options */
enum dav_header_flag {
	DAV_HEADER_IF = (1u << 0),
	DAV_HEADER_LOCK = (1u << 1),
	DAV_HEADER_TIMEOUT = (1u << 2)
};

static char *xml_entities(const char *s)
{
	struct strbuf buf = STRBUF_INIT;
	strbuf_addstr_xml_quoted(&buf, s);
	return strbuf_detach(&buf, NULL);
}

static void curl_setup_http_get(CURL *curl, const char *url,
		const char *custom_req)
{
	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, custom_req);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite_null);
}

static void curl_setup_http(CURL *curl, const char *url,
		const char *custom_req, struct buffer *buffer,
		curl_write_callback write_fn)
{
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_INFILE, buffer);
	curl_easy_setopt(curl, CURLOPT_INFILESIZE, buffer->buf.len);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, seek_buffer);
	curl_easy_setopt(curl, CURLOPT_SEEKDATA, buffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_fn);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, custom_req);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);
}

static struct curl_slist *get_dav_token_headers(struct remote_lock *lock, enum dav_header_flag options)
{
	struct strbuf buf = STRBUF_INIT;
	struct curl_slist *dav_headers = http_copy_default_headers();

	if (options & DAV_HEADER_IF) {
		strbuf_addf(&buf, "If: (<%s>)", lock->token);
		dav_headers = curl_slist_append(dav_headers, buf.buf);
		strbuf_reset(&buf);
	}
	if (options & DAV_HEADER_LOCK) {
		strbuf_addf(&buf, "Lock-Token: <%s>", lock->token);
		dav_headers = curl_slist_append(dav_headers, buf.buf);
		strbuf_reset(&buf);
	}
	if (options & DAV_HEADER_TIMEOUT) {
		strbuf_addf(&buf, "Timeout: Second-%ld", lock->timeout);
		dav_headers = curl_slist_append(dav_headers, buf.buf);
		strbuf_reset(&buf);
	}
	strbuf_release(&buf);

	return dav_headers;
}

static void finish_request(struct transfer_request *request);
static void release_request(struct transfer_request *request);

static void process_response(void *callback_data)
{
	struct transfer_request *request =
		(struct transfer_request *)callback_data;

	finish_request(request);
}

static void start_fetch_loose(struct transfer_request *request)
{
	struct active_request_slot *slot;
	struct http_object_request *obj_req;

	obj_req = new_http_object_request(repo->url, &request->obj->oid);
	if (!obj_req) {
		request->state = ABORTED;
		return;
	}

	slot = obj_req->slot;
	slot->callback_func = process_response;
	slot->callback_data = request;
	request->slot = slot;
	request->userData = obj_req;

	/* Try to get the request started, abort the request on error */
	request->state = RUN_FETCH_LOOSE;
	if (!start_active_slot(slot)) {
		fprintf(stderr, "Unable to start GET request\n");
		repo->can_update_info_refs = 0;
		release_http_object_request(&obj_req);
		release_request(request);
	}
}

static void start_mkcol(struct transfer_request *request)
{
	char *hex = oid_to_hex(&request->obj->oid);
	struct active_request_slot *slot;

	request->url = get_remote_object_url(repo->url, hex, 1);

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_setup_http_get(slot->curl, request->url, DAV_MKCOL);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, request->errorstr);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_MKCOL;
	} else {
		request->state = ABORTED;
		FREE_AND_NULL(request->url);
	}
}

static void start_fetch_packed(struct transfer_request *request)
{
	struct packed_git *target;

	struct transfer_request *check_request = request_queue_head;
	struct http_pack_request *preq;

	target = find_oid_pack(&request->obj->oid, repo->packs);
	if (!target) {
		fprintf(stderr, "Unable to fetch %s, will not be able to update server info refs\n", oid_to_hex(&request->obj->oid));
		repo->can_update_info_refs = 0;
		release_request(request);
		return;
	}
	close_pack_index(target);
	request->target = target;

	fprintf(stderr,	"Fetching pack %s\n",
		hash_to_hex(target->hash));
	fprintf(stderr, " which contains %s\n", oid_to_hex(&request->obj->oid));

	preq = new_http_pack_request(target->hash, repo->url);
	if (!preq) {
		repo->can_update_info_refs = 0;
		return;
	}

	/* Make sure there isn't another open request for this pack */
	while (check_request) {
		if (check_request->state == RUN_FETCH_PACKED &&
		    !strcmp(check_request->url, preq->url)) {
			release_http_pack_request(preq);
			release_request(request);
			return;
		}
		check_request = check_request->next;
	}

	preq->slot->callback_func = process_response;
	preq->slot->callback_data = request;
	request->slot = preq->slot;
	request->userData = preq;

	/* Try to get the request started, abort the request on error */
	request->state = RUN_FETCH_PACKED;
	if (!start_active_slot(preq->slot)) {
		fprintf(stderr, "Unable to start GET request\n");
		release_http_pack_request(preq);
		repo->can_update_info_refs = 0;
		release_request(request);
	}
}

static void start_put(struct transfer_request *request)
{
	char *hex = oid_to_hex(&request->obj->oid);
	struct active_request_slot *slot;
	struct strbuf buf = STRBUF_INIT;
	enum object_type type;
	char hdr[50];
	void *unpacked;
	unsigned long len;
	int hdrlen;
	ssize_t size;
	git_zstream stream;

	unpacked = repo_read_object_file(the_repository, &request->obj->oid,
					 &type, &len);
	hdrlen = format_object_header(hdr, sizeof(hdr), type, len);

	/* Set it up */
	git_deflate_init(&stream, zlib_compression_level);
	size = git_deflate_bound(&stream, len + hdrlen);
	strbuf_grow(&request->buffer.buf, size);
	request->buffer.posn = 0;

	/* Compress it */
	stream.next_out = (unsigned char *)request->buffer.buf.buf;
	stream.avail_out = size;

	/* First header.. */
	stream.next_in = (void *)hdr;
	stream.avail_in = hdrlen;
	while (git_deflate(&stream, 0) == Z_OK)
		; /* nothing */

	/* Then the data itself.. */
	stream.next_in = unpacked;
	stream.avail_in = len;
	while (git_deflate(&stream, Z_FINISH) == Z_OK)
		; /* nothing */
	git_deflate_end(&stream);
	free(unpacked);

	request->buffer.buf.len = stream.total_out;

	strbuf_addstr(&buf, "Destination: ");
	append_remote_object_url(&buf, repo->url, hex, 0);
	request->dest = strbuf_detach(&buf, NULL);

	append_remote_object_url(&buf, repo->url, hex, 0);
	strbuf_add(&buf, request->lock->tmpfile_suffix, the_hash_algo->hexsz + 1);
	request->url = strbuf_detach(&buf, NULL);

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_setup_http(slot->curl, request->url, DAV_PUT,
			&request->buffer, fwrite_null);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_PUT;
	} else {
		request->state = ABORTED;
		FREE_AND_NULL(request->url);
	}
}

static void start_move(struct transfer_request *request)
{
	struct active_request_slot *slot;
	struct curl_slist *dav_headers = http_copy_default_headers();

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_setup_http_get(slot->curl, request->url, DAV_MOVE);
	dav_headers = curl_slist_append(dav_headers, request->dest);
	dav_headers = curl_slist_append(dav_headers, "Overwrite: T");
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_MOVE;
		request->headers = dav_headers;
	} else {
		request->state = ABORTED;
		FREE_AND_NULL(request->url);
		curl_slist_free_all(dav_headers);
	}
}

static int refresh_lock(struct remote_lock *lock)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct curl_slist *dav_headers;
	int rc = 0;

	lock->refreshing = 1;

	dav_headers = get_dav_token_headers(lock, DAV_HEADER_IF | DAV_HEADER_TIMEOUT);

	slot = get_active_slot();
	slot->results = &results;
	curl_setup_http_get(slot->curl, lock->url, DAV_LOCK);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result != CURLE_OK) {
			fprintf(stderr, "LOCK HTTP error %ld\n",
				results.http_code);
		} else {
			lock->start_time = time(NULL);
			rc = 1;
		}
	}

	lock->refreshing = 0;
	curl_slist_free_all(dav_headers);

	return rc;
}

static void check_locks(void)
{
	struct remote_lock *lock = repo->locks;
	time_t current_time = time(NULL);
	int time_remaining;

	while (lock) {
		time_remaining = lock->start_time + lock->timeout -
			current_time;
		if (!lock->refreshing && time_remaining < LOCK_REFRESH) {
			if (!refresh_lock(lock)) {
				fprintf(stderr,
					"Unable to refresh lock for %s\n",
					lock->url);
				aborted = 1;
				return;
			}
		}
		lock = lock->next;
	}
}

static void release_request(struct transfer_request *request)
{
	struct transfer_request *entry = request_queue_head;

	if (request == request_queue_head) {
		request_queue_head = request->next;
	} else {
		while (entry && entry->next != request)
			entry = entry->next;
		if (entry)
			entry->next = request->next;
	}

	free(request->url);
	free(request->dest);
	strbuf_release(&request->buffer.buf);
	free(request);
}

static void finish_request(struct transfer_request *request)
{
	struct http_pack_request *preq;
	struct http_object_request *obj_req;

	request->curl_result = request->slot->curl_result;
	request->http_code = request->slot->http_code;
	request->slot = NULL;

	/* Keep locks active */
	check_locks();

	if (request->headers)
		curl_slist_free_all(request->headers);

	/* URL is reused for MOVE after PUT and used during FETCH */
	if (request->state != RUN_PUT && request->state != RUN_FETCH_PACKED) {
		FREE_AND_NULL(request->url);
	}

	if (request->state == RUN_MKCOL) {
		if (request->curl_result == CURLE_OK ||
		    request->http_code == 405) {
			remote_dir_exists[request->obj->oid.hash[0]] = 1;
			start_put(request);
		} else {
			fprintf(stderr, "MKCOL %s failed, aborting (%d/%ld)\n",
				oid_to_hex(&request->obj->oid),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_PUT) {
		if (request->curl_result == CURLE_OK) {
			start_move(request);
		} else {
			fprintf(stderr,	"PUT %s failed, aborting (%d/%ld)\n",
				oid_to_hex(&request->obj->oid),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_MOVE) {
		if (request->curl_result == CURLE_OK) {
			if (push_verbosely)
				fprintf(stderr, "    sent %s\n",
					oid_to_hex(&request->obj->oid));
			request->obj->flags |= REMOTE;
			release_request(request);
		} else {
			fprintf(stderr, "MOVE %s failed, aborting (%d/%ld)\n",
				oid_to_hex(&request->obj->oid),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_FETCH_LOOSE) {
		obj_req = (struct http_object_request *)request->userData;

		if (finish_http_object_request(obj_req) == 0)
			if (obj_req->rename == 0)
				request->obj->flags |= (LOCAL | REMOTE);

		release_http_object_request(&obj_req);

		/* Try fetching packed if necessary */
		if (request->obj->flags & LOCAL) {
			release_request(request);
		} else
			start_fetch_packed(request);

	} else if (request->state == RUN_FETCH_PACKED) {
		int fail = 1;
		if (request->curl_result != CURLE_OK) {
			fprintf(stderr, "Unable to get pack file %s\n%s",
				request->url, curl_errorstr);
		} else {
			preq = (struct http_pack_request *)request->userData;

			if (preq) {
				if (finish_http_pack_request(preq) == 0)
					fail = 0;
				release_http_pack_request(preq);
			}
		}
		if (fail)
			repo->can_update_info_refs = 0;
		else
			http_install_packfile(request->target, &repo->packs);
		release_request(request);
	}
}

static int is_running_queue;
static int fill_active_slot(void *data UNUSED)
{
	struct transfer_request *request;

	if (aborted || !is_running_queue)
		return 0;

	for (request = request_queue_head; request; request = request->next) {
		if (request->state == NEED_FETCH) {
			start_fetch_loose(request);
			return 1;
		} else if (pushing && request->state == NEED_PUSH) {
			if (remote_dir_exists[request->obj->oid.hash[0]] == 1) {
				start_put(request);
			} else {
				start_mkcol(request);
			}
			return 1;
		}
	}
	return 0;
}

static void get_remote_object_list(unsigned char parent);

static void add_fetch_request(struct object *obj)
{
	struct transfer_request *request;

	check_locks();

	/*
	 * Don't fetch the object if it's known to exist locally
	 * or is already in the request queue
	 */
	if (remote_dir_exists[obj->oid.hash[0]] == -1)
		get_remote_object_list(obj->oid.hash[0]);
	if (obj->flags & (LOCAL | FETCHING))
		return;

	obj->flags |= FETCHING;
	CALLOC_ARRAY(request, 1);
	request->obj = obj;
	request->state = NEED_FETCH;
	strbuf_init(&request->buffer.buf, 0);
	request->next = request_queue_head;
	request_queue_head = request;

	fill_active_slots();
	step_active_slots();
}

static int add_send_request(struct object *obj, struct remote_lock *lock)
{
	struct transfer_request *request;
	struct packed_git *target;

	/* Keep locks active */
	check_locks();

	/*
	 * Don't push the object if it's known to exist on the remote
	 * or is already in the request queue
	 */
	if (remote_dir_exists[obj->oid.hash[0]] == -1)
		get_remote_object_list(obj->oid.hash[0]);
	if (obj->flags & (REMOTE | PUSHING))
		return 0;
	target = find_oid_pack(&obj->oid, repo->packs);
	if (target) {
		obj->flags |= REMOTE;
		return 0;
	}

	obj->flags |= PUSHING;
	CALLOC_ARRAY(request, 1);
	request->obj = obj;
	request->lock = lock;
	request->state = NEED_PUSH;
	strbuf_init(&request->buffer.buf, 0);
	request->next = request_queue_head;
	request_queue_head = request;

	fill_active_slots();
	step_active_slots();

	return 1;
}

static int fetch_indices(void)
{
	int ret;

	if (push_verbosely)
		fprintf(stderr, "Getting pack list\n");

	switch (http_get_info_packs(repo->url, &repo->packs)) {
	case HTTP_OK:
	case HTTP_MISSING_TARGET:
		ret = 0;
		break;
	default:
		ret = -1;
	}

	return ret;
}

static void one_remote_object(const struct object_id *oid)
{
	struct object *obj;

	obj = lookup_object(the_repository, oid);
	if (!obj)
		obj = parse_object(the_repository, oid);

	/* Ignore remote objects that don't exist locally */
	if (!obj)
		return;

	obj->flags |= REMOTE;
	if (!object_list_contains(objects, obj))
		object_list_insert(obj, &objects);
}

static void handle_lockprop_ctx(struct xml_ctx *ctx, int tag_closed)
{
	int *lock_flags = (int *)ctx->userData;

	if (tag_closed) {
		if (!strcmp(ctx->name, DAV_CTX_LOCKENTRY)) {
			if ((*lock_flags & DAV_PROP_LOCKEX) &&
			    (*lock_flags & DAV_PROP_LOCKWR)) {
				*lock_flags |= DAV_LOCK_OK;
			}
			*lock_flags &= DAV_LOCK_OK;
		} else if (!strcmp(ctx->name, DAV_CTX_LOCKTYPE_WRITE)) {
			*lock_flags |= DAV_PROP_LOCKWR;
		} else if (!strcmp(ctx->name, DAV_CTX_LOCKTYPE_EXCLUSIVE)) {
			*lock_flags |= DAV_PROP_LOCKEX;
		}
	}
}

static void handle_new_lock_ctx(struct xml_ctx *ctx, int tag_closed)
{
	struct remote_lock *lock = (struct remote_lock *)ctx->userData;
	git_hash_ctx hash_ctx;
	unsigned char lock_token_hash[GIT_MAX_RAWSZ];

	if (tag_closed && ctx->cdata) {
		if (!strcmp(ctx->name, DAV_ACTIVELOCK_OWNER)) {
			lock->owner = xstrdup(ctx->cdata);
		} else if (!strcmp(ctx->name, DAV_ACTIVELOCK_TIMEOUT)) {
			const char *arg;
			if (skip_prefix(ctx->cdata, "Second-", &arg))
				lock->timeout = strtol(arg, NULL, 10);
		} else if (!strcmp(ctx->name, DAV_ACTIVELOCK_TOKEN)) {
			lock->token = xstrdup(ctx->cdata);

			the_hash_algo->init_fn(&hash_ctx);
			the_hash_algo->update_fn(&hash_ctx, lock->token, strlen(lock->token));
			the_hash_algo->final_fn(lock_token_hash, &hash_ctx);

			lock->tmpfile_suffix[0] = '_';
			memcpy(lock->tmpfile_suffix + 1, hash_to_hex(lock_token_hash), the_hash_algo->hexsz);
		}
	}
}

static void one_remote_ref(const char *refname);

static void
xml_start_tag(void *userData, const char *name, const char **atts UNUSED)
{
	struct xml_ctx *ctx = (struct xml_ctx *)userData;
	const char *c = strchr(name, ':');
	int old_namelen, new_len;

	if (!c)
		c = name;
	else
		c++;

	old_namelen = strlen(ctx->name);
	new_len = old_namelen + strlen(c) + 2;

	if (new_len > ctx->len) {
		ctx->name = xrealloc(ctx->name, new_len);
		ctx->len = new_len;
	}
	xsnprintf(ctx->name + old_namelen, ctx->len - old_namelen, ".%s", c);

	FREE_AND_NULL(ctx->cdata);

	ctx->userFunc(ctx, 0);
}

static void
xml_end_tag(void *userData, const char *name)
{
	struct xml_ctx *ctx = (struct xml_ctx *)userData;
	const char *c = strchr(name, ':');
	char *ep;

	ctx->userFunc(ctx, 1);

	if (!c)
		c = name;
	else
		c++;

	ep = ctx->name + strlen(ctx->name) - strlen(c) - 1;
	*ep = 0;
}

static void
xml_cdata(void *userData, const XML_Char *s, int len)
{
	struct xml_ctx *ctx = (struct xml_ctx *)userData;
	free(ctx->cdata);
	ctx->cdata = xmemdupz(s, len);
}

static struct remote_lock *lock_remote(const char *path, long timeout)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct buffer out_buffer = { STRBUF_INIT, 0 };
	struct strbuf in_buffer = STRBUF_INIT;
	char *url;
	char *ep;
	char timeout_header[25];
	struct remote_lock *lock = NULL;
	struct curl_slist *dav_headers = http_copy_default_headers();
	struct xml_ctx ctx;
	char *escaped;

	url = xstrfmt("%s%s", repo->url, path);

	/* Make sure leading directories exist for the remote ref */
	ep = strchr(url + strlen(repo->url) + 1, '/');
	while (ep) {
		char saved_character = ep[1];
		ep[1] = '\0';
		slot = get_active_slot();
		slot->results = &results;
		curl_setup_http_get(slot->curl, url, DAV_MKCOL);
		if (start_active_slot(slot)) {
			run_active_slot(slot);
			if (results.curl_result != CURLE_OK &&
			    results.http_code != 405) {
				fprintf(stderr,
					"Unable to create branch path %s\n",
					url);
				free(url);
				return NULL;
			}
		} else {
			fprintf(stderr, "Unable to start MKCOL request\n");
			free(url);
			return NULL;
		}
		ep[1] = saved_character;
		ep = strchr(ep + 1, '/');
	}

	escaped = xml_entities(ident_default_email());
	strbuf_addf(&out_buffer.buf, LOCK_REQUEST, escaped);
	free(escaped);

	xsnprintf(timeout_header, sizeof(timeout_header), "Timeout: Second-%ld", timeout);
	dav_headers = curl_slist_append(dav_headers, timeout_header);
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	slot->results = &results;
	curl_setup_http(slot->curl, url, DAV_LOCK, &out_buffer, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEDATA, &in_buffer);

	CALLOC_ARRAY(lock, 1);
	lock->timeout = -1;

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK) {
			XML_Parser parser = XML_ParserCreate(NULL);
			enum XML_Status result;
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_new_lock_ctx;
			ctx.userData = lock;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			XML_SetCharacterDataHandler(parser, xml_cdata);
			result = XML_Parse(parser, in_buffer.buf,
					   in_buffer.len, 1);
			free(ctx.name);
			free(ctx.cdata);
			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
				lock->timeout = -1;
			}
			XML_ParserFree(parser);
		} else {
			fprintf(stderr,
				"error: curl result=%d, HTTP code=%ld\n",
				results.curl_result, results.http_code);
		}
	} else {
		fprintf(stderr, "Unable to start LOCK request\n");
	}

	curl_slist_free_all(dav_headers);
	strbuf_release(&out_buffer.buf);
	strbuf_release(&in_buffer);

	if (lock->token == NULL || lock->timeout <= 0) {
		free(lock->token);
		free(lock->owner);
		free(url);
		FREE_AND_NULL(lock);
	} else {
		lock->url = url;
		lock->start_time = time(NULL);
		lock->next = repo->locks;
		repo->locks = lock;
	}

	return lock;
}

static int unlock_remote(struct remote_lock *lock)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct remote_lock *prev = repo->locks;
	struct curl_slist *dav_headers;
	int rc = 0;

	dav_headers = get_dav_token_headers(lock, DAV_HEADER_LOCK);

	slot = get_active_slot();
	slot->results = &results;
	curl_setup_http_get(slot->curl, lock->url, DAV_UNLOCK);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK)
			rc = 1;
		else
			fprintf(stderr, "UNLOCK HTTP error %ld\n",
				results.http_code);
	} else {
		fprintf(stderr, "Unable to start UNLOCK request\n");
	}

	curl_slist_free_all(dav_headers);

	if (repo->locks == lock) {
		repo->locks = lock->next;
	} else {
		while (prev && prev->next != lock)
			prev = prev->next;
		if (prev)
			prev->next = lock->next;
	}

	free(lock->owner);
	free(lock->url);
	free(lock->token);
	free(lock);

	return rc;
}

static void remove_locks(void)
{
	struct remote_lock *lock = repo->locks;

	fprintf(stderr, "Removing remote locks...\n");
	while (lock) {
		struct remote_lock *next = lock->next;
		unlock_remote(lock);
		lock = next;
	}
}

static void remove_locks_on_signal(int signo)
{
	remove_locks();
	sigchain_pop(signo);
	raise(signo);
}

static void remote_ls(const char *path, int flags,
		      void (*userFunc)(struct remote_ls_ctx *ls),
		      void *userData);

/* extract hex from sharded "xx/x{38}" filename */
static int get_oid_hex_from_objpath(const char *path, struct object_id *oid)
{
	memset(oid->hash, 0, GIT_MAX_RAWSZ);
	oid->algo = hash_algo_by_ptr(the_hash_algo);

	if (strlen(path) != the_hash_algo->hexsz + 1)
		return -1;

	if (hex_to_bytes(oid->hash, path, 1))
		return -1;
	path += 2;
	path++; /* skip '/' */

	return hex_to_bytes(oid->hash + 1, path, the_hash_algo->rawsz - 1);
}

static void process_ls_object(struct remote_ls_ctx *ls)
{
	unsigned int *parent = (unsigned int *)ls->userData;
	const char *path = ls->dentry_name;
	struct object_id oid;

	if (!strcmp(ls->path, ls->dentry_name) && (ls->flags & IS_DIR)) {
		remote_dir_exists[*parent] = 1;
		return;
	}

	if (!skip_prefix(path, "objects/", &path) ||
	    get_oid_hex_from_objpath(path, &oid))
		return;

	one_remote_object(&oid);
}

static void process_ls_ref(struct remote_ls_ctx *ls)
{
	if (!strcmp(ls->path, ls->dentry_name) && (ls->dentry_flags & IS_DIR)) {
		fprintf(stderr, "  %s\n", ls->dentry_name);
		return;
	}

	if (!(ls->dentry_flags & IS_DIR))
		one_remote_ref(ls->dentry_name);
}

static void handle_remote_ls_ctx(struct xml_ctx *ctx, int tag_closed)
{
	struct remote_ls_ctx *ls = (struct remote_ls_ctx *)ctx->userData;

	if (tag_closed) {
		if (!strcmp(ctx->name, DAV_PROPFIND_RESP) && ls->dentry_name) {
			if (ls->dentry_flags & IS_DIR) {

				/* ensure collection names end with slash */
				str_end_url_with_slash(ls->dentry_name, &ls->dentry_name);

				if (ls->flags & PROCESS_DIRS) {
					ls->userFunc(ls);
				}
				if (strcmp(ls->dentry_name, ls->path) &&
				    ls->flags & RECURSIVE) {
					remote_ls(ls->dentry_name,
						  ls->flags,
						  ls->userFunc,
						  ls->userData);
				}
			} else if (ls->flags & PROCESS_FILES) {
				ls->userFunc(ls);
			}
		} else if (!strcmp(ctx->name, DAV_PROPFIND_NAME) && ctx->cdata) {
			char *path = ctx->cdata;
			if (*ctx->cdata == 'h') {
				path = strstr(path, "//");
				if (path) {
					path = strchr(path+2, '/');
				}
			}
			if (path) {
				const char *url = repo->url;
				if (repo->path)
					url = repo->path;
				if (strncmp(path, url, repo->path_len))
					error("Parsed path '%s' does not match url: '%s'",
					      path, url);
				else {
					path += repo->path_len;
					ls->dentry_name = xstrdup(path);
				}
			}
		} else if (!strcmp(ctx->name, DAV_PROPFIND_COLLECTION)) {
			ls->dentry_flags |= IS_DIR;
		}
	} else if (!strcmp(ctx->name, DAV_PROPFIND_RESP)) {
		FREE_AND_NULL(ls->dentry_name);
		ls->dentry_flags = 0;
	}
}

/*
 * NEEDSWORK: remote_ls() ignores info/refs on the remote side.  But it
 * should _only_ heed the information from that file, instead of trying to
 * determine the refs from the remote file system (badly: it does not even
 * know about packed-refs).
 */
static void remote_ls(const char *path, int flags,
		      void (*userFunc)(struct remote_ls_ctx *ls),
		      void *userData)
{
	char *url = xstrfmt("%s%s", repo->url, path);
	struct active_request_slot *slot;
	struct slot_results results;
	struct strbuf in_buffer = STRBUF_INIT;
	struct buffer out_buffer = { STRBUF_INIT, 0 };
	struct curl_slist *dav_headers = http_copy_default_headers();
	struct xml_ctx ctx;
	struct remote_ls_ctx ls;

	ls.flags = flags;
	ls.path = xstrdup(path);
	ls.dentry_name = NULL;
	ls.dentry_flags = 0;
	ls.userData = userData;
	ls.userFunc = userFunc;

	strbuf_addstr(&out_buffer.buf, PROPFIND_ALL_REQUEST);

	dav_headers = curl_slist_append(dav_headers, "Depth: 1");
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	slot->results = &results;
	curl_setup_http(slot->curl, url, DAV_PROPFIND,
			&out_buffer, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEDATA, &in_buffer);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK) {
			XML_Parser parser = XML_ParserCreate(NULL);
			enum XML_Status result;
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_remote_ls_ctx;
			ctx.userData = &ls;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			XML_SetCharacterDataHandler(parser, xml_cdata);
			result = XML_Parse(parser, in_buffer.buf,
					   in_buffer.len, 1);
			free(ctx.name);
			free(ctx.cdata);

			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
			}
			XML_ParserFree(parser);
		}
	} else {
		fprintf(stderr, "Unable to start PROPFIND request\n");
	}

	free(ls.path);
	free(ls.dentry_name);
	free(url);
	strbuf_release(&out_buffer.buf);
	strbuf_release(&in_buffer);
	curl_slist_free_all(dav_headers);
}

static void get_remote_object_list(unsigned char parent)
{
	char path[] = "objects/XX/";
	static const char hex[] = "0123456789abcdef";
	unsigned int val = parent;

	path[8] = hex[val >> 4];
	path[9] = hex[val & 0xf];
	remote_dir_exists[val] = 0;
	remote_ls(path, (PROCESS_FILES | PROCESS_DIRS),
		  process_ls_object, &val);
}

static int locking_available(void)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct strbuf in_buffer = STRBUF_INIT;
	struct buffer out_buffer = { STRBUF_INIT, 0 };
	struct curl_slist *dav_headers = http_copy_default_headers();
	struct xml_ctx ctx;
	int lock_flags = 0;
	char *escaped;

	escaped = xml_entities(repo->url);
	strbuf_addf(&out_buffer.buf, PROPFIND_SUPPORTEDLOCK_REQUEST, escaped);
	free(escaped);

	dav_headers = curl_slist_append(dav_headers, "Depth: 0");
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	slot->results = &results;
	curl_setup_http(slot->curl, repo->url, DAV_PROPFIND,
			&out_buffer, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEDATA, &in_buffer);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK) {
			XML_Parser parser = XML_ParserCreate(NULL);
			enum XML_Status result;
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_lockprop_ctx;
			ctx.userData = &lock_flags;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			result = XML_Parse(parser, in_buffer.buf,
					   in_buffer.len, 1);
			free(ctx.name);

			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
				lock_flags = 0;
			}
			XML_ParserFree(parser);
			if (!lock_flags)
				error("no DAV locking support on %s",
				      repo->url);

		} else {
			error("Cannot access URL %s, return code %d",
			      repo->url, results.curl_result);
			lock_flags = 0;
		}
	} else {
		error("Unable to start PROPFIND request on %s", repo->url);
	}

	strbuf_release(&out_buffer.buf);
	strbuf_release(&in_buffer);
	curl_slist_free_all(dav_headers);

	return lock_flags;
}

static struct object_list **add_one_object(struct object *obj, struct object_list **p)
{
	struct object_list *entry = xmalloc(sizeof(struct object_list));
	entry->item = obj;
	entry->next = *p;
	*p = entry;
	return &entry->next;
}

static struct object_list **process_blob(struct blob *blob,
					 struct object_list **p)
{
	struct object *obj = &blob->object;

	obj->flags |= LOCAL;

	if (obj->flags & (UNINTERESTING | SEEN))
		return p;

	obj->flags |= SEEN;
	return add_one_object(obj, p);
}

static struct object_list **process_tree(struct tree *tree,
					 struct object_list **p)
{
	struct object *obj = &tree->object;
	struct tree_desc desc;
	struct name_entry entry;

	obj->flags |= LOCAL;

	if (obj->flags & (UNINTERESTING | SEEN))
		return p;
	if (parse_tree(tree) < 0)
		die("bad tree object %s", oid_to_hex(&obj->oid));

	obj->flags |= SEEN;
	p = add_one_object(obj, p);

	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry))
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			p = process_tree(lookup_tree(the_repository, &entry.oid),
					 p);
			break;
		case OBJ_BLOB:
			p = process_blob(lookup_blob(the_repository, &entry.oid),
					 p);
			break;
		default:
			/* Subproject commit - not in this repository */
			break;
		}

	free_tree_buffer(tree);
	return p;
}

static int get_delta(struct rev_info *revs, struct remote_lock *lock)
{
	struct commit *commit;
	struct object_list **p = &objects;
	int count = 0;

	while ((commit = get_revision(revs)) != NULL) {
		p = process_tree(repo_get_commit_tree(the_repository, commit),
				 p);
		commit->object.flags |= LOCAL;
		if (!(commit->object.flags & UNINTERESTING))
			count += add_send_request(&commit->object, lock);
	}

	for (size_t i = 0; i < revs->pending.nr; i++) {
		struct object_array_entry *entry = revs->pending.objects + i;
		struct object *obj = entry->item;
		const char *name = entry->name;

		if (obj->flags & (UNINTERESTING | SEEN))
			continue;
		if (obj->type == OBJ_TAG) {
			obj->flags |= SEEN;
			p = add_one_object(obj, p);
			continue;
		}
		if (obj->type == OBJ_TREE) {
			p = process_tree((struct tree *)obj, p);
			continue;
		}
		if (obj->type == OBJ_BLOB) {
			p = process_blob((struct blob *)obj, p);
			continue;
		}
		die("unknown pending object %s (%s)", oid_to_hex(&obj->oid), name);
	}

	while (objects) {
		struct object_list *next = objects->next;

		if (!(objects->item->flags & UNINTERESTING))
			count += add_send_request(objects->item, lock);

		free(objects);
		objects = next;
	}

	return count;
}

static int update_remote(const struct object_id *oid, struct remote_lock *lock)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct buffer out_buffer = { STRBUF_INIT, 0 };
	struct curl_slist *dav_headers;

	dav_headers = get_dav_token_headers(lock, DAV_HEADER_IF);

	strbuf_addf(&out_buffer.buf, "%s\n", oid_to_hex(oid));

	slot = get_active_slot();
	slot->results = &results;
	curl_setup_http(slot->curl, lock->url, DAV_PUT,
			&out_buffer, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		strbuf_release(&out_buffer.buf);
		curl_slist_free_all(dav_headers);
		if (results.curl_result != CURLE_OK) {
			fprintf(stderr,
				"PUT error: curl result=%d, HTTP code=%ld\n",
				results.curl_result, results.http_code);
			/* We should attempt recovery? */
			return 0;
		}
	} else {
		strbuf_release(&out_buffer.buf);
		curl_slist_free_all(dav_headers);
		fprintf(stderr, "Unable to start PUT request\n");
		return 0;
	}

	return 1;
}

static struct ref *remote_refs;

static void one_remote_ref(const char *refname)
{
	struct ref *ref;
	struct object *obj;

	ref = alloc_ref(refname);

	if (http_fetch_ref(repo->url, ref) != 0) {
		fprintf(stderr,
			"Unable to fetch ref %s from %s\n",
			refname, repo->url);
		free(ref);
		return;
	}

	/*
	 * Fetch a copy of the object if it doesn't exist locally - it
	 * may be required for updating server info later.
	 */
	if (repo->can_update_info_refs && !repo_has_object_file(the_repository, &ref->old_oid)) {
		obj = lookup_unknown_object(the_repository, &ref->old_oid);
		fprintf(stderr,	"  fetch %s for %s\n",
			oid_to_hex(&ref->old_oid), refname);
		add_fetch_request(obj);
	}

	ref->next = remote_refs;
	remote_refs = ref;
}

static void get_dav_remote_heads(void)
{
	remote_ls("refs/", (PROCESS_FILES | PROCESS_DIRS | RECURSIVE), process_ls_ref, NULL);
}

static void add_remote_info_ref(struct remote_ls_ctx *ls)
{
	struct strbuf *buf = (struct strbuf *)ls->userData;
	struct object *o;
	struct ref *ref;

	ref = alloc_ref(ls->dentry_name);

	if (http_fetch_ref(repo->url, ref) != 0) {
		fprintf(stderr,
			"Unable to fetch ref %s from %s\n",
			ls->dentry_name, repo->url);
		aborted = 1;
		free(ref);
		return;
	}

	o = parse_object(the_repository, &ref->old_oid);
	if (!o) {
		fprintf(stderr,
			"Unable to parse object %s for remote ref %s\n",
			oid_to_hex(&ref->old_oid), ls->dentry_name);
		aborted = 1;
		free(ref);
		return;
	}

	strbuf_addf(buf, "%s\t%s\n",
		    oid_to_hex(&ref->old_oid), ls->dentry_name);

	if (o->type == OBJ_TAG) {
		o = deref_tag(the_repository, o, ls->dentry_name, 0);
		if (o)
			strbuf_addf(buf, "%s\t%s^{}\n",
				    oid_to_hex(&o->oid), ls->dentry_name);
	}
	free(ref);
}

static void update_remote_info_refs(struct remote_lock *lock)
{
	struct buffer buffer = { STRBUF_INIT, 0 };
	struct active_request_slot *slot;
	struct slot_results results;
	struct curl_slist *dav_headers;

	remote_ls("refs/", (PROCESS_FILES | RECURSIVE),
		  add_remote_info_ref, &buffer.buf);
	if (!aborted) {
		dav_headers = get_dav_token_headers(lock, DAV_HEADER_IF);

		slot = get_active_slot();
		slot->results = &results;
		curl_setup_http(slot->curl, lock->url, DAV_PUT,
				&buffer, fwrite_null);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

		if (start_active_slot(slot)) {
			run_active_slot(slot);
			if (results.curl_result != CURLE_OK) {
				fprintf(stderr,
					"PUT error: curl result=%d, HTTP code=%ld\n",
					results.curl_result, results.http_code);
			}
		}
		curl_slist_free_all(dav_headers);
	}
	strbuf_release(&buffer.buf);
}

static int remote_exists(const char *path)
{
	char *url = xstrfmt("%s%s", repo->url, path);
	int ret;


	switch (http_get_strbuf(url, NULL, NULL)) {
	case HTTP_OK:
		ret = 1;
		break;
	case HTTP_MISSING_TARGET:
		ret = 0;
		break;
	case HTTP_ERROR:
		error("unable to access '%s': %s", url, curl_errorstr);
		/* fallthrough */
	default:
		ret = -1;
	}
	free(url);
	return ret;
}

static void fetch_symref(const char *path, char **symref, struct object_id *oid)
{
	char *url = xstrfmt("%s%s", repo->url, path);
	struct strbuf buffer = STRBUF_INIT;
	const char *name;

	if (http_get_strbuf(url, &buffer, NULL) != HTTP_OK)
		die("Couldn't get %s for remote symref\n%s", url,
		    curl_errorstr);
	free(url);

	FREE_AND_NULL(*symref);
	oidclr(oid, the_repository->hash_algo);

	if (buffer.len == 0)
		return;

	/* Cut off trailing newline. */
	strbuf_rtrim(&buffer);

	/* If it's a symref, set the refname; otherwise try for a sha1 */
	if (skip_prefix(buffer.buf, "ref: ", &name)) {
		*symref = xmemdupz(name, buffer.len - (name - buffer.buf));
	} else {
		get_oid_hex(buffer.buf, oid);
	}

	strbuf_release(&buffer);
}

static int verify_merge_base(struct object_id *head_oid, struct ref *remote)
{
	struct commit *head = lookup_commit_or_die(head_oid, "HEAD");
	struct commit *branch = lookup_commit_or_die(&remote->old_oid,
						     remote->name);
	int ret = repo_in_merge_bases(the_repository, branch, head);

	if (ret < 0)
		exit(128);
	return ret;
}

static int delete_remote_branch(const char *pattern, int force)
{
	struct ref *refs = remote_refs;
	struct ref *remote_ref = NULL;
	struct object_id head_oid;
	char *symref = NULL;
	int match;
	int patlen = strlen(pattern);
	int i;
	struct active_request_slot *slot;
	struct slot_results results;
	char *url;

	/* Find the remote branch(es) matching the specified branch name */
	for (match = 0; refs; refs = refs->next) {
		char *name = refs->name;
		int namelen = strlen(name);
		if (namelen < patlen ||
		    memcmp(name + namelen - patlen, pattern, patlen))
			continue;
		if (namelen != patlen && name[namelen - patlen - 1] != '/')
			continue;
		match++;
		remote_ref = refs;
	}
	if (match == 0)
		return error("No remote branch matches %s", pattern);
	if (match != 1)
		return error("More than one remote branch matches %s",
			     pattern);

	/*
	 * Remote HEAD must be a symref (not exactly foolproof; a remote
	 * symlink to a symref will look like a symref)
	 */
	fetch_symref("HEAD", &symref, &head_oid);
	if (!symref)
		return error("Remote HEAD is not a symref");

	/* Remote branch must not be the remote HEAD */
	for (i = 0; symref && i < MAXDEPTH; i++) {
		if (!strcmp(remote_ref->name, symref))
			return error("Remote branch %s is the current HEAD",
				     remote_ref->name);
		fetch_symref(symref, &symref, &head_oid);
	}

	/* Run extra sanity checks if delete is not forced */
	if (!force) {
		/* Remote HEAD must resolve to a known object */
		if (symref)
			return error("Remote HEAD symrefs too deep");
		if (is_null_oid(&head_oid))
			return error("Unable to resolve remote HEAD");
		if (!repo_has_object_file(the_repository, &head_oid))
			return error("Remote HEAD resolves to object %s\nwhich does not exist locally, perhaps you need to fetch?", oid_to_hex(&head_oid));

		/* Remote branch must resolve to a known object */
		if (is_null_oid(&remote_ref->old_oid))
			return error("Unable to resolve remote branch %s",
				     remote_ref->name);
		if (!repo_has_object_file(the_repository, &remote_ref->old_oid))
			return error("Remote branch %s resolves to object %s\nwhich does not exist locally, perhaps you need to fetch?", remote_ref->name, oid_to_hex(&remote_ref->old_oid));

		/* Remote branch must be an ancestor of remote HEAD */
		if (!verify_merge_base(&head_oid, remote_ref)) {
			return error("The branch '%s' is not an ancestor "
				     "of your current HEAD.\n"
				     "If you are sure you want to delete it,"
				     " run:\n\t'git http-push -D %s %s'",
				     remote_ref->name, repo->url, pattern);
		}
	}

	/* Send delete request */
	fprintf(stderr, "Removing remote branch '%s'\n", remote_ref->name);
	if (dry_run)
		return 0;
	url = xstrfmt("%s%s", repo->url, remote_ref->name);
	slot = get_active_slot();
	slot->results = &results;
	curl_setup_http_get(slot->curl, url, DAV_DELETE);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		free(url);
		if (results.curl_result != CURLE_OK)
			return error("DELETE request failed (%d/%ld)",
				     results.curl_result, results.http_code);
	} else {
		free(url);
		return error("Unable to start DELETE request");
	}

	return 0;
}

static void run_request_queue(void)
{
	is_running_queue = 1;
	fill_active_slots();
	add_fill_function(NULL, fill_active_slot);
	do {
		finish_all_active_slots();
		fill_active_slots();
	} while (request_queue_head && !aborted);

	is_running_queue = 0;
}

int cmd_main(int argc, const char **argv)
{
	struct transfer_request *request;
	struct transfer_request *next_request;
	struct refspec rs = REFSPEC_INIT_PUSH;
	struct remote_lock *ref_lock = NULL;
	struct remote_lock *info_ref_lock = NULL;
	int delete_branch = 0;
	int force_delete = 0;
	int objects_to_send;
	int rc = 0;
	int i;
	int new_refs;
	struct ref *ref, *local_refs = NULL;

	CALLOC_ARRAY(repo, 1);

	argv++;
	for (i = 1; i < argc; i++, argv++) {
		const char *arg = *argv;

		if (*arg == '-') {
			if (!strcmp(arg, "--all")) {
				push_all = MATCH_REFS_ALL;
				continue;
			}
			if (!strcmp(arg, "--force")) {
				force_all = 1;
				continue;
			}
			if (!strcmp(arg, "--dry-run")) {
				dry_run = 1;
				continue;
			}
			if (!strcmp(arg, "--helper-status")) {
				helper_status = 1;
				continue;
			}
			if (!strcmp(arg, "--verbose")) {
				push_verbosely = 1;
				http_is_verbose = 1;
				continue;
			}
			if (!strcmp(arg, "-d")) {
				delete_branch = 1;
				continue;
			}
			if (!strcmp(arg, "-D")) {
				delete_branch = 1;
				force_delete = 1;
				continue;
			}
			if (!strcmp(arg, "-h"))
				usage(http_push_usage);
		}
		if (!repo->url) {
			char *path = strstr(arg, "//");
			str_end_url_with_slash(arg, &repo->url);
			repo->path_len = strlen(repo->url);
			if (path) {
				repo->path = strchr(path+2, '/');
				if (repo->path)
					repo->path_len = strlen(repo->path);
			}
			continue;
		}
		refspec_appendn(&rs, argv, argc - i);
		break;
	}

	if (!repo->url)
		usage(http_push_usage);

	if (delete_branch && rs.nr != 1)
		die("You must specify only one branch name when deleting a remote branch");

	setup_git_directory();

	memset(remote_dir_exists, -1, 256);

	http_init(NULL, repo->url, 1);

	is_running_queue = 0;

	/* Verify DAV compliance/lock support */
	if (!locking_available()) {
		rc = 1;
		goto cleanup;
	}

	sigchain_push_common(remove_locks_on_signal);

	/* Check whether the remote has server info files */
	repo->can_update_info_refs = 0;
	repo->has_info_refs = remote_exists("info/refs");
	repo->has_info_packs = remote_exists("objects/info/packs");
	if (repo->has_info_refs) {
		info_ref_lock = lock_remote("info/refs", LOCK_TIME);
		if (info_ref_lock)
			repo->can_update_info_refs = 1;
		else {
			error("cannot lock existing info/refs");
			rc = 1;
			goto cleanup;
		}
	}
	if (repo->has_info_packs)
		fetch_indices();

	/* Get a list of all local and remote heads to validate refspecs */
	local_refs = get_local_heads();
	fprintf(stderr, "Fetching remote heads...\n");
	get_dav_remote_heads();
	run_request_queue();

	/* Remove a remote branch if -d or -D was specified */
	if (delete_branch) {
		const char *branch = rs.items[i].src;
		if (delete_remote_branch(branch, force_delete) == -1) {
			fprintf(stderr, "Unable to delete remote branch %s\n",
				branch);
			if (helper_status)
				printf("error %s cannot remove\n", branch);
		}
		goto cleanup;
	}

	/* match them up */
	if (match_push_refs(local_refs, &remote_refs, &rs, push_all)) {
		rc = -1;
		goto cleanup;
	}
	if (!remote_refs) {
		fprintf(stderr, "No refs in common and none specified; doing nothing.\n");
		if (helper_status)
			printf("error null no match\n");
		rc = 0;
		goto cleanup;
	}

	new_refs = 0;
	for (ref = remote_refs; ref; ref = ref->next) {
		struct rev_info revs;
		struct strvec commit_argv = STRVEC_INIT;

		if (!ref->peer_ref)
			continue;

		if (is_null_oid(&ref->peer_ref->new_oid)) {
			if (delete_remote_branch(ref->name, 1) == -1) {
				error("Could not remove %s", ref->name);
				if (helper_status)
					printf("error %s cannot remove\n", ref->name);
				rc = -4;
			}
			else if (helper_status)
				printf("ok %s\n", ref->name);
			new_refs++;
			continue;
		}

		if (oideq(&ref->old_oid, &ref->peer_ref->new_oid)) {
			if (push_verbosely)
				/* stable plumbing output; do not modify or localize */
				fprintf(stderr, "'%s': up-to-date\n", ref->name);
			if (helper_status)
				printf("ok %s up to date\n", ref->name);
			continue;
		}

		if (!force_all &&
		    !is_null_oid(&ref->old_oid) &&
		    !ref->force) {
			if (!repo_has_object_file(the_repository, &ref->old_oid) ||
			    !ref_newer(&ref->peer_ref->new_oid,
				       &ref->old_oid)) {
				/*
				 * We do not have the remote ref, or
				 * we know that the remote ref is not
				 * an ancestor of what we are trying to
				 * push.  Either way this can be losing
				 * commits at the remote end and likely
				 * we were not up to date to begin with.
				 */
				/* stable plumbing output; do not modify or localize */
				error("remote '%s' is not an ancestor of\n"
				      "local '%s'.\n"
				      "Maybe you are not up-to-date and "
				      "need to pull first?",
				      ref->name,
				      ref->peer_ref->name);
				if (helper_status)
					printf("error %s non-fast forward\n", ref->name);
				rc = -2;
				continue;
			}
		}
		oidcpy(&ref->new_oid, &ref->peer_ref->new_oid);
		new_refs++;

		fprintf(stderr, "updating '%s'", ref->name);
		if (strcmp(ref->name, ref->peer_ref->name))
			fprintf(stderr, " using '%s'", ref->peer_ref->name);
		fprintf(stderr, "\n  from %s\n  to   %s\n",
			oid_to_hex(&ref->old_oid), oid_to_hex(&ref->new_oid));
		if (dry_run) {
			if (helper_status)
				printf("ok %s\n", ref->name);
			continue;
		}

		/* Lock remote branch ref */
		ref_lock = lock_remote(ref->name, LOCK_TIME);
		if (!ref_lock) {
			fprintf(stderr, "Unable to lock remote branch %s\n",
				ref->name);
			if (helper_status)
				printf("error %s lock error\n", ref->name);
			rc = 1;
			continue;
		}

		/* Set up revision info for this refspec */
		strvec_push(&commit_argv, ""); /* ignored */
		strvec_push(&commit_argv, "--objects");
		strvec_push(&commit_argv, oid_to_hex(&ref->new_oid));
		if (!push_all && !is_null_oid(&ref->old_oid))
			strvec_pushf(&commit_argv, "^%s",
				     oid_to_hex(&ref->old_oid));
		repo_init_revisions(the_repository, &revs, setup_git_directory());
		setup_revisions(commit_argv.nr, commit_argv.v, &revs, NULL);
		revs.edge_hint = 0; /* just in case */

		/* Generate a list of objects that need to be pushed */
		pushing = 0;
		if (prepare_revision_walk(&revs))
			die("revision walk setup failed");
		mark_edges_uninteresting(&revs, NULL, 0);
		objects_to_send = get_delta(&revs, ref_lock);
		finish_all_active_slots();

		/* Push missing objects to remote, this would be a
		   convenient time to pack them first if appropriate. */
		pushing = 1;
		if (objects_to_send)
			fprintf(stderr, "    sending %d objects\n",
				objects_to_send);

		run_request_queue();

		/* Update the remote branch if all went well */
		if (aborted || !update_remote(&ref->new_oid, ref_lock))
			rc = 1;

		if (!rc)
			fprintf(stderr, "    done\n");
		if (helper_status)
			printf("%s %s\n", !rc ? "ok" : "error", ref->name);
		unlock_remote(ref_lock);
		check_locks();
		strvec_clear(&commit_argv);
		release_revisions(&revs);
	}

	/* Update remote server info if appropriate */
	if (repo->has_info_refs && new_refs) {
		if (info_ref_lock && repo->can_update_info_refs) {
			fprintf(stderr, "Updating remote server info\n");
			if (!dry_run)
				update_remote_info_refs(info_ref_lock);
		} else {
			fprintf(stderr, "Unable to update server info\n");
		}
	}

 cleanup:
	if (info_ref_lock)
		unlock_remote(info_ref_lock);
	free(repo->url);
	free(repo);

	http_cleanup();

	request = request_queue_head;
	while (request != NULL) {
		next_request = request->next;
		release_request(request);
		request = next_request;
	}

	refspec_clear(&rs);
	free_refs(local_refs);

	return rc;
}
