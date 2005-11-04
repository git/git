#include "cache.h"
#include "commit.h"
#include "pack.h"
#include "fetch.h"
#include "tag.h"
#include "blob.h"

#include <curl/curl.h>
#include <curl/easy.h>
#include "expat.h"

static const char http_push_usage[] =
"git-http-push [--complete] [--force] [--verbose] <url> <ref> [<ref>...]\n";

#if LIBCURL_VERSION_NUM >= 0x070908
#define USE_CURL_MULTI
#define DEFAULT_MAX_REQUESTS 5
#endif

#if LIBCURL_VERSION_NUM < 0x070704
#define curl_global_cleanup() do { /* nothing */ } while(0)
#endif
#if LIBCURL_VERSION_NUM < 0x070800
#define curl_global_init(a) do { /* nothing */ } while(0)
#endif

#if LIBCURL_VERSION_NUM < 0x070c04
#define NO_CURL_EASY_DUPHANDLE
#endif

#define RANGE_HEADER_SIZE 30

/* DAV method names and request body templates */
#define DAV_LOCK "LOCK"
#define DAV_MKCOL "MKCOL"
#define DAV_MOVE "MOVE"
#define DAV_PROPFIND "PROPFIND"
#define DAV_PUT "PUT"
#define DAV_UNLOCK "UNLOCK"
#define PROPFIND_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:propfind xmlns:D=\"DAV:\">\n<D:prop xmlns:R=\"%s\">\n<D:supportedlock/>\n</D:prop>\n</D:propfind>"
#define LOCK_REQUEST "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:lockinfo xmlns:D=\"DAV:\">\n<D:lockscope><D:exclusive/></D:lockscope>\n<D:locktype><D:write/></D:locktype>\n<D:owner>\n<D:href>mailto:%s</D:href>\n</D:owner>\n</D:lockinfo>"

static int active_requests = 0;
static int data_received;
static int pushing = 0;
static int aborted = 0;

#ifdef USE_CURL_MULTI
static int max_requests = -1;
static CURLM *curlm;
#endif
#ifndef NO_CURL_EASY_DUPHANDLE
static CURL *curl_default;
#endif
static struct curl_slist *no_pragma_header;
static struct curl_slist *default_headers;
static char curl_errorstr[CURL_ERROR_SIZE];

static int push_verbosely = 0;
static int push_all = 0;
static int force_all = 0;

struct buffer
{
        size_t posn;
        size_t size;
        void *buffer;
};

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

struct active_request_slot
{
	CURL *curl;
	FILE *local;
	int in_use;
	int done;
	CURLcode curl_result;
	long http_code;
	struct active_request_slot *next;
};

static struct transfer_request *request_queue_head = NULL;
static struct active_request_slot *active_queue_head = NULL;

static int curl_ssl_verify = -1;
static char *ssl_cert = NULL;
#if LIBCURL_VERSION_NUM >= 0x070902
static char *ssl_key = NULL;
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
static char *ssl_capath = NULL;
#endif
static char *ssl_cainfo = NULL;
static long curl_low_speed_limit = -1;
static long curl_low_speed_time = -1;

struct active_lock
{
	int ctx_activelock;
	int ctx_owner;
	int ctx_owner_href;
	int ctx_timeout;
	int ctx_locktoken;
	int ctx_locktoken_href;
	char *owner;
	time_t start_time;
	long timeout;
	char *token;
};

struct lockprop
{
	int supported_lock;
	int lock_entry;
	int lock_scope;
	int lock_type;
	int lock_exclusive;
	int lock_exclusive_write;
};

static int http_options(const char *var, const char *value)
{
	if (!strcmp("http.sslverify", var)) {
		if (curl_ssl_verify == -1) {
			curl_ssl_verify = git_config_bool(var, value);
		}
		return 0;
	}

	if (!strcmp("http.sslcert", var)) {
		if (ssl_cert == NULL) {
			ssl_cert = xmalloc(strlen(value)+1);
			strcpy(ssl_cert, value);
		}
		return 0;
	}
#if LIBCURL_VERSION_NUM >= 0x070902
	if (!strcmp("http.sslkey", var)) {
		if (ssl_key == NULL) {
			ssl_key = xmalloc(strlen(value)+1);
			strcpy(ssl_key, value);
		}
		return 0;
	}
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
	if (!strcmp("http.sslcapath", var)) {
		if (ssl_capath == NULL) {
			ssl_capath = xmalloc(strlen(value)+1);
			strcpy(ssl_capath, value);
		}
		return 0;
	}
#endif
	if (!strcmp("http.sslcainfo", var)) {
		if (ssl_cainfo == NULL) {
			ssl_cainfo = xmalloc(strlen(value)+1);
			strcpy(ssl_cainfo, value);
		}
		return 0;
	}

#ifdef USE_CURL_MULTI	
	if (!strcmp("http.maxrequests", var)) {
		if (max_requests == -1)
			max_requests = git_config_int(var, value);
		return 0;
	}
#endif

	if (!strcmp("http.lowspeedlimit", var)) {
		if (curl_low_speed_limit == -1)
			curl_low_speed_limit = (long)git_config_int(var, value);
		return 0;
	}
	if (!strcmp("http.lowspeedtime", var)) {
		if (curl_low_speed_time == -1)
			curl_low_speed_time = (long)git_config_int(var, value);
		return 0;
	}

	/* Fall back on the default ones */
	return git_default_config(var, value);
}

static size_t fread_buffer(void *ptr, size_t eltsize, size_t nmemb,
			   struct buffer *buffer)
{
	size_t size = eltsize * nmemb;
	if (size > buffer->size - buffer->posn)
		size = buffer->size - buffer->posn;
	memcpy(ptr, buffer->buffer + buffer->posn, size);
	buffer->posn += size;
	return size;
}

static size_t fwrite_buffer_dynamic(const void *ptr, size_t eltsize,
				    size_t nmemb, struct buffer *buffer)
{
	size_t size = eltsize * nmemb;
	if (size > buffer->size - buffer->posn) {
		buffer->size = buffer->size * 3 / 2;
		if (buffer->size < buffer->posn + size)
			buffer->size = buffer->posn + size;
		buffer->buffer = xrealloc(buffer->buffer, buffer->size);
	}
	memcpy(buffer->buffer + buffer->posn, ptr, size);
	buffer->posn += size;
	data_received++;
	return size;
}

static size_t fwrite_null(const void *ptr, size_t eltsize,
			  size_t nmemb, struct buffer *buffer)
{
	data_received++;
	return eltsize * nmemb;
}

#ifdef USE_CURL_MULTI
static void process_curl_messages(void);
static void process_request_queue(void);
#endif

static CURL* get_curl_handle(void)
{
	CURL* result = curl_easy_init();

	curl_easy_setopt(result, CURLOPT_SSL_VERIFYPEER, curl_ssl_verify);
#if LIBCURL_VERSION_NUM >= 0x070907
	curl_easy_setopt(result, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
#endif

	if (ssl_cert != NULL)
		curl_easy_setopt(result, CURLOPT_SSLCERT, ssl_cert);
#if LIBCURL_VERSION_NUM >= 0x070902
	if (ssl_key != NULL)
		curl_easy_setopt(result, CURLOPT_SSLKEY, ssl_key);
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
	if (ssl_capath != NULL)
		curl_easy_setopt(result, CURLOPT_CAPATH, ssl_capath);
#endif
	if (ssl_cainfo != NULL)
		curl_easy_setopt(result, CURLOPT_CAINFO, ssl_cainfo);
	curl_easy_setopt(result, CURLOPT_FAILONERROR, 1);

	if (curl_low_speed_limit > 0 && curl_low_speed_time > 0) {
		curl_easy_setopt(result, CURLOPT_LOW_SPEED_LIMIT,
				 curl_low_speed_limit);
		curl_easy_setopt(result, CURLOPT_LOW_SPEED_TIME,
				 curl_low_speed_time);
	}

	return result;
}

static struct active_request_slot *get_active_slot(void)
{
	struct active_request_slot *slot = active_queue_head;
	struct active_request_slot *newslot;

#ifdef USE_CURL_MULTI
	int num_transfers;

	/* Wait for a slot to open up if the queue is full */
	while (active_requests >= max_requests) {
		curl_multi_perform(curlm, &num_transfers);
		if (num_transfers < active_requests) {
			process_curl_messages();
		}
	}
#endif

	while (slot != NULL && slot->in_use) {
		slot = slot->next;
	}
	if (slot == NULL) {
		newslot = xmalloc(sizeof(*newslot));
		newslot->curl = NULL;
		newslot->in_use = 0;
		newslot->next = NULL;

		slot = active_queue_head;
		if (slot == NULL) {
			active_queue_head = newslot;
		} else {
			while (slot->next != NULL) {
				slot = slot->next;
			}
			slot->next = newslot;
		}
		slot = newslot;
	}

	if (slot->curl == NULL) {
#ifdef NO_CURL_EASY_DUPHANDLE
		slot->curl = get_curl_handle();
#else
		slot->curl = curl_easy_duphandle(curl_default);
#endif
	}

	active_requests++;
	slot->in_use = 1;
	slot->done = 0;
	slot->local = NULL;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, default_headers);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, curl_errorstr);

	return slot;
}

static int start_active_slot(struct active_request_slot *slot)
{
#ifdef USE_CURL_MULTI
	CURLMcode curlm_result = curl_multi_add_handle(curlm, slot->curl);

	if (curlm_result != CURLM_OK &&
	    curlm_result != CURLM_CALL_MULTI_PERFORM) {
		active_requests--;
		slot->in_use = 0;
		return 0;
	}
#endif
	return 1;
}

static void run_active_slot(struct active_request_slot *slot)
{
#ifdef USE_CURL_MULTI
	int num_transfers;
	long last_pos = 0;
	long current_pos;
	fd_set readfds;
	fd_set writefds;
	fd_set excfds;
	int max_fd;
	struct timeval select_timeout;
	CURLMcode curlm_result;

	while (!slot->done) {
		data_received = 0;
		do {
			curlm_result = curl_multi_perform(curlm,
							  &num_transfers);
		} while (curlm_result == CURLM_CALL_MULTI_PERFORM);
		if (num_transfers < active_requests) {
			process_curl_messages();
			process_request_queue();
		}

		if (!data_received && slot->local != NULL) {
			current_pos = ftell(slot->local);
			if (current_pos > last_pos)
				data_received++;
			last_pos = current_pos;
		}

		if (!slot->done && !data_received) {
			max_fd = 0;
			FD_ZERO(&readfds);
			FD_ZERO(&writefds);
			FD_ZERO(&excfds);
			select_timeout.tv_sec = 0;
			select_timeout.tv_usec = 50000;
			select(max_fd, &readfds, &writefds,
			       &excfds, &select_timeout);
		}
	}
#else
	slot->curl_result = curl_easy_perform(slot->curl);
	active_requests--;
#endif
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
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, request->errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_URL, request->url);
	curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 1);

	if (start_active_slot(slot)) {
		request->slot = slot;
		request->state = RUN_HEAD;
	} else {
		request->state = ABORTED;
		free(request->url);
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

	if (request->url != NULL)
		free(request->url);
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
	}
}

static void start_move(struct transfer_request *request)
{
	struct active_request_slot *slot;
	struct curl_slist *dav_headers = NULL;

	slot = get_active_slot();
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
	}
}

static void finish_request(struct transfer_request *request)
{
	request->curl_result =	request->slot->curl_result;
	request->http_code = request->slot->http_code;
	request->slot = NULL;
	if (request->headers != NULL)
		curl_slist_free_all(request->headers);
	if (request->state == RUN_HEAD) {
		if (request->http_code == 404) {
			request->state = NEED_PUSH;
		} else if (request->curl_result == CURLE_OK) {
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

	free(request->url);
	free(request);
}

#ifdef USE_CURL_MULTI
void process_curl_messages(void)
{
	int num_messages;
	struct active_request_slot *slot;
	struct transfer_request *request = NULL;
	CURLMsg *curl_message = curl_multi_info_read(curlm, &num_messages);

	while (curl_message != NULL) {
		if (curl_message->msg == CURLMSG_DONE) {
			slot = active_queue_head;
			while (slot != NULL &&
			       slot->curl != curl_message->easy_handle)
				slot = slot->next;
			if (slot != NULL) {
				curl_multi_remove_handle(curlm, slot->curl);
				active_requests--;
				slot->done = 1;
				slot->in_use = 0;
				slot->curl_result = curl_message->data.result;
				curl_easy_getinfo(slot->curl,
						  CURLINFO_HTTP_CODE,
						  &slot->http_code);
				request = request_queue_head;
				while (request != NULL &&
				       request->slot != slot)
					request = request->next;
				if (request != NULL)
					finish_request(request);
			} else {
				fprintf(stderr, "Received DONE message for unknown request!\n");
			}
		} else {
			fprintf(stderr, "Unknown CURL message received: %d\n",
				(int)curl_message->msg);
		}
		curl_message = curl_multi_info_read(curlm, &num_messages);
	}
}

void process_request_queue(void)
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
#endif

void process_waiting_requests(void)
{
	struct active_request_slot *slot = active_queue_head;

	while (slot != NULL)
		if (slot->in_use) {
			run_active_slot(slot);
			slot = active_queue_head;
		} else {
			slot = slot->next;
		}
}

void add_request(unsigned char *sha1, struct active_lock *lock)
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
#ifdef USE_CURL_MULTI
	process_request_queue();
	process_curl_messages();
#endif
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

static int fetch_indices()
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
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
			 fwrite_buffer_dynamic);
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
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
			 fwrite_buffer_dynamic);
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

static void
start_activelock_element(void *userData, const char *name, const char **atts)
{
	struct active_lock *lock = (struct active_lock *)userData;

	if (lock->ctx_activelock && !strcmp(name, "D:timeout"))
		lock->ctx_timeout = 1;
	else if (lock->ctx_owner && strstr(name, "href"))
		lock->ctx_owner_href = 1;
	else if (lock->ctx_activelock && strstr(name, "owner"))
		lock->ctx_owner = 1;
	else if (lock->ctx_locktoken && !strcmp(name, "D:href"))
		lock->ctx_locktoken_href = 1;
	else if (lock->ctx_activelock && !strcmp(name, "D:locktoken"))
		lock->ctx_locktoken = 1;
	else if (!strcmp(name, "D:activelock"))
		lock->ctx_activelock = 1;
}

static void
end_activelock_element(void *userData, const char *name)
{
	struct active_lock *lock = (struct active_lock *)userData;

	if (lock->ctx_timeout && !strcmp(name, "D:timeout")) {
		lock->ctx_timeout = 0;
	} else if (lock->ctx_owner_href && strstr(name, "href")) {
		lock->ctx_owner_href = 0;
	} else if (lock->ctx_owner && strstr(name, "owner")) {
		lock->ctx_owner = 0;
	} else if (lock->ctx_locktoken_href && !strcmp(name, "D:href")) {
		lock->ctx_locktoken_href = 0;
	} else if (lock->ctx_locktoken && !strcmp(name, "D:locktoken")) {
		lock->ctx_locktoken = 0;
	} else if (lock->ctx_activelock && !strcmp(name, "D:activelock")) {
		lock->ctx_activelock = 0;
	}
}

static void
activelock_cdata(void *userData, const XML_Char *s, int len)
{
	struct active_lock *lock = (struct active_lock *)userData;
	char *this = malloc(len+1);
	strncpy(this, s, len);

	if (lock->ctx_owner_href) {
		lock->owner = malloc(len+1);
		strcpy(lock->owner, this);
	} else if (lock->ctx_locktoken_href) {
		if (!strncmp(this, "opaquelocktoken:", 16)) {
			lock->token = malloc(len-15);
			strcpy(lock->token, this+16);
		}
	} else if (lock->ctx_timeout) {
		if (!strncmp(this, "Second-", 7))
			lock->timeout = strtol(this+7, NULL, 10);
	}

	free(this);
}

static void
start_lockprop_element(void *userData, const char *name, const char **atts)
{
	struct lockprop *prop = (struct lockprop *)userData;

	if (prop->lock_type && !strcmp(name, "D:write")) {
		if (prop->lock_exclusive) {
			prop->lock_exclusive_write = 1;
		}
	} else if (prop->lock_scope && !strcmp(name, "D:exclusive")) {
		prop->lock_exclusive = 1;
	} else if (prop->lock_entry) {
		if (!strcmp(name, "D:lockscope")) {
			prop->lock_scope = 1;
		} else if (!strcmp(name, "D:locktype")) {
			prop->lock_type = 1;
		}
	} else if (prop->supported_lock) {
		if (!strcmp(name, "D:lockentry")) {
			prop->lock_entry = 1;
		}
	} else if (!strcmp(name, "D:supportedlock")) {
		prop->supported_lock = 1;
	}
}

static void
end_lockprop_element(void *userData, const char *name)
{
	struct lockprop *prop = (struct lockprop *)userData;

	if (!strcmp(name, "D:lockentry")) {
		prop->lock_entry = 0;
		prop->lock_scope = 0;
		prop->lock_type = 0;
		prop->lock_exclusive = 0;
	} else if (!strcmp(name, "D:supportedlock")) {
		prop->supported_lock = 0;
	}
}

struct active_lock *lock_remote(char *file, int timeout)
{
	struct active_request_slot *slot;
	struct buffer out_buffer;
	struct buffer in_buffer;
	char *out_data;
	char *in_data;
	char *url;
	char *ep;
	char timeout_header[25];
	struct active_lock *new_lock;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct curl_slist *dav_headers = NULL;

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

	new_lock = xmalloc(sizeof(*new_lock));
	new_lock->owner = NULL;
	new_lock->token = NULL;
	new_lock->timeout = -1;

	sprintf(timeout_header, "Timeout: Second-%d", timeout);
	dav_headers = curl_slist_append(dav_headers, timeout_header);
	dav_headers = curl_slist_append(dav_headers, "Content-Type: text/xml");

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_INFILE, &out_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_INFILESIZE, out_buffer.size);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, fread_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &in_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
			 fwrite_buffer_dynamic);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_LOCK);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK) {
			fprintf(stderr, "Got HTTP error %ld\n", slot->http_code);
			free(new_lock);
			free(url);
			free(out_data);
			free(in_data);
			return NULL;
		}
	} else {
		free(new_lock);
		free(url);
		free(out_data);
		free(in_data);
		fprintf(stderr, "Unable to start request\n");
		return NULL;
	}

	free(url);
	free(out_data);

	XML_SetUserData(parser, new_lock);
	XML_SetElementHandler(parser, start_activelock_element,
				      end_activelock_element);
	XML_SetCharacterDataHandler(parser, activelock_cdata);
	result = XML_Parse(parser, in_buffer.buffer, in_buffer.posn, 1);
	free(in_data);
	if (result != XML_STATUS_OK) {
		fprintf(stderr, "%s", XML_ErrorString(
				XML_GetErrorCode(parser)));
		free(new_lock);
		return NULL;
	}

	if (new_lock->token == NULL || new_lock->timeout <= 0) {
		if (new_lock->token != NULL)
			free(new_lock->token);
		if (new_lock->owner != NULL)
			free(new_lock->owner);
		free(new_lock);
		return NULL;
	}

	new_lock->start_time = time(NULL);
	return new_lock;
}

int unlock_remote(char *file, struct active_lock *lock)
{
	struct active_request_slot *slot;
	char *url;
	char *lock_token_header;
	struct curl_slist *dav_headers = NULL;
	int rc = 0;

	lock_token_header = xmalloc(strlen(lock->token) + 31);
	sprintf(lock_token_header, "Lock-Token: <opaquelocktoken:%s>",
		lock->token);
	url = xmalloc(strlen(remote->url) + strlen(file) + 1);
	sprintf(url, "%s%s", remote->url, file);
	dav_headers = curl_slist_append(dav_headers, lock_token_header);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_null);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
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
	free(url);

	return rc;
}

int check_locking()
{
	struct active_request_slot *slot;
	struct buffer in_buffer;
	struct buffer out_buffer;
	char *in_data;
	char *out_data;
	XML_Parser parser = XML_ParserCreate(NULL);
	enum XML_Status result;
	struct lockprop supported_lock;
	struct curl_slist *dav_headers = NULL;

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
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
			 fwrite_buffer_dynamic);
	curl_easy_setopt(slot->curl, CURLOPT_URL, remote->url);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 1);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, DAV_PROPFIND);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, dav_headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		free(out_data);
		if (slot->curl_result != CURLE_OK) {
			free(in_buffer.buffer);
			return -1;
		}

		XML_SetUserData(parser, &supported_lock);
		XML_SetElementHandler(parser, start_lockprop_element,
				      end_lockprop_element);
		result = XML_Parse(parser, in_buffer.buffer, in_buffer.posn, 1);
		free(in_buffer.buffer);
		if (result != XML_STATUS_OK)
			return error("%s", XML_ErrorString(
					     XML_GetErrorCode(parser)));
	} else {
		free(out_data);
		free(in_buffer.buffer);
		return error("Unable to start request");
	}

	if (supported_lock.lock_exclusive_write)
		return 0;
	else
		return 1;
}

int is_ancestor(unsigned char *sha1, struct commit *commit)
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

void get_delta(unsigned char *sha1, struct object *obj,
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

int update_remote(char *remote_path, unsigned char *sha1,
		  struct active_lock *lock)
{
	struct active_request_slot *slot;
	char *url;
	char *out_data;
	char *if_header;
	struct buffer out_buffer;
	struct curl_slist *dav_headers = NULL;
	int i;

	url = xmalloc(strlen(remote->url) + strlen(remote_path) + 1);
	sprintf(url, "%s%s", remote->url, remote_path);

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
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		free(out_data);
		free(if_header);
		free(url);
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
		free(url);
		fprintf(stderr, "Unable to start PUT request\n");
		return 0;
	}

	return 1;
}

int main(int argc, char **argv)
{
	struct active_request_slot *slot;
	struct active_request_slot *next_slot;
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
	char *low_speed_limit;
	char *low_speed_time;
	int rc = 0;
	int i;

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

	curl_global_init(CURL_GLOBAL_ALL);

#ifdef USE_CURL_MULTI
	{
		char *http_max_requests = getenv("GIT_HTTP_MAX_REQUESTS");
		if (http_max_requests != NULL)
			max_requests = atoi(http_max_requests);
	}

	curlm = curl_multi_init();
	if (curlm == NULL) {
		fprintf(stderr, "Error creating curl multi handle.\n");
		return 1;
	}
#endif

	if (getenv("GIT_SSL_NO_VERIFY"))
		curl_ssl_verify = 0;

	ssl_cert = getenv("GIT_SSL_CERT");
#if LIBCURL_VERSION_NUM >= 0x070902
	ssl_key = getenv("GIT_SSL_KEY");
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
	ssl_capath = getenv("GIT_SSL_CAPATH");
#endif
	ssl_cainfo = getenv("GIT_SSL_CAINFO");

	low_speed_limit = getenv("GIT_HTTP_LOW_SPEED_LIMIT");
	if (low_speed_limit != NULL)
		curl_low_speed_limit = strtol(low_speed_limit, NULL, 10);
	low_speed_time = getenv("GIT_HTTP_LOW_SPEED_TIME");
	if (low_speed_time != NULL)
		curl_low_speed_time = strtol(low_speed_time, NULL, 10);

	git_config(http_options);

	if (curl_ssl_verify == -1)
		curl_ssl_verify = 1;

#ifdef USE_CURL_MULTI
	if (max_requests < 1)
		max_requests = DEFAULT_MAX_REQUESTS;
#endif

	no_pragma_header = curl_slist_append(no_pragma_header, "Pragma:");
	default_headers = curl_slist_append(default_headers, "Range:");
	default_headers = curl_slist_append(default_headers, "Destination:");
	default_headers = curl_slist_append(default_headers, "If:");
	default_headers = curl_slist_append(default_headers,
					    "Pragma: no-cache");

#ifndef NO_CURL_EASY_DUPHANDLE
	curl_default = get_curl_handle();
#endif

	/* Verify DAV compliance/lock support */
	if (check_locking() != 0) {
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
		remote_lock = lock_remote(remote_path, 3600);
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
		process_waiting_requests();

		/* Push missing objects to remote, this would be a
		   convenient time to pack them first if appropriate. */
		pushing = 1;
		process_request_queue();
		process_waiting_requests();

		/* Update the remote branch if all went well */
		if (do_remote_update) {
			if (!aborted && update_remote(remote_path,
						      local_sha1,
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
		unlock_remote(remote_path, remote_lock);
		free(remote_path);
		if (remote_lock->owner != NULL)
			free(remote_lock->owner);
		free(remote_lock->token);
		free(remote_lock);
	}

 cleanup:
	free(remote);

	curl_slist_free_all(no_pragma_header);
	curl_slist_free_all(default_headers);

	slot = active_queue_head;
	while (slot != NULL) {
		next_slot = slot->next;
		if (slot->curl != NULL)
			curl_easy_cleanup(slot->curl);
		free(slot);
		slot = next_slot;
	}

	request = request_queue_head;
	while (request != NULL) {
		next_request = request->next;
		release_request(request);
		request = next_request;
	}

#ifndef NO_CURL_EASY_DUPHANDLE
	curl_easy_cleanup(curl_default);
#endif
#ifdef USE_CURL_MULTI
	curl_multi_cleanup(curlm);
#endif
	curl_global_cleanup();
	return rc;
}
