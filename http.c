#include "http.h"

int data_received;
int active_requests = 0;

#ifdef USE_CURL_MULTI
static int max_requests = -1;
static CURLM *curlm;
#endif
#ifndef NO_CURL_EASY_DUPHANDLE
static CURL *curl_default;
#endif
char curl_errorstr[CURL_ERROR_SIZE];

static int curl_ssl_verify = -1;
static const char *ssl_cert = NULL;
#if LIBCURL_VERSION_NUM >= 0x070902
static const char *ssl_key = NULL;
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
static const char *ssl_capath = NULL;
#endif
static const char *ssl_cainfo = NULL;
static long curl_low_speed_limit = -1;
static long curl_low_speed_time = -1;
static int curl_ftp_no_epsv = 0;
static char *curl_http_proxy = NULL;

static struct curl_slist *pragma_header;

static struct active_request_slot *active_queue_head = NULL;

size_t fread_buffer(void *ptr, size_t eltsize, size_t nmemb, void *buffer_)
{
	size_t size = eltsize * nmemb;
	struct buffer *buffer = buffer_;

	if (size > buffer->buf.len - buffer->posn)
		size = buffer->buf.len - buffer->posn;
	memcpy(ptr, buffer->buf.buf + buffer->posn, size);
	buffer->posn += size;

	return size;
}

size_t fwrite_buffer(const void *ptr, size_t eltsize, size_t nmemb, void *buffer_)
{
	size_t size = eltsize * nmemb;
	struct strbuf *buffer = buffer_;

	strbuf_add(buffer, ptr, size);
	data_received++;
	return size;
}

size_t fwrite_null(const void *ptr, size_t eltsize, size_t nmemb, void *strbuf)
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

static int http_options(const char *var, const char *value, void *cb)
{
	if (!strcmp("http.sslverify", var)) {
		if (curl_ssl_verify == -1) {
			curl_ssl_verify = git_config_bool(var, value);
		}
		return 0;
	}

	if (!strcmp("http.sslcert", var)) {
		if (ssl_cert == NULL)
			return git_config_string(&ssl_cert, var, value);
		return 0;
	}
#if LIBCURL_VERSION_NUM >= 0x070902
	if (!strcmp("http.sslkey", var)) {
		if (ssl_key == NULL)
			return git_config_string(&ssl_key, var, value);
		return 0;
	}
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
	if (!strcmp("http.sslcapath", var)) {
		if (ssl_capath == NULL)
			return git_config_string(&ssl_capath, var, value);
		return 0;
	}
#endif
	if (!strcmp("http.sslcainfo", var)) {
		if (ssl_cainfo == NULL)
			return git_config_string(&ssl_cainfo, var, value);
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
	if (!strcmp("http.proxy", var)) {
		if (curl_http_proxy == NULL) {
			if (!value)
				return config_error_nonbool(var);
			curl_http_proxy = xstrdup(value);
		}
		return 0;
	}

	/* Fall back on the default ones */
	return git_default_config(var, value, cb);
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

	if (curl_http_proxy)
		curl_easy_setopt(result, CURLOPT_PROXY, curl_http_proxy);

	return result;
}

void http_init(struct remote *remote)
{
	char *low_speed_limit;
	char *low_speed_time;

	curl_global_init(CURL_GLOBAL_ALL);

	if (remote && remote->http_proxy)
		curl_http_proxy = xstrdup(remote->http_proxy);

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

	git_config(http_options, NULL);

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

	while (slot != NULL) {
		struct active_request_slot *next = slot->next;
		if (slot->curl != NULL) {
#ifdef USE_CURL_MULTI
			curl_multi_remove_handle(curlm, slot->curl);
#endif
			curl_easy_cleanup(slot->curl);
		}
		free(slot);
		slot = next;
	}
	active_queue_head = NULL;

#ifndef NO_CURL_EASY_DUPHANDLE
	curl_easy_cleanup(curl_default);
#endif

#ifdef USE_CURL_MULTI
	curl_multi_cleanup(curlm);
#endif
	curl_global_cleanup();

	curl_slist_free_all(pragma_header);
	pragma_header = NULL;

	if (curl_http_proxy) {
		free(curl_http_proxy);
		curl_http_proxy = NULL;
	}
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
	int num_transfers;

	if (curlm_result != CURLM_OK &&
	    curlm_result != CURLM_CALL_MULTI_PERFORM) {
		active_requests--;
		slot->in_use = 0;
		return 0;
	}

	/*
	 * We know there must be something to do, since we just added
	 * something.
	 */
	curl_multi_perform(curlm, &num_transfers);
#endif
	return 1;
}

#ifdef USE_CURL_MULTI
struct fill_chain {
	void *data;
	int (*fill)(void *);
	struct fill_chain *next;
};

static struct fill_chain *fill_cfg = NULL;

void add_fill_function(void *data, int (*fill)(void *))
{
	struct fill_chain *new = malloc(sizeof(*new));
	struct fill_chain **linkp = &fill_cfg;
	new->data = data;
	new->fill = fill;
	new->next = NULL;
	while (*linkp)
		linkp = &(*linkp)->next;
	*linkp = new;
}

void fill_active_slots(void)
{
	struct active_request_slot *slot = active_queue_head;

	while (active_requests < max_requests) {
		struct fill_chain *fill;
		for (fill = fill_cfg; fill; fill = fill->next)
			if (fill->fill(fill->data))
				break;

		if (!fill)
			break;
	}

	while (slot != NULL) {
		if (!slot->in_use && slot->curl != NULL) {
			curl_easy_cleanup(slot->curl);
			slot->curl = NULL;
		}
		slot = slot->next;
	}
}

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

static inline int needs_quote(int ch)
{
	if (((ch >= 'A') && (ch <= 'Z'))
			|| ((ch >= 'a') && (ch <= 'z'))
			|| ((ch >= '0') && (ch <= '9'))
			|| (ch == '/')
			|| (ch == '-')
			|| (ch == '.'))
		return 0;
	return 1;
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
	len = baselen + 2; /* '/' after base and terminating NUL */
	for (cp = ref; (ch = *cp) != 0; cp++, len++)
		if (needs_quote(ch))
			len += 2; /* extra two hex plus replacement % */
	qref = xmalloc(len);
	memcpy(qref, base, baselen);
	dp = qref + baselen;
	*(dp++) = '/';
	for (cp = ref; (ch = *cp) != 0; cp++) {
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

int http_fetch_ref(const char *base, struct ref *ref)
{
	char *url;
	struct strbuf buffer = STRBUF_INIT;
	struct active_request_slot *slot;
	struct slot_results results;
	int ret;

	url = quote_ref_url(base, ref->name);
	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_FILE, &buffer);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, fwrite_buffer);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK) {
			strbuf_rtrim(&buffer);
			if (buffer.len == 40)
				ret = get_sha1_hex(buffer.buf, ref->old_sha1);
			else if (!prefixcmp(buffer.buf, "ref: ")) {
				ref->symref = xstrdup(buffer.buf + 5);
				ret = 0;
			} else
				ret = 1;
		} else {
			ret = error("Couldn't get %s for %s\n%s",
				    url, ref->name, curl_errorstr);
		}
	} else {
		ret = error("Unable to start request");
	}

	strbuf_release(&buffer);
	free(url);
	return ret;
}
