#include "cache.h"
#include "commit.h"
#include "pack.h"
#include "fetch.h"
#include "tag.h"
#include "blob.h"
#include "http.h"

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

/* DAV request body templates */
#define PROPFIND_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:propfind xmlns:D=\"DAV:\">\n<D:prop xmlns:R=\"%s\">\n<D:supportedlock/>\n</D:prop>\n</D:propfind>"
#define LOCK_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:lockinfo xmlns:D=\"DAV:\">\n<D:lockscope><D:exclusive/></D:lockscope>\n<D:locktype><D:write/></D:locktype>\n<D:owner>\n<D:href>mailto:%s</D:href>\n</D:owner>\n</D:lockinfo>"

#define LOCK_TIME 600
#define LOCK_REFRESH 30

static int pushing = 0;
static int aborted = 0;
static char remote_dir_exists[256];

static struct curl_slist *no_pragma_header;
static struct curl_slist *default_headers;

static int push_verbosely = 0;
static int push_all = 0;
static int force_all = 0;

struct repo
{
	char *url;
	struct packed_git *packs;
};

static struct repo *remote = NULL;

enum transfer_state {
	NEED_CHECK,
	RUN_HEAD,
	NEED_PUSH,
	RUN_MKCOL,
	RUN_PUT,
	RUN_MOVE,
	ABORTED,
	COMPLETE,
};

struct transfer_request
{
	unsigned char sha1[20];
	char *url;
	char *dest;
	struct active_lock *lock;
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

struct active_lock
{
	char *url;
	char *owner;
	char *token;
	time_t start_time;
	long timeout;
	int refreshing;
};

static void finish_request(struct transfer_request *request);

static void process_response(void *callback_data)
{
	struct transfer_request *request =
		(struct transfer_request *)callback_data;

	finish_request(request);
}

static void start_check(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->sha1);
	struct active_request_slot *slot;
	char *posn;

	request->url = xmalloc(strlen(remote->url) + 55);
	strcpy(request->url, remote->url);
	posn = request->url + strlen(remote->url);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	*(posn++) = '/';
	strcpy(posn, hex + 2);

	slot = get_active_slot();
	slot->callback_func = process_response;
	slot->callback_data = request;
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, request->errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_URL, request->url);
	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 1);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_HEAD;
	} else {
		request->state = ABORTED;
		free(request->url);
		request->url = NULL;
	}
}

static void start_mkcol(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->sha1);
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
	char *hex = sha1_to_hex(request->sha1);
	struct active_request_slot *slot;
	char *posn;
	char type[20];
	char hdr[50];
	void *unpacked;
	unsigned long len;
	int hdrlen;
	ssize_t size;
	z_stream stream;

	unpacked = read_sha1_file(request->sha1, type, &len);
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

static int refresh_lock(struct active_lock *lock)
{
	struct active_request_slot *slot;
	char *if_header;
	char timeout_header[25];
	struct curl_slist *dav_headers = NULL;
	int rc = 0;

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
		} else {
			lock->start_time = time(NULL);
			rc = 1;
		}
	}

	lock->refreshing = 0;
	curl_slist_free_all(dav_headers);
	free(if_header);

	return rc;
}

static void finish_request(struct transfer_request *request)
{
	time_t current_time = time(NULL);
	int time_remaining;

	request->curl_result =	request->slot->curl_result;
	request->http_code = request->slot->http_code;
	request->slot = NULL;

	/* Refresh the lock if it is close to timing out */
	time_remaining = request->lock->start_time + request->lock->timeout
		- current_time;
	if (time_remaining < LOCK_REFRESH && !request->lock->refreshing) {
		if (!refresh_lock(request->lock)) {
			fprintf(stderr, "Unable to refresh remote lock\n");
			aborted = 1;
		}
	}

	if (request->headers != NULL)
		curl_slist_free_all(request->headers);

	/* URL is reused for MOVE after PUT */
	if (request->state != RUN_PUT) {
		free(request->url);
		request->url = NULL;
	}		

	if (request->state == RUN_HEAD) {
		if (request->http_code == 404) {
			request->state = NEED_PUSH;
		} else if (request->curl_result == CURLE_OK) {
			remote_dir_exists[request->sha1[0]] = 1;
			request->state = COMPLETE;
		} else {
			fprintf(stderr, "HEAD %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_MKCOL) {
		if (request->curl_result == CURLE_OK ||
		    request->http_code == 405) {
			remote_dir_exists[request->sha1[0]] = 1;
			start_put(request);
		} else {
			fprintf(stderr, "MKCOL %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_PUT) {
		if (request->curl_result == CURLE_OK) {
			start_move(request);
		} else {
			fprintf(stderr,	"PUT %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	} else if (request->state == RUN_MOVE) {
		if (request->curl_result == CURLE_OK) {
			if (push_verbosely)
				fprintf(stderr,
					"sent %s\n",
					sha1_to_hex(request->sha1));
			request->state = COMPLETE;
		} else {
			fprintf(stderr, "MOVE %s failed, aborting (%d/%ld)\n",
				sha1_to_hex(request->sha1),
				request->curl_result, request->http_code);
			request->state = ABORTED;
			aborted = 1;
		}
	}
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

void fill_active_slots(void)
{
	struct transfer_request *request = request_queue_head;
	struct active_request_slot *slot = active_queue_head;
	int num_transfers;

	if (aborted)
		return;

	while (active_requests < max_requests && request != NULL) {
		if (!pushing && request->state == NEED_CHECK) {
			start_check(request);
			curl_multi_perform(curlm, &num_transfers);
		} else if (pushing && request->state == NEED_PUSH) {
			if (remote_dir_exists[request->sha1[0]])
				start_put(request);
			else
				start_mkcol(request);
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

static void add_request(unsigned char *sha1, struct active_lock *lock)
{
	struct transfer_request *request = request_queue_head;
	struct packed_git *target;
	
	while (request != NULL && memcmp(request->sha1, sha1, 20))
		request = request->next;
	if (request != NULL)
		return;

	target = find_sha1_pack(sha1, remote->packs);
	if (target)
		return;

	request = xmalloc(sizeof(*request));
	memcpy(request->sha1, sha1, 20);
	request->url = NULL;
	request->lock = lock;
	request->headers = NULL;
	request->state = NEED_CHECK;
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
	len = baselen + 12; /* "refs/heads/" + NUL */
	for (cp = ref; (ch = *cp) != 0; cp++, len++)
		if (needs_quote(ch))
			len += 2; /* extra two hex plus replacement % */
	qref = xmalloc(len);
	memcpy(qref, base, baselen);
	memcpy(qref + baselen, "refs/heads/", 11);
	for (cp = ref, dp = qref + baselen + 11; (ch = *cp) != 0; cp++) {
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
	struct active_lock *lock = (struct active_lock *)ctx->userData;

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

static struct active_lock *lock_remote(char *file, long timeout)
{
	struct active_request_slot *slot;
	struct buffer out_buffer;
	struct buffer in_buffer;
	char *out_data;
	char *in_data;
	char *url;
	char *ep;
	char timeout_header[25];
	struct active_lock *new_lock = NULL;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct curl_slist *dav_headers = NULL;
	struct xml_ctx ctx;

	url = xmalloc(strlen(remote->url) + strlen(file) + 1);
	sprintf(url, "%s%s", remote->url, file);

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

	new_lock = xcalloc(1, sizeof(*new_lock));
	new_lock->owner = NULL;
	new_lock->token = NULL;
	new_lock->timeout = -1;
	new_lock->refreshing = 0;

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result == CURLE_OK) {
			ctx.name = xcalloc(10, 1);
			ctx.len = 0;
			ctx.cdata = NULL;
			ctx.userFunc = handle_new_lock_ctx;
			ctx.userData = new_lock;
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
				new_lock->timeout = -1;
			}
		}
	} else {
		fprintf(stderr, "Unable to start request\n");
	}

	curl_slist_free_all(dav_headers);
	free(out_data);
	free(in_data);

	if (new_lock->token == NULL || new_lock->timeout <= 0) {
		if (new_lock->token != NULL)
			free(new_lock->token);
		if (new_lock->owner != NULL)
			free(new_lock->owner);
		free(url);
		free(new_lock);
		new_lock = NULL;
	} else {
		new_lock->url = url;
		new_lock->start_time = time(NULL);
	}

	return new_lock;
}

static int unlock_remote(struct active_lock *lock)
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

	if (lock->owner != NULL)
		free(lock->owner);
	free(lock->url);
	free(lock->token);
	free(lock);

	return rc;
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

	out_buffer.size = strlen(PROPFIND_REQUEST) + strlen(remote->url) - 2;
	out_data = xmalloc(out_buffer.size + 1);
	snprintf(out_data, out_buffer.size + 1, PROPFIND_REQUEST, remote->url);
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

static int is_ancestor(unsigned char *sha1, struct commit *commit)
{
	struct commit_list *parents;

	if (parse_commit(commit))
		return 0;
	parents = commit->parents;
	for (; parents; parents = parents->next) {
		if (!memcmp(sha1, parents->item->object.sha1, 20)) {
			return 1;
		} else if (parents->item->object.type == commit_type) {
			if (is_ancestor(
				    sha1,
				    (struct commit *)&parents->item->object
				    ))
				return 1;
		}
	}
	return 0;
}

static void get_delta(unsigned char *sha1, struct object *obj,
		      struct active_lock *lock)
{
	struct commit *commit;
	struct commit_list *parents;
	struct tree *tree;
	struct tree_entry_list *entry;

	if (sha1 && !memcmp(sha1, obj->sha1, 20))
		return;

	if (aborted)
		return;

	if (obj->type == commit_type) {
		if (push_verbosely)
			fprintf(stderr, "walk %s\n", sha1_to_hex(obj->sha1));
		add_request(obj->sha1, lock);
		commit = (struct commit *)obj;
		if (parse_commit(commit)) {
			fprintf(stderr, "Error parsing commit %s\n",
				sha1_to_hex(obj->sha1));
			aborted = 1;
			return;
		}
		parents = commit->parents;
		for (; parents; parents = parents->next)
			if (sha1 == NULL ||
			    memcmp(sha1, parents->item->object.sha1, 20))
				get_delta(sha1, &parents->item->object,
					  lock);
		get_delta(sha1, &commit->tree->object, lock);
	} else if (obj->type == tree_type) {
		if (push_verbosely)
			fprintf(stderr, "walk %s\n", sha1_to_hex(obj->sha1));
		add_request(obj->sha1, lock);
		tree = (struct tree *)obj;
		if (parse_tree(tree)) {
			fprintf(stderr, "Error parsing tree %s\n",
				sha1_to_hex(obj->sha1));
			aborted = 1;
			return;
		}
		entry = tree->entries;
		tree->entries = NULL;
		while (entry) {
			struct tree_entry_list *next = entry->next;
			get_delta(sha1, entry->item.any, lock);
			free(entry->name);
			free(entry);
			entry = next;
		}
	} else if (obj->type == blob_type || obj->type == tag_type) {
		add_request(obj->sha1, lock);
	}
}

static int update_remote(unsigned char *sha1, struct active_lock *lock)
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

int main(int argc, char **argv)
{
	struct transfer_request *request;
	struct transfer_request *next_request;
	int nr_refspec = 0;
	char **refspec = NULL;
	int do_remote_update;
	int new_branch;
	int force_this;
	char *local_ref;
	unsigned char local_sha1[20];
	struct object *local_object = NULL;
	char *remote_ref = NULL;
	unsigned char remote_sha1[20];
	struct active_lock *remote_lock;
	char *remote_path = NULL;
	int rc = 0;
	int i;

	setup_git_directory();
	setup_ident();

	remote = xmalloc(sizeof(*remote));
	remote->url = NULL;
	remote->packs = NULL;

	argv++;
	for (i = 1; i < argc; i++, argv++) {
		char *arg = *argv;

		if (*arg == '-') {
			if (!strcmp(arg, "--complete")) {
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
			continue;
		}
		refspec = argv;
		nr_refspec = argc - i;
		break;
	}

	if (!remote->url)
		usage(http_push_usage);

	memset(remote_dir_exists, 0, 256);

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

	/* Process each refspec */
	for (i = 0; i < nr_refspec; i++) {
		char *ep;
		force_this = 0;
		do_remote_update = 0;
		new_branch = 0;
		local_ref = refspec[i];
		if (*local_ref == '+') {
			force_this = 1;
			local_ref++;
		}
		ep = strchr(local_ref, ':');
		if (ep) {
			remote_ref = ep + 1;
			*ep = 0;
		}
		else
			remote_ref = local_ref;

		/* Lock remote branch ref */
		if (remote_path)
			free(remote_path);
		remote_path = xmalloc(strlen(remote_ref) + 12);
		sprintf(remote_path, "refs/heads/%s", remote_ref);
		remote_lock = lock_remote(remote_path, LOCK_TIME);
		if (remote_lock == NULL) {
			fprintf(stderr, "Unable to lock remote branch %s\n",
				remote_ref);
			rc = 1;
			continue;
		}

		/* Resolve local and remote refs */
		if (fetch_ref(remote_ref, remote_sha1) != 0) {
			fprintf(stderr,
				"Remote branch %s does not exist on %s\n",
				remote_ref, remote->url);
			new_branch = 1;
		}
		if (get_sha1(local_ref, local_sha1) != 0) {
			fprintf(stderr, "Error resolving local branch %s\n",
				local_ref);
			rc = 1;
			goto unlock;
		}
	
		/* Find relationship between local and remote */
		local_object = parse_object(local_sha1);
		if (!local_object) {
			fprintf(stderr, "Unable to parse local object %s\n",
				sha1_to_hex(local_sha1));
			rc = 1;
			goto unlock;
		} else if (new_branch) {
			do_remote_update = 1;
		} else {
			if (!memcmp(local_sha1, remote_sha1, 20)) {
				fprintf(stderr,
					"* %s: same as branch '%s' of %s\n",
					local_ref, remote_ref, remote->url);
			} else if (is_ancestor(remote_sha1,
					       (struct commit *)local_object)) {
				fprintf(stderr,
					"Remote %s will fast-forward to local %s\n",
					remote_ref, local_ref);
				do_remote_update = 1;
			} else if (force_all || force_this) {
				fprintf(stderr,
					"* %s on %s does not fast forward to local branch '%s', overwriting\n",
					remote_ref, remote->url, local_ref);
				do_remote_update = 1;
			} else {
				fprintf(stderr,
					"* %s on %s does not fast forward to local branch '%s'\n",
					remote_ref, remote->url, local_ref);
				rc = 1;
				goto unlock;
			}
		}

		/* Generate and check list of required objects */
		pushing = 0;
		if (do_remote_update || push_all)
			fetch_indices();
		get_delta(push_all ? NULL : remote_sha1,
			  local_object, remote_lock);
		finish_all_active_slots();

		/* Push missing objects to remote, this would be a
		   convenient time to pack them first if appropriate. */
		pushing = 1;
		fill_active_slots();
		finish_all_active_slots();

		/* Update the remote branch if all went well */
		if (do_remote_update) {
			if (!aborted && update_remote(local_sha1,
						      remote_lock)) {
				fprintf(stderr, "%s remote branch %s\n",
					new_branch ? "Created" : "Updated",
					remote_ref);
			} else {
				fprintf(stderr,
					"Unable to %s remote branch %s\n",
					new_branch ? "create" : "update",
					remote_ref);
				rc = 1;
				goto unlock;
			}
		}

	unlock:
		unlock_remote(remote_lock);
		free(remote_path);
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
