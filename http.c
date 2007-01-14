#include "http.h"

int data_received;
int active_requests = 0;

#ifdef USE_CURL_MULTI
int max_requests = -1;
CURLM *curlm;
#endif
#ifndef NO_CURL_EASY_DUPHANDLE
CURL *curl_default;
#endif
char curl_errorstr[CURL_ERROR_SIZE];

int curl_ssl_verify = -1;
char *ssl_cert = NULL;
#if LIBCURL_VERSION_NUM >= 0x070902
char *ssl_key = NULL;
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
char *ssl_capath = NULL;
#endif
char *ssl_cainfo = NULL;
long curl_low_speed_limit = -1;
long curl_low_speed_time = -1;
int curl_ftp_no_epsv = 0;

struct curl_slist *pragma_header;

struct active_request_slot *active_queue_head = NULL;

size_t fread_buffer(void *ptr, size_t eltsize, size_t nmemb,
			   struct buffer *buffer)
{
	size_t size = eltsize * nmemb;
	if (size > buffer->size - buffer->posn)
		size = buffer->size - buffer->posn;
	memcpy(ptr, (char *) buffer->buffer + buffer->posn, size);
	buffer->posn += size;
	return size;
}

size_t fwrite_buffer(const void *ptr, size_t eltsize,
			    size_t nmemb, struct buffer *buffer)
{
	size_t size = eltsize * nmemb;
	if (size > buffer->size - buffer->posn) {
		buffer->size = buffer->size * 3 / 2;
		if (buffer->size < buffer->posn + size)
			buffer->size = buffer->posn + size;
		buffer->buffer = xrealloc(buffer->buffer, buffer->size);
	}
	memcpy((char *) buffer->buffer + buffer->posn, ptr, size);
	buffer->posn += size;
	data_received++;
	return size;
}

size_t fwrite_null(const void *ptr, size_t eltsize,
			  size_t nmemb, struct buffer *buffer)
{
	data_received++;
	return eltsize * nmemb;
}

static void finish_active_slot(struct active_request_slot *slot);

#ifdef USE_CURL_MULTI
static void process_curl_messages(void)
{
	int num_messages;
	struct active_request_slot *slot;
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
				slot->curl_result = curl_result;
				finish_active_slot(slot);
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
#endif

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

	if (!strcmp("http.noepsv", var)) {
		curl_ftp_no_epsv = git_config_bool(var, value);
		return 0;
	}

	/* Fall back on the default ones */
	return git_default_config(var, value);
}

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

	curl_easy_setopt(result, CURLOPT_FOLLOWLOCATION, 1);

	if (getenv("GIT_CURL_VERBOSE"))
		curl_easy_setopt(result, CURLOPT_VERBOSE, 1);

	curl_easy_setopt(result, CURLOPT_USERAGENT, GIT_USER_AGENT);

	if (curl_ftp_no_epsv)
		curl_easy_setopt(result, CURLOPT_FTP_USE_EPSV, 0);

	return result;
}

void http_init(void)
{
	char *low_speed_limit;
	char *low_speed_time;

	curl_global_init(CURL_GLOBAL_ALL);

	pragma_header = curl_slist_append(pragma_header, "Pragma: no-cache");

#ifdef USE_CURL_MULTI
	{
		char *http_max_requests = getenv("GIT_HTTP_MAX_REQUESTS");
		if (http_max_requests != NULL)
			max_requests = atoi(http_max_requests);
	}

	curlm = curl_multi_init();
	if (curlm == NULL) {
		fprintf(stderr, "Error creating curl multi handle.\n");
		exit(1);
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

	if (getenv("GIT_CURL_FTP_NO_EPSV"))
		curl_ftp_no_epsv = 1;

#ifndef NO_CURL_EASY_DUPHANDLE
	curl_default = get_curl_handle();
#endif
}

void http_cleanup(void)
{
	struct active_request_slot *slot = active_queue_head;
#ifdef USE_CURL_MULTI
	char *wait_url;
#endif

	while (slot != NULL) {
#ifdef USE_CURL_MULTI
		if (slot->in_use) {
			curl_easy_getinfo(slot->curl,
					  CURLINFO_EFFECTIVE_URL,
					  &wait_url);
			fprintf(stderr, "Waiting for %s\n", wait_url);
			run_active_slot(slot);
		}
#endif
		if (slot->curl != NULL)
			curl_easy_cleanup(slot->curl);
		slot = slot->next;
	}

#ifndef NO_CURL_EASY_DUPHANDLE
	curl_easy_cleanup(curl_default);
#endif

#ifdef USE_CURL_MULTI
	curl_multi_cleanup(curlm);
#endif
	curl_global_cleanup();

	curl_slist_free_all(pragma_header);
}

struct active_request_slot *get_active_slot(void)
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
	slot->local = NULL;
	slot->results = NULL;
	slot->finished = NULL;
	slot->callback_data = NULL;
	slot->callback_func = NULL;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, pragma_header);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, curl_errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 0);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);

	return slot;
}

int start_active_slot(struct active_request_slot *slot)
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

#ifdef USE_CURL_MULTI
void step_active_slots(void)
{
	int num_transfers;
	CURLMcode curlm_result;

	do {
		curlm_result = curl_multi_perform(curlm, &num_transfers);
	} while (curlm_result == CURLM_CALL_MULTI_PERFORM);
	if (num_transfers < active_requests) {
		process_curl_messages();
		fill_active_slots();
	}
}
#endif

void run_active_slot(struct active_request_slot *slot)
{
#ifdef USE_CURL_MULTI
	long last_pos = 0;
	long current_pos;
	fd_set readfds;
	fd_set writefds;
	fd_set excfds;
	int max_fd;
	struct timeval select_timeout;
	int finished = 0;

	slot->finished = &finished;
	while (!finished) {
		data_received = 0;
		step_active_slots();

		if (!data_received && slot->local != NULL) {
			current_pos = ftell(slot->local);
			if (current_pos > last_pos)
				data_received++;
			last_pos = current_pos;
		}

		if (slot->in_use && !data_received) {
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
	while (slot->in_use) {
		slot->curl_result = curl_easy_perform(slot->curl);
		finish_active_slot(slot);
	}
#endif
}

static void closedown_active_slot(struct active_request_slot *slot)
{
        active_requests--;
        slot->in_use = 0;
}

void release_active_slot(struct active_request_slot *slot)
{
	closedown_active_slot(slot);
	if (slot->curl) {
#ifdef USE_CURL_MULTI
		curl_multi_remove_handle(curlm, slot->curl);
#endif
		curl_easy_cleanup(slot->curl);
		slot->curl = NULL;
	}
#ifdef USE_CURL_MULTI
	fill_active_slots();
#endif
}

static void finish_active_slot(struct active_request_slot *slot)
{
	closedown_active_slot(slot);
        curl_easy_getinfo(slot->curl, CURLINFO_HTTP_CODE, &slot->http_code);

	if (slot->finished != NULL)
		(*slot->finished) = 1;

	/* Store slot results so they can be read after the slot is reused */
	if (slot->results != NULL) {
		slot->results->curl_result = slot->curl_result;
		slot->results->http_code = slot->http_code;
	}

        /* Run callback if appropriate */
        if (slot->callback_func != NULL) {
                slot->callback_func(slot->callback_data);
        }
}

void finish_all_active_slots(void)
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
