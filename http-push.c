#include "cache.h"
#include "commit.h"
#include "pack.h"
#include "fetch.h"
#include "tag.h"
#include "blob.h"
#include "http.h"
#include "refs.h"
#include "revision.h"

#include <expat.h>

static const char http_push_usage[] =
"git-http-push [--complete] [--force] [--verbose] <url> <ref> [<ref>...]\n";

#ifndef XML_STATUS_OK
enum XML_Status {
  XML_STATUS_OK = 1,
  XML_STATUS_ERROR = 0
};
#define XML_STATUS_OK    1
#define XML_STATUS_ERROR 0
#endif

#define RANGE_HEADER_SIZE 30

/* DAV methods */
#define DAV_LOCK "LOCK"
#define DAV_MKCOL "MKCOL"
#define DAV_MOVE "MOVE"
#define DAV_PROPFIND "PROPFIND"
#define DAV_PUT "PUT"
#define DAV_UNLOCK "UNLOCK"

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

/* bits #0-4 in revision.h */

#define LOCAL   (1u << 5)
#define REMOTE  (1u << 6)
#define PUSHING (1u << 7)

static int pushing = 0;
static int aborted = 0;
static char remote_dir_exists[256];

static struct curl_slist *no_pragma_header;
static struct curl_slist *default_headers;

static int push_verbosely = 0;
static int push_all = 0;
static int force_all = 0;

static struct object_list *objects = NULL;

struct repo
{
	char *url;
	int path_len;
	struct packed_git *packs;
};

static struct repo *remote = NULL;
static struct remote_lock *remote_locks = NULL;

enum transfer_state {
	NEED_PUSH,
	RUN_MKCOL,
	RUN_PUT,
	RUN_MOVE,
	ABORTED,
	COMPLETE,
};

struct transfer_request
{
	struct object *obj;
	char *url;
	char *dest;
	struct remote_lock *lock;
	struct curl_slist *headers;
	struct buffer buffer;
	char filename[PATH_MAX];
	char tmpfile[PATH_MAX];
	enum transfer_state state;
	CURLcode curl_result;
	char errorstr[CURL_ERROR_SIZE];
	long http_code;
	unsigned char real_sha1[20];
	SHA_CTX c;
	z_stream stream;
	int zret;
	int rename;
	struct active_request_slot *slot;
	struct transfer_request *next;
};

static struct transfer_request *request_queue_head = NULL;

struct xml_ctx
{
	char *name;
	int len;
	char *cdata;
	void (*userFunc)(struct xml_ctx *ctx, int tag_closed);
	void *userData;
};

struct remote_lock
{
	char *url;
	char *owner;
	char *token;
	time_t start_time;
	long timeout;
	int active;
	int refreshing;
	struct remote_lock *next;
};

struct remote_dentry
{
	char *base;
	char *name;
	int is_dir;
};

static void finish_request(struct transfer_request *request);

static void process_response(void *callback_data)
{
	struct transfer_request *request =
		(struct transfer_request *)callback_data;

	finish_request(request);
}

static void start_mkcol(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->obj->sha1);
	struct active_request_slot *slot;
	char *posn;

	request->url = xmalloc(strlen(remote->url) + 13);
	strcpy(request->url, remote->url);
	posn = request->url + strlen(remote->url);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	strcpy(posn, "/");

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1); /* undo PUT setup */
	curl_easy_setopt(slot->curl, CURLOPT_URL, request->url);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, request->errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_MKCOL);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_MKCOL;
	} else {
		request->state = ABORTED;
		free(request->url);
		request->url = NULL;
	}
}

static void start_put(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->obj->sha1);
	struct active_request_slot *slot;
	char *posn;
	char type[20];
	char hdr[50];
	void *unpacked;
	unsigned long len;
	int hdrlen;
	ssize_t size;
	z_stream stream;

	unpacked = read_sha1_file(request->obj->sha1, type, &len);
	hdrlen = sprintf(hdr, "%s %lu", type, len) + 1;

	/* Set it up */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, Z_BEST_COMPRESSION);
	size = deflateBound(&stream, len + hdrlen);
	request->buffer.buffer = xmalloc(size);

	/* Compress it */
	stream.next_out = request->buffer.buffer;
	stream.avail_out = size;

	/* First header.. */
	stream.next_in = (void *)hdr;
	stream.avail_in = hdrlen;
	while (deflate(&stream, 0) == Z_OK)
		/* nothing */;

	/* Then the data itself.. */
	stream.next_in = unpacked;
	stream.avail_in = len;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);
	free(unpacked);

	request->buffer.size = stream.total_out;
	request->buffer.posn = 0;

	request->url = xmalloc(strlen(remote->url) + 
			       strlen(request->lock->token) + 51);
	strcpy(request->url, remote->url);
	posn = request->url + strlen(remote->url);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	*(posn++) = '/';
	strcpy(posn, hex + 2);
	request->dest = xmalloc(strlen(request->url) + 14);
	sprintf(request->dest, "Destination: %s", request->url);
	posn += 38;
	*(posn++) = '.';
	strcpy(posn, request->lock->token);

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &request->buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, request->buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PUT);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_PUT, 1);
	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(slot->curl, CURLOPT_URL, request->url);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_PUT;
	} else {
		request->state = ABORTED;
		free(request->url);
		request->url = NULL;
	}
}

static void start_move(struct transfer_request *request)
{
	struct active_request_slot *slot;
	struct curl_slist *dav_headers = NULL;

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1); /* undo PUT setup */
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_MOVE);
	dav_headers = curl_slist_append(dav_headers, request->dest);
	dav_headers = curl_slist_append(dav_headers, "Overwrite: T");
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_URL, request->url);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_MOVE;
	} else {
		request->state = ABORTED;
		free(request->url);
		request->url = NULL;
	}
}

static int refresh_lock(struct remote_lock *check_lock)
{
	struct active_request_slot *slot;
	char *if_header;
	char timeout_header[25];
	struct curl_slist *dav_headers = NULL;
	struct remote_lock *lock;
	int time_remaining;
	time_t current_time;

	/* Refresh all active locks if they're close to expiring */
	for (lock = remote_locks; lock; lock = lock->next) {
		if (!lock->active)
			continue;

		current_time = time(NULL);
		time_remaining = lock->start_time + lock->timeout
			- current_time;
		if (time_remaining > LOCK_REFRESH)
			continue;

		lock->refreshing = 1;

		if_header = xmalloc(strlen(lock->token) + 25);
		sprintf(if_header, "If: (<opaquelocktoken:%s>)", lock->token);
		sprintf(timeout_header, "Timeout: Second-%ld", lock->timeout);
		dav_headers = curl_slist_append(dav_headers, if_header);
		dav_headers = curl_slist_append(dav_headers, timeout_header);

		slot = get_active_slot();
		curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
		curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
		curl_easy_setopt(slot->curl, CURLOPT_URL, lock->url);
		curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_LOCK);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

		if (start_active_slot(slot)) {
			run_active_slot(slot);
			if (slot->curl_result != CURLE_OK) {
				fprintf(stderr, "Got HTTP error %ld\n", slot->http_code);
				lock->active = 0;
			} else {
				lock->active = 1;
				lock->start_time = time(NULL);
			}
		}

		lock->refreshing = 0;
		curl_slist_free_all(dav_headers);
		free(if_header);
	}

	if (check_lock)
		return check_lock->active;
	else
		return 0;
}

static void release_request(struct transfer_request *request)
{
	struct transfer_request *entry = request_queue_head;

	if (request == request_queue_head) {
		request_queue_head = request->next;
	} else {
		while (entry->next != NULL && entry->next != request)
			entry = entry->next;
		if (entry->next == request)
			entry->next = entry->next->next;
	}

	if (request->url != NULL)
		free(request->url);
	free(request);
}

static void finish_request(struct transfer_request *request)
{
	request->curl_result =	request->slot->curl_result;
	request->http_code = request->slot->http_code;
	request->slot = NULL;

	/* Keep locks active */
	refresh_lock(request->lock);

	if (request->headers != NULL)
		curl_slist_free_all(request->headers);

	/* URL is reused for MOVE after PUT */
	if (request->state != RUN_PUT) {
		free(request->url);
		request->url = NULL;
	}

	if (request->state == RUN_MKCOL) {
		if (request->curl_result == CURLE_OK ||
		    request->http_code == 405) {
			remote_dir_exists[request->obj->sha1[0]] = 1;
			start_put(request);
		} else {
			fprintf(stderr, "MKCOL %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->obj->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_PUT) {
		if (request->curl_result == CURLE_OK) {
			start_move(request);
		} else {
			fprintf(stderr,	"PUT %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->obj->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_MOVE) {
		if (request->curl_result == CURLE_OK) {
			fprintf(stderr, "    sent %s\n",
				sha1_to_hex(request->obj->sha1));
			request->state = COMPLETE;
			request->obj->flags |= REMOTE;
			release_request(request);
		} else {
			fprintf(stderr, "MOVE %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->obj->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	}
}

void fill_active_slots(void)
{
	struct transfer_request *request = request_queue_head;
	struct active_request_slot *slot = active_queue_head;
	int num_transfers;

	if (aborted)
		return;

	while (active_requests < max_requests && request != NULL) {
		if (pushing && request->state == NEED_PUSH) {
			if (remote_dir_exists[request->obj->sha1[0]] == 1) {
				start_put(request);
			} else {
				start_mkcol(request);
			}
			curl_multi_perform(curlm, &num_transfers);
		}
		request = request->next;
	}

	while (slot != NULL) {
		if (!slot->in_use && slot->curl != NULL) {
			curl_easy_cleanup(slot->curl);
			slot->curl = NULL;
		}
		slot = slot->next;
	}
}

static void get_remote_object_list(unsigned char parent);

static void add_request(struct object *obj, struct remote_lock *lock)
{
	struct transfer_request *request = request_queue_head;
	struct packed_git *target;

	/*
	 * Don't push the object if it's known to exist on the remote
	 * or is already in the request queue
	 */
	if (remote_dir_exists[obj->sha1[0]] == -1)
		get_remote_object_list(obj->sha1[0]);
	if (obj->flags & (REMOTE | PUSHING))
		return;
	target = find_sha1_pack(obj->sha1, remote->packs);
	if (target) {
		obj->flags |= REMOTE;
		return;
	}

	obj->flags |= PUSHING;
	request = xmalloc(sizeof(*request));
	request->obj = obj;
	request->url = NULL;
	request->lock = lock;
	request->headers = NULL;
	request->state = NEED_PUSH;
	request->next = request_queue_head;
	request_queue_head = request;

	fill_active_slots();
	step_active_slots();
}

static int fetch_index(unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	char *filename;
	char *url;
	char tmpfile[PATH_MAX];
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;

	FILE *indexfile;
	struct active_request_slot *slot;

	/* Don't use the index if the pack isn't there */
	url = xmalloc(strlen(remote->url) + 65);
	sprintf(url, "%s/objects/pack/pack-%s.pack", remote->url, hex);
	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 1);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK) {
			free(url);
			return error("Unable to verify pack %s is available",
				     hex);
		}
	} else {
		return error("Unable to start request");
	}

	if (has_pack_index(sha1))
		return 0;

	if (push_verbosely)
		fprintf(stderr, "Getting index for pack %s\n", hex);
	
	sprintf(url, "%s/objects/pack/pack-%s.idx", remote->url, hex);
	
	filename = sha1_pack_index_name(sha1);
	snprintf(tmpfile, sizeof(tmpfile), "%s.temp", filename);
	indexfile = fopen(tmpfile, "a");
	if (!indexfile)
		return error("Unable to open local file %s for pack index",
			     filename);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, indexfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);
	slot->local = indexfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(indexfile);
	if (prev_posn>0) {
		if (push_verbosely)
			fprintf(stderr,
				"Resuming fetch of index for pack %s at byte %ld\n",
				hex, prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, range_header);
	}

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK) {
			free(url);
			fclose(indexfile);
			return error("Unable to get pack index %s\n%s", url,
				     curl_errorstr);
		}
	} else {
		free(url);
		fclose(indexfile);
		return error("Unable to start request");
	}

	free(url);
	fclose(indexfile);

	return move_temp_to_file(tmpfile, filename);
}

static int setup_index(unsigned char *sha1)
{
	struct packed_git *new_pack;

	if (fetch_index(sha1))
		return -1;

	new_pack = parse_pack_index(sha1);
	new_pack->next = remote->packs;
	remote->packs = new_pack;
	return 0;
}

static int fetch_indices(void)
{
	unsigned char sha1[20];
	char *url;
	struct buffer buffer;
	char *data;
	int i = 0;

	struct active_request_slot *slot;

	data = xmalloc(4096);
	memset(data, 0, 4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (push_verbosely)
		fprintf(stderr, "Getting pack list\n");
	
	url = xmalloc(strlen(remote->url) + 21);
	sprintf(url, "%s/objects/info/packs", remote->url);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK) {
			free(buffer.buffer);
			free(url);
			if (slot->http_code == 404)
				return 0;
			else
				return error("%s", curl_errorstr);
		}
	} else {
		free(buffer.buffer);
		free(url);
		return error("Unable to start request");
	}
	free(url);

	data = buffer.buffer;
	while (i < buffer.posn) {
		switch (data[i]) {
		case 'P':
			i++;
			if (i + 52 < buffer.posn &&
			    !strncmp(data + i, " pack-", 6) &&
			    !strncmp(data + i + 46, ".pack\n", 6)) {
				get_sha1_hex(data + i + 6, sha1);
				setup_index(sha1);
				i += 51;
				break;
			}
		default:
			while (data[i] != '\n')
				i++;
		}
		i++;
	}

	free(buffer.buffer);
	return 0;
}

static inline int needs_quote(int ch)
{
	switch (ch) {
	case '/': case '-': case '.':
	case 'A'...'Z':	case 'a'...'z':	case '0'...'9':
		return 0;
	default:
		return 1;
	}
}

static inline int hex(int v)
{
	if (v < 10) return '0' + v;
	else return 'A' + v - 10;
}

static char *quote_ref_url(const char *base, const char *ref)
{
	const char *cp;
	char *dp, *qref;
	int len, baselen, ch;

	baselen = strlen(base);
	len = baselen + 1;
	for (cp = ref; (ch = *cp) != 0; cp++, len++)
		if (needs_quote(ch))
			len += 2; /* extra two hex plus replacement % */
	qref = xmalloc(len);
	memcpy(qref, base, baselen);
	for (cp = ref, dp = qref + baselen; (ch = *cp) != 0; cp++) {
		if (needs_quote(ch)) {
			*dp++ = '%';
			*dp++ = hex((ch >> 4) & 0xF);
			*dp++ = hex(ch & 0xF);
		}
		else
			*dp++ = ch;
	}
	*dp = 0;

	return qref;
}

int fetch_ref(char *ref, unsigned char *sha1)
{
        char *url;
        char hex[42];
        struct buffer buffer;
	char *base = remote->url;
	struct active_request_slot *slot;
        buffer.size = 41;
        buffer.posn = 0;
        buffer.buffer = hex;
        hex[41] = '\0';
        
	url = quote_ref_url(base, ref);
	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK)
			return error("Couldn't get %s for %s\n%s",
				     url, ref, curl_errorstr);
	} else {
		return error("Unable to start request");
	}

        hex[40] = '\0';
        get_sha1_hex(hex, sha1);
        return 0;
}

static void one_remote_object(const char *hex)
{
	unsigned char sha1[20];
	struct object *obj;

	if (get_sha1_hex(hex, sha1) != 0)
		return;

	obj = lookup_object(sha1);
	if (!obj)
		obj = parse_object(sha1);

	/* Ignore remote objects that don't exist locally */
	if (!obj)
		return;

	obj->flags |= REMOTE;
	if (!object_list_contains(objects, obj))
		add_object(obj, &objects, NULL, "");
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

	if (tag_closed && ctx->cdata) {
		if (!strcmp(ctx->name, DAV_ACTIVELOCK_OWNER)) {
			lock->owner = xmalloc(strlen(ctx->cdata) + 1);
			strcpy(lock->owner, ctx->cdata);
		} else if (!strcmp(ctx->name, DAV_ACTIVELOCK_TIMEOUT)) {
			if (!strncmp(ctx->cdata, "Second-", 7))
				lock->timeout =
					strtol(ctx->cdata + 7, NULL, 10);
		} else if (!strcmp(ctx->name, DAV_ACTIVELOCK_TOKEN)) {
			if (!strncmp(ctx->cdata, "opaquelocktoken:", 16)) {
				lock->token = xmalloc(strlen(ctx->cdata) - 15);
				strcpy(lock->token, ctx->cdata + 16);
			}
		}
	}
}

static void one_remote_ref(char *refname);
static void crawl_remote_refs(char *path);

static void handle_crawl_ref_ctx(struct xml_ctx *ctx, int tag_closed)
{
	struct remote_dentry *dentry = (struct remote_dentry *)ctx->userData;


	if (tag_closed) {
		if (!strcmp(ctx->name, DAV_PROPFIND_RESP) && dentry->name) {
			if (dentry->is_dir) {
				if (strcmp(dentry->name, dentry->base)) {
					crawl_remote_refs(dentry->name);
				}
			} else {
				one_remote_ref(dentry->name);
			}
		} else if (!strcmp(ctx->name, DAV_PROPFIND_NAME) && ctx->cdata) {
			dentry->name = xmalloc(strlen(ctx->cdata) -
					       remote->path_len + 1);
			strcpy(dentry->name,
			       ctx->cdata + remote->path_len);
		} else if (!strcmp(ctx->name, DAV_PROPFIND_COLLECTION)) {
			dentry->is_dir = 1;
		}
	} else if (!strcmp(ctx->name, DAV_PROPFIND_RESP)) {
		dentry->name = NULL;
		dentry->is_dir = 0;
	}
}

static void handle_remote_object_list_ctx(struct xml_ctx *ctx, int tag_closed)
{
	char *path;
	char *obj_hex;

	if (tag_closed) {
		if (!strcmp(ctx->name, DAV_PROPFIND_NAME) && ctx->cdata) {
			path = ctx->cdata + remote->path_len;
			if (strlen(path) != 50)
				return;
			path += 9;
			obj_hex = xmalloc(strlen(path));
			strncpy(obj_hex, path, 2);
			strcpy(obj_hex + 2, path + 3);
			one_remote_object(obj_hex);
			free(obj_hex);
		}
	}
}

static void
xml_start_tag(void *userData, const char *name, const char **atts)
{
	struct xml_ctx *ctx = (struct xml_ctx *)userData;
	const char *c = index(name, ':');
	int new_len;

	if (c == NULL)
		c = name;
	else
		c++;

	new_len = strlen(ctx->name) + strlen(c) + 2;

	if (new_len > ctx->len) {
		ctx->name = xrealloc(ctx->name, new_len);
		ctx->len = new_len;
	}
	strcat(ctx->name, ".");
	strcat(ctx->name, c);

	if (ctx->cdata) {
		free(ctx->cdata);
		ctx->cdata = NULL;
	}

	ctx->userFunc(ctx, 0);
}

static void
xml_end_tag(void *userData, const char *name)
{
	struct xml_ctx *ctx = (struct xml_ctx *)userData;
	const char *c = index(name, ':');
	char *ep;

	ctx->userFunc(ctx, 1);

	if (c == NULL)
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
	if (ctx->cdata)
		free(ctx->cdata);
	ctx->cdata = xcalloc(len+1, 1);
	strncpy(ctx->cdata, s, len);
}

static struct remote_lock *lock_remote(char *path, long timeout)
{
	struct active_request_slot *slot;
	struct buffer out_buffer;
	struct buffer in_buffer;
	char *out_data;
	char *in_data;
	char *url;
	char *ep;
	char timeout_header[25];
	struct remote_lock *lock = remote_locks;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct curl_slist *dav_headers = NULL;
	struct xml_ctx ctx;

	url = xmalloc(strlen(remote->url) + strlen(path) + 1);
	sprintf(url, "%s%s", remote->url, path);

	/* Make sure the url is not already locked */
	while (lock && strcmp(lock->url, url)) {
		lock = lock->next;
	}
	if (lock) {
		free(url);
		if (refresh_lock(lock))
			return lock;
		else
			return NULL;
	}

	/* Make sure leading directories exist for the remote ref */
	ep = strchr(url + strlen(remote->url) + 11, '/');
	while (ep) {
		*ep = 0;
		slot = get_active_slot();
		curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
		curl_easy_setopt(slot->curl, CURLOPT_URL, url);
		curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_MKCOL);
		curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
		if (start_active_slot(slot)) {
			run_active_slot(slot);
			if (slot->curl_result != CURLE_OK &&
			    slot->http_code != 405) {
				fprintf(stderr,
					"Unable to create branch path %s\n",
					url);
				free(url);
				return NULL;
			}
		} else {
			fprintf(stderr, "Unable to start request\n");
			free(url);
			return NULL;
		}
		*ep = '/';
		ep = strchr(ep + 1, '/');
	}

	out_buffer.size = strlen(LOCK_REQUEST) + strlen(git_default_email) - 2;
	out_data = xmalloc(out_buffer.size + 1);
	snprintf(out_data, out_buffer.size + 1, LOCK_REQUEST, git_default_email);
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	in_buffer.size = 4096;
	in_data = xmalloc(in_buffer.size);
	in_buffer.posn = 0;
	in_buffer.buffer = in_data;

	sprintf(timeout_header, "Timeout: Second-%ld", timeout);
	dav_headers = curl_slist_append(dav_headers, timeout_header);
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &in_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_LOCK);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	lock = xcalloc(1, sizeof(*lock));
	lock->owner = NULL;
	lock->token = NULL;
	lock->timeout = -1;
	lock->refreshing = 0;

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result == CURLE_OK) {
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_new_lock_ctx;
			ctx.userData = lock;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			XML_SetCharacterDataHandler(parser, xml_cdata);
			result = XML_Parse(parser, in_buffer.buffer,
					   in_buffer.posn, 1);
			free(ctx.name);
			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
				lock->timeout = -1;
			}
		}
	} else {
		fprintf(stderr, "Unable to start request\n");
	}

	curl_slist_free_all(dav_headers);
	free(out_data);
	free(in_data);

	if (lock->token == NULL || lock->timeout <= 0) {
		if (lock->token != NULL)
			free(lock->token);
		if (lock->owner != NULL)
			free(lock->owner);
		free(url);
		free(lock);
		lock = NULL;
	} else {
		lock->url = url;
		lock->active = 1;
		lock->start_time = time(NULL);
		lock->next = remote_locks;
		remote_locks = lock;
	}

	return lock;
}

static int unlock_remote(struct remote_lock *lock)
{
	struct active_request_slot *slot;
	char *lock_token_header;
	struct curl_slist *dav_headers = NULL;
	int rc = 0;

	lock_token_header = xmalloc(strlen(lock->token) + 31);
	sprintf(lock_token_header, "Lock-Token: <opaquelocktoken:%s>",
		lock->token);
	dav_headers = curl_slist_append(dav_headers, lock_token_header);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_URL, lock->url);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_UNLOCK);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result == CURLE_OK)
			rc = 1;
		else
			fprintf(stderr, "Got HTTP error %ld\n",
				slot->http_code);
	} else {
		fprintf(stderr, "Unable to start request\n");
	}

	curl_slist_free_all(dav_headers);
	free(lock_token_header);

	lock->active = 0;

	return rc;
}

static void crawl_remote_refs(char *path)
{
	char *url;
	struct active_request_slot *slot;
	struct buffer in_buffer;
	struct buffer out_buffer;
	char *in_data;
	char *out_data;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct curl_slist *dav_headers = NULL;
	struct xml_ctx ctx;
	struct remote_dentry dentry;

	fprintf(stderr, "  %s\n", path);

	dentry.base = path;
	dentry.name = NULL;
	dentry.is_dir = 0;

	url = xmalloc(strlen(remote->url) + strlen(path) + 1);
	sprintf(url, "%s%s", remote->url, path);

	out_buffer.size = strlen(PROPFIND_ALL_REQUEST);
	out_data = xmalloc(out_buffer.size + 1);
	snprintf(out_data, out_buffer.size + 1, PROPFIND_ALL_REQUEST);
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	in_buffer.size = 4096;
	in_data = xmalloc(in_buffer.size);
	in_buffer.posn = 0;
	in_buffer.buffer = in_data;

	dav_headers = curl_slist_append(dav_headers, "Depth: 1");
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &in_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PROPFIND);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result == CURLE_OK) {
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_crawl_ref_ctx;
			ctx.userData = &dentry;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			XML_SetCharacterDataHandler(parser, xml_cdata);
			result = XML_Parse(parser, in_buffer.buffer,
					   in_buffer.posn, 1);
			free(ctx.name);

			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
			}
		}
	} else {
		fprintf(stderr, "Unable to start request\n");
	}

	free(url);
	free(out_data);
	free(in_buffer.buffer);
	curl_slist_free_all(dav_headers);
}

static void get_remote_object_list(unsigned char parent)
{
	char *url;
	struct active_request_slot *slot;
	struct buffer in_buffer;
	struct buffer out_buffer;
	char *in_data;
	char *out_data;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct curl_slist *dav_headers = NULL;
	struct xml_ctx ctx;
	char path[] = "/objects/XX/";
	static const char hex[] = "0123456789abcdef";
	unsigned int val = parent;

	path[9] = hex[val >> 4];
	path[10] = hex[val & 0xf];
	url = xmalloc(strlen(remote->url) + strlen(path) + 1);
	sprintf(url, "%s%s", remote->url, path);

	out_buffer.size = strlen(PROPFIND_ALL_REQUEST);
	out_data = xmalloc(out_buffer.size + 1);
	snprintf(out_data, out_buffer.size + 1, PROPFIND_ALL_REQUEST);
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	in_buffer.size = 4096;
	in_data = xmalloc(in_buffer.size);
	in_buffer.posn = 0;
	in_buffer.buffer = in_data;

	dav_headers = curl_slist_append(dav_headers, "Depth: 1");
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &in_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PROPFIND);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result == CURLE_OK) {
			remote_dir_exists[parent] = 1;
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_remote_object_list_ctx;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			XML_SetCharacterDataHandler(parser, xml_cdata);
			result = XML_Parse(parser, in_buffer.buffer,
					   in_buffer.posn, 1);
			free(ctx.name);

			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
			}
		} else {
			remote_dir_exists[parent] = 0;
		}
	} else {
		fprintf(stderr, "Unable to start request\n");
	}

	free(url);
	free(out_data);
	free(in_buffer.buffer);
	curl_slist_free_all(dav_headers);
}

static int locking_available(void)
{
	struct active_request_slot *slot;
	struct buffer in_buffer;
	struct buffer out_buffer;
	char *in_data;
	char *out_data;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct curl_slist *dav_headers = NULL;
	struct xml_ctx ctx;
	int lock_flags = 0;

	out_buffer.size =
		strlen(PROPFIND_SUPPORTEDLOCK_REQUEST) +
		strlen(remote->url) - 2;
	out_data = xmalloc(out_buffer.size + 1);
	snprintf(out_data, out_buffer.size + 1,
		 PROPFIND_SUPPORTEDLOCK_REQUEST, remote->url);
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	in_buffer.size = 4096;
	in_data = xmalloc(in_buffer.size);
	in_buffer.posn = 0;
	in_buffer.buffer = in_data;

	dav_headers = curl_slist_append(dav_headers, "Depth: 0");
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");
	
	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &in_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_URL, remote->url);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PROPFIND);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result == CURLE_OK) {
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_lockprop_ctx;
			ctx.userData = &lock_flags;
			XML_SetUserData(parser, &ctx);
			XML_SetElementHandler(parser, xml_start_tag,
					      xml_end_tag);
			result = XML_Parse(parser, in_buffer.buffer,
					   in_buffer.posn, 1);
			free(ctx.name);

			if (result != XML_STATUS_OK) {
				fprintf(stderr, "XML error: %s\n",
					XML_ErrorString(
						XML_GetErrorCode(parser)));
				lock_flags = 0;
			}
		}
	} else {
		fprintf(stderr, "Unable to start request\n");
	}

	free(out_data);
	free(in_buffer.buffer);
	curl_slist_free_all(dav_headers);

	return lock_flags;
}

static struct object_list **process_blob(struct blob *blob,
					 struct object_list **p,
					 struct name_path *path,
					 const char *name)
{
	struct object *obj = &blob->object;

	obj->flags |= LOCAL;

	if (obj->flags & (UNINTERESTING | SEEN))
		return p;

	obj->flags |= SEEN;
	return add_object(obj, p, path, name);
}

static struct object_list **process_tree(struct tree *tree,
					 struct object_list **p,
					 struct name_path *path,
					 const char *name)
{
	struct object *obj = &tree->object;
	struct tree_entry_list *entry;
	struct name_path me;

	obj->flags |= LOCAL;

	if (obj->flags & (UNINTERESTING | SEEN))
		return p;
	if (parse_tree(tree) < 0)
		die("bad tree object %s", sha1_to_hex(obj->sha1));

	obj->flags |= SEEN;
	p = add_object(obj, p, NULL, name);
	me.up = path;
	me.elem = name;
	me.elem_len = strlen(name);
	entry = tree->entries;
	tree->entries = NULL;
	while (entry) {
		struct tree_entry_list *next = entry->next;
		if (entry->directory)
			p = process_tree(entry->item.tree, p, &me, entry->name);
		else
			p = process_blob(entry->item.blob, p, &me, entry->name);
		free(entry);
		entry = next;
	}
	return p;
}

static void get_delta(struct rev_info *revs, struct remote_lock *lock)
{
	struct commit *commit;
	struct object_list **p = &objects, *pending;

	while ((commit = get_revision(revs)) != NULL) {
		p = process_tree(commit->tree, p, NULL, "");
		commit->object.flags |= LOCAL;
		if (!(commit->object.flags & UNINTERESTING))
			add_request(&commit->object, lock);
	}

	for (pending = revs->pending_objects; pending; pending = pending->next) {
		struct object *obj = pending->item;
		const char *name = pending->name;

		if (obj->flags & (UNINTERESTING | SEEN))
			continue;
		if (obj->type == tag_type) {
			obj->flags |= SEEN;
			p = add_object(obj, p, NULL, name);
			continue;
		}
		if (obj->type == tree_type) {
			p = process_tree((struct tree *)obj, p, NULL, name);
			continue;
		}
		if (obj->type == blob_type) {
			p = process_blob((struct blob *)obj, p, NULL, name);
			continue;
		}
		die("unknown pending object %s (%s)", sha1_to_hex(obj->sha1), name);
	}

	while (objects) {
		if (!(objects->item->flags & UNINTERESTING))
			add_request(objects->item, lock);
		objects = objects->next;
	}
}

static int update_remote(unsigned char *sha1, struct remote_lock *lock)
{
	struct active_request_slot *slot;
	char *out_data;
	char *if_header;
	struct buffer out_buffer;
	struct curl_slist *dav_headers = NULL;
	int i;

	if_header = xmalloc(strlen(lock->token) + 25);
	sprintf(if_header, "If: (<opaquelocktoken:%s>)", lock->token);
	dav_headers = curl_slist_append(dav_headers, if_header);

	out_buffer.size = 41;
	out_data = xmalloc(out_buffer.size + 1);
	i = snprintf(out_data, out_buffer.size + 1, "%s\n", sha1_to_hex(sha1));
	if (i != out_buffer.size) {
		fprintf(stderr, "Unable to initialize PUT request body\n");
		return 0;
	}
	out_buffer.posn = 0;
	out_buffer.buffer = out_data;

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PUT);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_PUT, 1);
	curl_easy_setopt(slot->curl, CURLOPT_URL, lock->url);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		free(out_data);
		free(if_header);
		if (slot->curl_result != CURLE_OK) {
			fprintf(stderr,
				"PUT error: curl result=%d, HTTP code=%ld\n",
				slot->curl_result, slot->http_code);
			/* We should attempt recovery? */
			return 0;
		}
	} else {
		free(out_data);
		free(if_header);
		fprintf(stderr, "Unable to start PUT request\n");
		return 0;
	}

	return 1;
}

static struct ref *local_refs, **local_tail;
static struct ref *remote_refs, **remote_tail;

static int one_local_ref(const char *refname, const unsigned char *sha1)
{
	struct ref *ref;
	int len = strlen(refname) + 1;
	ref = xcalloc(1, sizeof(*ref) + len);
	memcpy(ref->new_sha1, sha1, 20);
	memcpy(ref->name, refname, len);
	*local_tail = ref;
	local_tail = &ref->next;
	return 0;
}

static void one_remote_ref(char *refname)
{
	struct ref *ref;
	unsigned char remote_sha1[20];

	if (fetch_ref(refname, remote_sha1) != 0) {
		fprintf(stderr,
			"Unable to fetch ref %s from %s\n",
			refname, remote->url);
		return;
	}

	int len = strlen(refname) + 1;
	ref = xcalloc(1, sizeof(*ref) + len);
	memcpy(ref->old_sha1, remote_sha1, 20);
	memcpy(ref->name, refname, len);
	*remote_tail = ref;
	remote_tail = &ref->next;
}

static void get_local_heads(void)
{
	local_tail = &local_refs;
	for_each_ref(one_local_ref);
}

static void get_dav_remote_heads(void)
{
	remote_tail = &remote_refs;
	crawl_remote_refs("refs/");
}

static int is_zero_sha1(const unsigned char *sha1)
{
	int i;

	for (i = 0; i < 20; i++) {
		if (*sha1++)
			return 0;
	}
	return 1;
}

static void unmark_and_free(struct commit_list *list, unsigned int mark)
{
	while (list) {
		struct commit_list *temp = list;
		temp->item->object.flags &= ~mark;
		list = temp->next;
		free(temp);
	}
}

static int ref_newer(const unsigned char *new_sha1,
		     const unsigned char *old_sha1)
{
	struct object *o;
	struct commit *old, *new;
	struct commit_list *list, *used;
	int found = 0;

	/* Both new and old must be commit-ish and new is descendant of
	 * old.  Otherwise we require --force.
	 */
	o = deref_tag(parse_object(old_sha1), NULL, 0);
	if (!o || o->type != commit_type)
		return 0;
	old = (struct commit *) o;

	o = deref_tag(parse_object(new_sha1), NULL, 0);
	if (!o || o->type != commit_type)
		return 0;
	new = (struct commit *) o;

	if (parse_commit(new) < 0)
		return 0;

	used = list = NULL;
	commit_list_insert(new, &list);
	while (list) {
		new = pop_most_recent_commit(&list, TMP_MARK);
		commit_list_insert(new, &used);
		if (new == old) {
			found = 1;
			break;
		}
	}
	unmark_and_free(list, TMP_MARK);
	unmark_and_free(used, TMP_MARK);
	return found;
}

static void mark_edge_parents_uninteresting(struct commit *commit)
{
	struct commit_list *parents;

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;
		if (!(parent->object.flags & UNINTERESTING))
			continue;
		mark_tree_uninteresting(parent->tree);
	}
}

static void mark_edges_uninteresting(struct commit_list *list)
{
	for ( ; list; list = list->next) {
		struct commit *commit = list->item;

		if (commit->object.flags & UNINTERESTING) {
			mark_tree_uninteresting(commit->tree);
			continue;
		}
		mark_edge_parents_uninteresting(commit);
	}
}

int main(int argc, char **argv)
{
	struct transfer_request *request;
	struct transfer_request *next_request;
	int nr_refspec = 0;
	char **refspec = NULL;
	struct remote_lock *ref_lock;
	struct rev_info revs;
	int rc = 0;
	int i;

	setup_git_directory();
	setup_ident();

	remote = xmalloc(sizeof(*remote));
	remote->url = NULL;
	remote->path_len = 0;
	remote->packs = NULL;

	argv++;
	for (i = 1; i < argc; i++, argv++) {
		char *arg = *argv;

		if (*arg == '-') {
			if (!strcmp(arg, "--all")) {
				push_all = 1;
				continue;
			}
			if (!strcmp(arg, "--force")) {
				force_all = 1;
				continue;
			}
			if (!strcmp(arg, "--verbose")) {
				push_verbosely = 1;
				continue;
			}
			usage(http_push_usage);
		}
		if (!remote->url) {
			remote->url = arg;
			char *path = strstr(arg, "//");
			if (path) {
				path = index(path+2, '/');
				if (path)
					remote->path_len = strlen(path);
			}
			continue;
		}
		refspec = argv;
		nr_refspec = argc - i;
		break;
	}

	if (!remote->url)
		usage(http_push_usage);

	memset(remote_dir_exists, -1, 256);

	http_init();

	no_pragma_header = curl_slist_append(no_pragma_header, "Pragma:");
	default_headers = curl_slist_append(default_headers, "Range:");
	default_headers = curl_slist_append(default_headers, "Destination:");
	default_headers = curl_slist_append(default_headers, "If:");
	default_headers = curl_slist_append(default_headers,
					    "Pragma: no-cache");

	/* Verify DAV compliance/lock support */
	if (!locking_available()) {
		fprintf(stderr, "Error: no DAV locking support on remote repo %s\n", remote->url);
		rc = 1;
		goto cleanup;
	}

	/* Get a list of all local and remote heads to validate refspecs */
	get_local_heads();
	fprintf(stderr, "Fetching remote heads...\n");
	get_dav_remote_heads();

	/* match them up */
	if (!remote_tail)
		remote_tail = &remote_refs;
	if (match_refs(local_refs, remote_refs, &remote_tail,
		       nr_refspec, refspec, push_all))
		return -1;
	if (!remote_refs) {
		fprintf(stderr, "No refs in common and none specified; doing nothing.\n");
		return 0;
	}

	int ret = 0;
	int new_refs = 0;
	struct ref *ref;
	for (ref = remote_refs; ref; ref = ref->next) {
		char old_hex[60], *new_hex;
		if (!ref->peer_ref)
			continue;
		if (!memcmp(ref->old_sha1, ref->peer_ref->new_sha1, 20)) {
			if (push_verbosely || 1)
				fprintf(stderr, "'%s': up-to-date\n", ref->name);
			continue;
		}

		if (!force_all &&
		    !is_zero_sha1(ref->old_sha1) &&
		    !ref->force) {
			if (!has_sha1_file(ref->old_sha1) ||
			    !ref_newer(ref->peer_ref->new_sha1,
				       ref->old_sha1)) {
				/* We do not have the remote ref, or
				 * we know that the remote ref is not
				 * an ancestor of what we are trying to
				 * push.  Either way this can be losing
				 * commits at the remote end and likely
				 * we were not up to date to begin with.
				 */
				error("remote '%s' is not a strict "
				      "subset of local ref '%s'. "
				      "maybe you are not up-to-date and "
				      "need to pull first?",
				      ref->name,
				      ref->peer_ref->name);
				ret = -2;
				continue;
			}
		}
		memcpy(ref->new_sha1, ref->peer_ref->new_sha1, 20);
		if (is_zero_sha1(ref->new_sha1)) {
			error("cannot happen anymore");
			ret = -3;
			continue;
		}
		new_refs++;
		strcpy(old_hex, sha1_to_hex(ref->old_sha1));
		new_hex = sha1_to_hex(ref->new_sha1);

		fprintf(stderr, "updating '%s'", ref->name);
		if (strcmp(ref->name, ref->peer_ref->name))
			fprintf(stderr, " using '%s'", ref->peer_ref->name);
		fprintf(stderr, "\n  from %s\n  to   %s\n", old_hex, new_hex);


		/* Lock remote branch ref */
		ref_lock = lock_remote(ref->name, LOCK_TIME);
		if (ref_lock == NULL) {
			fprintf(stderr, "Unable to lock remote branch %s\n",
				ref->name);
			rc = 1;
			continue;
		}

		/* Set up revision info for this refspec */
		const char *commit_argv[3];
		int commit_argc = 2;
		char *new_sha1_hex = strdup(sha1_to_hex(ref->new_sha1));
		char *old_sha1_hex = NULL;
		commit_argv[1] = new_sha1_hex;
		if (!push_all && !is_zero_sha1(ref->old_sha1)) {
			old_sha1_hex = xmalloc(42);
			sprintf(old_sha1_hex, "^%s",
				sha1_to_hex(ref->old_sha1));
			commit_argv[2] = old_sha1_hex;
			commit_argc++;
		}
		revs.commits = NULL;
		setup_revisions(commit_argc, commit_argv, &revs, NULL);
		revs.tag_objects = 1;
		revs.tree_objects = 1;
		revs.blob_objects = 1;
		free(new_sha1_hex);
		if (old_sha1_hex) {
			free(old_sha1_hex);
			commit_argv[1] = NULL;
		}

		/* Generate a list of objects that need to be pushed */
		pushing = 0;
		prepare_revision_walk(&revs);
		mark_edges_uninteresting(revs.commits);
		fetch_indices();
		get_delta(&revs, ref_lock);
		finish_all_active_slots();

		/* Push missing objects to remote, this would be a
		   convenient time to pack them first if appropriate. */
		pushing = 1;
		fill_active_slots();
		finish_all_active_slots();

		/* Update the remote branch if all went well */
		if (aborted || !update_remote(ref->new_sha1, ref_lock)) {
			rc = 1;
			goto unlock;
		}

	unlock:
		if (!rc)
			fprintf(stderr, "    done\n");
		unlock_remote(ref_lock);
	}

 cleanup:
	free(remote);

	curl_slist_free_all(no_pragma_header);
	curl_slist_free_all(default_headers);

	http_cleanup();

	request = request_queue_head;
	while (request != NULL) {
		next_request = request->next;
		release_request(request);
		request = next_request;
	}

	return rc;
}
