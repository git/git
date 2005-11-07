#include "cache.h"
#include "commit.h"
#include "pack.h"
#include "fetch.h"

#include <curl/curl.h>
#include <curl/easy.h>

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

#define PREV_BUF_SIZE 4096
#define RANGE_HEADER_SIZE 30

static int got_alternates = 0;
static int active_requests = 0;
static int data_received;

#ifdef USE_CURL_MULTI
static int max_requests = -1;
static CURLM *curlm;
#endif
#ifndef NO_CURL_EASY_DUPHANDLE
static CURL *curl_default;
#endif
static struct curl_slist *pragma_header;
static struct curl_slist *no_pragma_header;
static struct curl_slist *no_range_header;
static char curl_errorstr[CURL_ERROR_SIZE];

struct alt_base
{
	char *base;
	int got_indices;
	struct packed_git *packs;
	struct alt_base *next;
};

static struct alt_base *alt = NULL;

enum transfer_state {
	WAITING,
	ABORTED,
	ACTIVE,
	COMPLETE,
};

struct transfer_request
{
	unsigned char sha1[20];
	struct alt_base *repo;
	char *url;
	char filename[PATH_MAX];
	char tmpfile[PATH_MAX];
	int local;
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

struct buffer
{
        size_t posn;
        size_t size;
        void *buffer;
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

static size_t fwrite_buffer(void *ptr, size_t eltsize, size_t nmemb,
                            struct buffer *buffer)
{
        size_t size = eltsize * nmemb;
        if (size > buffer->size - buffer->posn)
                size = buffer->size - buffer->posn;
        memcpy(buffer->buffer + buffer->posn, ptr, size);
        buffer->posn += size;
	data_received++;
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

static size_t fwrite_sha1_file(void *ptr, size_t eltsize, size_t nmemb,
			       void *data)
{
	unsigned char expn[4096];
	size_t size = eltsize * nmemb;
	int posn = 0;
	struct transfer_request *request = (struct transfer_request *)data;
	do {
		ssize_t retval = write(request->local,
				       ptr + posn, size - posn);
		if (retval < 0)
			return posn;
		posn += retval;
	} while (posn < size);

	request->stream.avail_in = size;
	request->stream.next_in = ptr;
	do {
		request->stream.next_out = expn;
		request->stream.avail_out = sizeof(expn);
		request->zret = inflate(&request->stream, Z_SYNC_FLUSH);
		SHA1_Update(&request->c, expn,
			    sizeof(expn) - request->stream.avail_out);
	} while (request->stream.avail_in && request->zret == Z_OK);
	data_received++;
	return size;
}

#ifdef USE_CURL_MULTI
static void process_curl_messages(void);
static void process_request_queue(void);
#endif
static int fetch_alternates(char *base);

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
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, pragma_header);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_range_header);
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

static void start_request(struct transfer_request *request)
{
	char *hex = sha1_to_hex(request->sha1);
	char prevfile[PATH_MAX];
	char *url;
	char *posn;
	int prevlocal;
	unsigned char prev_buf[PREV_BUF_SIZE];
	ssize_t prev_read = 0;
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;
	struct active_request_slot *slot;

	snprintf(prevfile, sizeof(prevfile), "%s.prev", request->filename);
	unlink(prevfile);
	rename(request->tmpfile, prevfile);
	unlink(request->tmpfile);

	request->local = open(request->tmpfile,
			      O_WRONLY | O_CREAT | O_EXCL, 0666);
	/* This could have failed due to the "lazy directory creation";
	 * try to mkdir the last path component.
	 */
	if (request->local < 0 && errno == ENOENT) {
		char *dir = strrchr(request->tmpfile, '/');
		if (dir) {
			*dir = 0;
			mkdir(request->tmpfile, 0777);
			*dir = '/';
		}
		request->local = open(request->tmpfile,
				      O_WRONLY | O_CREAT | O_EXCL, 0666);
	}

	if (request->local < 0) {
		request->state = ABORTED;
		error("Couldn't create temporary file %s for %s: %s\n",
		      request->tmpfile, request->filename, strerror(errno));
		return;
	}

	memset(&request->stream, 0, sizeof(request->stream));

	inflateInit(&request->stream);

	SHA1_Init(&request->c);

	url = xmalloc(strlen(request->repo->base) + 50);
	request->url = xmalloc(strlen(request->repo->base) + 50);
	strcpy(url, request->repo->base);
	posn = url + strlen(request->repo->base);
	strcpy(posn, "objects/");
	posn += 8;
	memcpy(posn, hex, 2);
	posn += 2;
	*(posn++) = '/';
	strcpy(posn, hex + 2);
	strcpy(request->url, url);

	/* If a previous temp file is present, process what was already
	   fetched. */
	prevlocal = open(prevfile, O_RDONLY);
	if (prevlocal != -1) {
		do {
			prev_read = read(prevlocal, prev_buf, PREV_BUF_SIZE);
			if (prev_read>0) {
				if (fwrite_sha1_file(prev_buf,
						     1,
						     prev_read,
						     request) == prev_read) {
					prev_posn += prev_read;
				} else {
					prev_read = -1;
				}
			}
		} while (prev_read > 0);
		close(prevlocal);
	}
	unlink(prevfile);

	/* Reset inflate/SHA1 if there was an error reading the previous temp
	   file; also rewind to the beginning of the local file. */
	if (prev_read == -1) {
		memset(&request->stream, 0, sizeof(request->stream));
		inflateInit(&request->stream);
		SHA1_Init(&request->c);
		if (prev_posn>0) {
			prev_posn = 0;
			lseek(request->local, SEEK_SET, 0);
			ftruncate(request->local, 0);
		}
	}

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, request);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_sha1_file);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, request->errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);

	/* If we have successfully processed data from a previous fetch
	   attempt, only fetch the data we don't already have. */
	if (prev_posn>0) {
		if (get_verbosely)
			fprintf(stderr,
				"Resuming fetch of object %s at byte %ld\n",
				hex, prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl,
				 CURLOPT_HTTPHEADER, range_header);
	}

	/* Try to get the request started, abort the request on error */
	if (!start_active_slot(slot)) {
		request->state = ABORTED;
		close(request->local);
		free(request->url);
		return;
	}
	
	request->slot = slot;
	request->state = ACTIVE;
}

static void finish_request(struct transfer_request *request)
{
	struct stat st;

	fchmod(request->local, 0444);
	close(request->local);

	if (request->http_code == 416) {
		fprintf(stderr, "Warning: requested range invalid; we may already have all the data.\n");
	} else if (request->curl_result != CURLE_OK) {
		if (stat(request->tmpfile, &st) == 0)
			if (st.st_size == 0)
				unlink(request->tmpfile);
		return;
	}

	inflateEnd(&request->stream);
	SHA1_Final(request->real_sha1, &request->c);
	if (request->zret != Z_STREAM_END) {
		unlink(request->tmpfile);
		return;
	}
	if (memcmp(request->sha1, request->real_sha1, 20)) {
		unlink(request->tmpfile);
		return;
	}
	request->rename =
		move_temp_to_file(request->tmpfile, request->filename);

	if (request->rename == 0)
		pull_say("got %s\n", sha1_to_hex(request->sha1));
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
			int curl_result = curl_message->data.result;
			slot = active_queue_head;
			while (slot != NULL &&
			       slot->curl != curl_message->easy_handle)
				slot = slot->next;
			if (slot != NULL) {
				curl_multi_remove_handle(curlm, slot->curl);
				active_requests--;
				slot->done = 1;
				slot->in_use = 0;
				slot->curl_result = curl_result;
				curl_easy_getinfo(slot->curl,
						  CURLINFO_HTTP_CODE,
						  &slot->http_code);
				request = request_queue_head;
				while (request != NULL &&
				       request->slot != slot)
					request = request->next;
			} else {
				fprintf(stderr, "Received DONE message for unknown request!\n");
			}
			if (request != NULL) {
				request->curl_result = curl_result;
				request->http_code = slot->http_code;
				request->slot = NULL;
				request->state = COMPLETE;

				/* Use alternates if necessary */
				if (request->http_code == 404) {
					fetch_alternates(alt->base);
					if (request->repo->next != NULL) {
						request->repo =
							request->repo->next;
						start_request(request);
					}
				} else {
					finish_request(request);
				}
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

	while (active_requests < max_requests && request != NULL) {
		if (request->state == WAITING) {
			if (has_sha1_file(request->sha1))
				release_request(request);
			else
				start_request(request);
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

void prefetch(unsigned char *sha1)
{
	struct transfer_request *newreq;
	struct transfer_request *tail;
	char *filename = sha1_file_name(sha1);

	newreq = xmalloc(sizeof(*newreq));
	memcpy(newreq->sha1, sha1, 20);
	newreq->repo = alt;
	newreq->url = NULL;
	newreq->local = -1;
	newreq->state = WAITING;
	snprintf(newreq->filename, sizeof(newreq->filename), "%s", filename);
	snprintf(newreq->tmpfile, sizeof(newreq->tmpfile),
		 "%s.temp", filename);
	newreq->next = NULL;

	if (request_queue_head == NULL) {
		request_queue_head = newreq;
	} else {
		tail = request_queue_head;
		while (tail->next != NULL) {
			tail = tail->next;
		}
		tail->next = newreq;
	}
#ifdef USE_CURL_MULTI
	process_request_queue();
	process_curl_messages();
#endif
}

static int fetch_index(struct alt_base *repo, unsigned char *sha1)
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

	if (has_pack_index(sha1))
		return 0;

	if (get_verbosely)
		fprintf(stderr, "Getting index for pack %s\n", hex);
	
	url = xmalloc(strlen(repo->base) + 64);
	sprintf(url, "%s/objects/pack/pack-%s.idx", repo->base, hex);
	
	filename = sha1_pack_index_name(sha1);
	snprintf(tmpfile, sizeof(tmpfile), "%s.temp", filename);
	indexfile = fopen(tmpfile, "a");
	if (!indexfile)
		return error("Unable to open local file %s for pack index",
			     filename);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, indexfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);
	slot->local = indexfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(indexfile);
	if (prev_posn>0) {
		if (get_verbosely)
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
			fclose(indexfile);
			return error("Unable to get pack index %s\n%s", url,
				     curl_errorstr);
		}
	} else {
		return error("Unable to start request");
	}

	fclose(indexfile);

	return move_temp_to_file(tmpfile, filename);
}

static int setup_index(struct alt_base *repo, unsigned char *sha1)
{
	struct packed_git *new_pack;
	if (has_pack_file(sha1))
		return 0; // don't list this as something we can get

	if (fetch_index(repo, sha1))
		return -1;

	new_pack = parse_pack_index(sha1);
	new_pack->next = repo->packs;
	repo->packs = new_pack;
	return 0;
}

static int fetch_alternates(char *base)
{
	int ret = 0;
	struct buffer buffer;
	char *url;
	char *data;
	int i = 0;
	int http_specific = 1;
	struct alt_base *tail = alt;
	static const char null_byte = '\0';

	struct active_request_slot *slot;

	if (got_alternates)
		return 0;

	data = xmalloc(4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (get_verbosely)
		fprintf(stderr, "Getting alternates list\n");
	
	url = xmalloc(strlen(base) + 31);
	sprintf(url, "%s/objects/info/http-alternates", base);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
			 fwrite_buffer_dynamic);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK || !buffer.posn) {
			http_specific = 0;

			sprintf(url, "%s/objects/info/alternates", base);

			slot = get_active_slot();
			curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
			curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
					 fwrite_buffer_dynamic);
			curl_easy_setopt(slot->curl, CURLOPT_URL, url);
			if (start_active_slot(slot)) {
				run_active_slot(slot);
				if (slot->curl_result != CURLE_OK) {
					free(buffer.buffer);
					if (slot->http_code == 404)
						got_alternates = 1;
					return 0;
				}
			}
		}
	} else {
		free(buffer.buffer);
		return 0;
	}

	fwrite_buffer_dynamic(&null_byte, 1, 1, &buffer);
	buffer.posn--;
	data = buffer.buffer;

	while (i < buffer.posn) {
		int posn = i;
		while (posn < buffer.posn && data[posn] != '\n')
			posn++;
		if (data[posn] == '\n') {
			int okay = 0;
			int serverlen = 0;
			struct alt_base *newalt;
			char *target = NULL;
			if (data[i] == '/') {
				serverlen = strchr(base + 8, '/') - base;
				okay = 1;
			} else if (!memcmp(data + i, "../", 3)) {
				i += 3;
				serverlen = strlen(base);
				while (i + 2 < posn && 
				       !memcmp(data + i, "../", 3)) {
					do {
						serverlen--;
					} while (serverlen &&
						 base[serverlen - 1] != '/');
					i += 3;
				}
				// If the server got removed, give up.
				okay = strchr(base, ':') - base + 3 < 
					serverlen;
			} else if (http_specific) {
				char *colon = strchr(data + i, ':');
				char *slash = strchr(data + i, '/');
				if (colon && slash && colon < data + posn &&
				    slash < data + posn && colon < slash) {
					okay = 1;
				}
			}
			// skip 'objects' at end
			if (okay) {
				target = xmalloc(serverlen + posn - i - 6);
				strncpy(target, base, serverlen);
				strncpy(target + serverlen, data + i,
					posn - i - 7);
				target[serverlen + posn - i - 7] = '\0';
				if (get_verbosely)
					fprintf(stderr, 
						"Also look at %s\n", target);
				newalt = xmalloc(sizeof(*newalt));
				newalt->next = NULL;
				newalt->base = target;
				newalt->got_indices = 0;
				newalt->packs = NULL;
				while (tail->next != NULL)
					tail = tail->next;
				tail->next = newalt;
				ret++;
			}
		}
		i = posn + 1;
	}

	got_alternates = 1;
	free(buffer.buffer);
	return ret;
}

static int fetch_indices(struct alt_base *repo)
{
	unsigned char sha1[20];
	char *url;
	struct buffer buffer;
	char *data;
	int i = 0;

	struct active_request_slot *slot;

	if (repo->got_indices)
		return 0;

	data = xmalloc(4096);
	buffer.size = 4096;
	buffer.posn = 0;
	buffer.buffer = data;

	if (get_verbosely)
		fprintf(stderr, "Getting pack list\n");
	
	url = xmalloc(strlen(repo->base) + 21);
	sprintf(url, "%s/objects/info/packs", repo->base);

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
			return error("%s", curl_errorstr);
		}
	} else {
		free(buffer.buffer);
		return error("Unable to start request");
	}

	data = buffer.buffer;
	while (i < buffer.posn) {
		switch (data[i]) {
		case 'P':
			i++;
			if (i + 52 < buffer.posn &&
			    !strncmp(data + i, " pack-", 6) &&
			    !strncmp(data + i + 46, ".pack\n", 6)) {
				get_sha1_hex(data + i + 6, sha1);
				setup_index(repo, sha1);
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
	repo->got_indices = 1;
	return 0;
}

static int fetch_pack(struct alt_base *repo, unsigned char *sha1)
{
	char *url;
	struct packed_git *target;
	struct packed_git **lst;
	FILE *packfile;
	char *filename;
	char tmpfile[PATH_MAX];
	int ret;
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;

	struct active_request_slot *slot;

	if (fetch_indices(repo))
		return -1;
	target = find_sha1_pack(sha1, repo->packs);
	if (!target)
		return -1;

	if (get_verbosely) {
		fprintf(stderr, "Getting pack %s\n",
			sha1_to_hex(target->sha1));
		fprintf(stderr, " which contains %s\n",
			sha1_to_hex(sha1));
	}

	url = xmalloc(strlen(repo->base) + 65);
	sprintf(url, "%s/objects/pack/pack-%s.pack",
		repo->base, sha1_to_hex(target->sha1));

	filename = sha1_pack_name(target->sha1);
	snprintf(tmpfile, sizeof(tmpfile), "%s.temp", filename);
	packfile = fopen(tmpfile, "a");
	if (!packfile)
		return error("Unable to open local file %s for pack",
			     filename);

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_FILE, packfile);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);
	slot->local = packfile;

	/* If there is data present from a previous transfer attempt,
	   resume where it left off */
	prev_posn = ftell(packfile);
	if (prev_posn>0) {
		if (get_verbosely)
			fprintf(stderr,
				"Resuming fetch of pack %s at byte %ld\n",
				sha1_to_hex(target->sha1), prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, range_header);
	}

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (slot->curl_result != CURLE_OK) {
			fclose(packfile);
			return error("Unable to get pack file %s\n%s", url,
				     curl_errorstr);
		}
	} else {
		return error("Unable to start request");
	}

	fclose(packfile);

	ret = move_temp_to_file(tmpfile, filename);
	if (ret)
		return ret;

	lst = &repo->packs;
	while (*lst != target)
		lst = &((*lst)->next);
	*lst = (*lst)->next;

	if (verify_pack(target, 0))
		return -1;
	install_packed_git(target);

	return 0;
}

static int fetch_object(struct alt_base *repo, unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	int ret;
	struct transfer_request *request = request_queue_head;

	while (request != NULL && memcmp(request->sha1, sha1, 20))
		request = request->next;
	if (request == NULL)
		return error("Couldn't find request for %s in the queue", hex);

	if (has_sha1_file(request->sha1)) {
		release_request(request);
		return 0;
	}

#ifdef USE_CURL_MULTI
	while (request->state == WAITING) {
		int num_transfers;
		curl_multi_perform(curlm, &num_transfers);
		if (num_transfers < active_requests) {
			process_curl_messages();
			process_request_queue();
		}
	}
#else
	start_request(request);
#endif

	while (request->state == ACTIVE) {
		run_active_slot(request->slot);
#ifndef USE_CURL_MULTI
		request->curl_result = request->slot->curl_result;
		request->http_code = request->slot->http_code;
		request->slot = NULL;

		/* Use alternates if necessary */
		if (request->http_code == 404) {
			fetch_alternates(alt->base);
			if (request->repo->next != NULL) {
				request->repo = request->repo->next;
				start_request(request);
			}
		} else {
			finish_request(request);
			request->state = COMPLETE;
		}
#endif
	}

	if (request->state == ABORTED) {
		release_request(request);
		return error("Request for %s aborted", hex);
	}

	if (request->curl_result != CURLE_OK && request->http_code != 416) {
		if (request->http_code == 404)
			ret = -1; /* Be silent, it is probably in a pack. */
		else
			ret = error("%s (curl_result = %d, http_code = %ld, sha1 = %s)",
				    request->errorstr, request->curl_result,
				    request->http_code, hex);
		release_request(request);
		return ret;
	}

	if (request->zret != Z_STREAM_END) {
		ret = error("File %s (%s) corrupt\n", hex, request->url);
		release_request(request);
		return ret;
	}

	if (memcmp(request->sha1, request->real_sha1, 20)) {
		release_request(request);
		return error("File %s has bad hash\n", hex);
	}

	if (request->rename < 0) {
		ret = error("unable to write sha1 filename %s: %s",
			    request->filename,
			    strerror(request->rename));
		release_request(request);
		return ret;
	}

	release_request(request);
	return 0;
}

int fetch(unsigned char *sha1)
{
	struct alt_base *altbase = alt;

	if (!fetch_object(altbase, sha1))
		return 0;
	while (altbase) {
		if (!fetch_pack(altbase, sha1))
			return 0;
		fetch_alternates(alt->base);
		altbase = altbase->next;
	}
	return error("Unable to find %s under %s\n", sha1_to_hex(sha1), 
		     alt->base);
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
	len = baselen + 6; /* "refs/" + NUL */
	for (cp = ref; (ch = *cp) != 0; cp++, len++)
		if (needs_quote(ch))
			len += 2; /* extra two hex plus replacement % */
	qref = xmalloc(len);
	memcpy(qref, base, baselen);
	memcpy(qref + baselen, "refs/", 5);
	for (cp = ref, dp = qref + baselen + 5; (ch = *cp) != 0; cp++) {
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
	char *base = alt->base;
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

int main(int argc, char **argv)
{
	char *commit_id;
	char *url;
	int arg = 1;
	struct active_request_slot *slot;
	char *low_speed_limit;
	char *low_speed_time;
	char *wait_url;
	int rc = 0;

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
			get_tree = 1;
		} else if (argv[arg][1] == 'c') {
			get_history = 1;
		} else if (argv[arg][1] == 'a') {
			get_all = 1;
			get_tree = 1;
			get_history = 1;
		} else if (argv[arg][1] == 'v') {
			get_verbosely = 1;
		} else if (argv[arg][1] == 'w') {
			write_ref = argv[arg + 1];
			arg++;
		} else if (!strcmp(argv[arg], "--recover")) {
			get_recover = 1;
		}
		arg++;
	}
	if (argc < arg + 2) {
		usage("git-http-fetch [-c] [-t] [-a] [-d] [-v] [--recover] [-w ref] commit-id url");
		return 1;
	}
	commit_id = argv[arg];
	url = argv[arg + 1];

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

	pragma_header = curl_slist_append(pragma_header, "Pragma: no-cache");
	no_pragma_header = curl_slist_append(no_pragma_header, "Pragma:");
	no_range_header = curl_slist_append(no_range_header, "Range:");

#ifndef NO_CURL_EASY_DUPHANDLE
	curl_default = get_curl_handle();
#endif

	alt = xmalloc(sizeof(*alt));
	alt->base = url;
	alt->got_indices = 0;
	alt->packs = NULL;
	alt->next = NULL;

	if (pull(commit_id))
		rc = 1;

	curl_slist_free_all(pragma_header);
	curl_slist_free_all(no_pragma_header);
	curl_slist_free_all(no_range_header);
#ifndef NO_CURL_EASY_DUPHANDLE
	curl_easy_cleanup(curl_default);
#endif
	slot = active_queue_head;
	while (slot != NULL) {
		if (slot->in_use) {
			if (get_verbosely) {
				curl_easy_getinfo(slot->curl,
						  CURLINFO_EFFECTIVE_URL,
						  &wait_url);
				fprintf(stderr, "Waiting for %s\n", wait_url);
			}
			run_active_slot(slot);
		}
		if (slot->curl != NULL)
			curl_easy_cleanup(slot->curl);
		slot = slot->next;
	}
#ifdef USE_CURL_MULTI
	curl_multi_cleanup(curlm);
#endif
	curl_global_cleanup();
	return rc;
}
