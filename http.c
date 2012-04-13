#include "http.h"
#include "pack.h"
#include "sideband.h"
#include "run-command.h"
#include "url.h"
#include "credential.h"

int active_requests;
int http_is_verbose;
size_t http_post_buffer = 16 * LARGE_PACKET_MAX;

#if LIBCURL_VERSION_NUM >= 0x070a06
#define LIBCURL_CAN_HANDLE_AUTH_ANY
#endif

static int min_curl_sessions = 1;
static int curl_session_count;
#ifdef USE_CURL_MULTI
static int max_requests = -1;
static CURLM *curlm;
#endif
#ifndef NO_CURL_EASY_DUPHANDLE
static CURL *curl_default;
#endif

#define PREV_BUF_SIZE 4096
#define RANGE_HEADER_SIZE 30

char curl_errorstr[CURL_ERROR_SIZE];

static int curl_ssl_verify = -1;
static const char *ssl_cert;
#if LIBCURL_VERSION_NUM >= 0x070903
static const char *ssl_key;
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
static const char *ssl_capath;
#endif
static const char *ssl_cainfo;
static long curl_low_speed_limit = -1;
static long curl_low_speed_time = -1;
static int curl_ftp_no_epsv;
static const char *curl_http_proxy;
static const char *curl_cookie_file;
static struct credential http_auth = CREDENTIAL_INIT;
static int http_proactive_auth;
static const char *user_agent;

#if LIBCURL_VERSION_NUM >= 0x071700
/* Use CURLOPT_KEYPASSWD as is */
#elif LIBCURL_VERSION_NUM >= 0x070903
#define CURLOPT_KEYPASSWD CURLOPT_SSLKEYPASSWD
#else
#define CURLOPT_KEYPASSWD CURLOPT_SSLCERTPASSWD
#endif

static struct credential cert_auth = CREDENTIAL_INIT;
static int ssl_cert_password_required;

static struct curl_slist *pragma_header;
static struct curl_slist *no_pragma_header;

static struct active_request_slot *active_queue_head;

size_t fread_buffer(char *ptr, size_t eltsize, size_t nmemb, void *buffer_)
{
	size_t size = eltsize * nmemb;
	struct buffer *buffer = buffer_;

	if (size > buffer->buf.len - buffer->posn)
		size = buffer->buf.len - buffer->posn;
	memcpy(ptr, buffer->buf.buf + buffer->posn, size);
	buffer->posn += size;

	return size;
}

#ifndef NO_CURL_IOCTL
curlioerr ioctl_buffer(CURL *handle, int cmd, void *clientp)
{
	struct buffer *buffer = clientp;

	switch (cmd) {
	case CURLIOCMD_NOP:
		return CURLIOE_OK;

	case CURLIOCMD_RESTARTREAD:
		buffer->posn = 0;
		return CURLIOE_OK;

	default:
		return CURLIOE_UNKNOWNCMD;
	}
}
#endif

size_t fwrite_buffer(char *ptr, size_t eltsize, size_t nmemb, void *buffer_)
{
	size_t size = eltsize * nmemb;
	struct strbuf *buffer = buffer_;

	strbuf_add(buffer, ptr, size);
	return size;
}

size_t fwrite_null(char *ptr, size_t eltsize, size_t nmemb, void *strbuf)
{
	return eltsize * nmemb;
}

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
		curl_ssl_verify = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp("http.sslcert", var))
		return git_config_string(&ssl_cert, var, value);
#if LIBCURL_VERSION_NUM >= 0x070903
	if (!strcmp("http.sslkey", var))
		return git_config_string(&ssl_key, var, value);
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
	if (!strcmp("http.sslcapath", var))
		return git_config_string(&ssl_capath, var, value);
#endif
	if (!strcmp("http.sslcainfo", var))
		return git_config_string(&ssl_cainfo, var, value);
	if (!strcmp("http.sslcertpasswordprotected", var)) {
		if (git_config_bool(var, value))
			ssl_cert_password_required = 1;
		return 0;
	}
	if (!strcmp("http.minsessions", var)) {
		min_curl_sessions = git_config_int(var, value);
#ifndef USE_CURL_MULTI
		if (min_curl_sessions > 1)
			min_curl_sessions = 1;
#endif
		return 0;
	}
#ifdef USE_CURL_MULTI
	if (!strcmp("http.maxrequests", var)) {
		max_requests = git_config_int(var, value);
		return 0;
	}
#endif
	if (!strcmp("http.lowspeedlimit", var)) {
		curl_low_speed_limit = (long)git_config_int(var, value);
		return 0;
	}
	if (!strcmp("http.lowspeedtime", var)) {
		curl_low_speed_time = (long)git_config_int(var, value);
		return 0;
	}

	if (!strcmp("http.noepsv", var)) {
		curl_ftp_no_epsv = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp("http.proxy", var))
		return git_config_string(&curl_http_proxy, var, value);

	if (!strcmp("http.cookiefile", var))
		return git_config_string(&curl_cookie_file, var, value);

	if (!strcmp("http.postbuffer", var)) {
		http_post_buffer = git_config_int(var, value);
		if (http_post_buffer < LARGE_PACKET_MAX)
			http_post_buffer = LARGE_PACKET_MAX;
		return 0;
	}

	if (!strcmp("http.useragent", var))
		return git_config_string(&user_agent, var, value);

	/* Fall back on the default ones */
	return git_default_config(var, value, cb);
}

static void init_curl_http_auth(CURL *result)
{
	if (!http_auth.username)
		return;

	credential_fill(&http_auth);

#if LIBCURL_VERSION_NUM >= 0x071301
	curl_easy_setopt(result, CURLOPT_USERNAME, http_auth.username);
	curl_easy_setopt(result, CURLOPT_PASSWORD, http_auth.password);
#else
	{
		static struct strbuf up = STRBUF_INIT;
		strbuf_reset(&up);
		strbuf_addf(&up, "%s:%s",
			    http_auth.username, http_auth.password);
		curl_easy_setopt(result, CURLOPT_USERPWD, up.buf);
	}
#endif
}

static int has_cert_password(void)
{
	if (ssl_cert == NULL || ssl_cert_password_required != 1)
		return 0;
	if (!cert_auth.password) {
		cert_auth.protocol = xstrdup("cert");
		cert_auth.path = xstrdup(ssl_cert);
		credential_fill(&cert_auth);
	}
	return 1;
}

static CURL *get_curl_handle(void)
{
	CURL *result = curl_easy_init();

	if (!curl_ssl_verify) {
		curl_easy_setopt(result, CURLOPT_SSL_VERIFYPEER, 0);
		curl_easy_setopt(result, CURLOPT_SSL_VERIFYHOST, 0);
	} else {
		/* Verify authenticity of the peer's certificate */
		curl_easy_setopt(result, CURLOPT_SSL_VERIFYPEER, 1);
		/* The name in the cert must match whom we tried to connect */
		curl_easy_setopt(result, CURLOPT_SSL_VERIFYHOST, 2);
	}

#if LIBCURL_VERSION_NUM >= 0x070907
	curl_easy_setopt(result, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
#endif
#ifdef LIBCURL_CAN_HANDLE_AUTH_ANY
	curl_easy_setopt(result, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
#endif

	if (http_proactive_auth)
		init_curl_http_auth(result);

	if (ssl_cert != NULL)
		curl_easy_setopt(result, CURLOPT_SSLCERT, ssl_cert);
	if (has_cert_password())
		curl_easy_setopt(result, CURLOPT_KEYPASSWD, cert_auth.password);
#if LIBCURL_VERSION_NUM >= 0x070903
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
#if LIBCURL_VERSION_NUM >= 0x071301
	curl_easy_setopt(result, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
#elif LIBCURL_VERSION_NUM >= 0x071101
	curl_easy_setopt(result, CURLOPT_POST301, 1);
#endif

	if (getenv("GIT_CURL_VERBOSE"))
		curl_easy_setopt(result, CURLOPT_VERBOSE, 1);

	curl_easy_setopt(result, CURLOPT_USERAGENT,
		user_agent ? user_agent : GIT_HTTP_USER_AGENT);

	if (curl_ftp_no_epsv)
		curl_easy_setopt(result, CURLOPT_FTP_USE_EPSV, 0);

	if (curl_http_proxy) {
		curl_easy_setopt(result, CURLOPT_PROXY, curl_http_proxy);
		curl_easy_setopt(result, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
	}

	return result;
}

static void set_from_env(const char **var, const char *envname)
{
	const char *val = getenv(envname);
	if (val)
		*var = val;
}

void http_init(struct remote *remote, const char *url, int proactive_auth)
{
	char *low_speed_limit;
	char *low_speed_time;

	http_is_verbose = 0;

	git_config(http_options, NULL);

	curl_global_init(CURL_GLOBAL_ALL);

	http_proactive_auth = proactive_auth;

	if (remote && remote->http_proxy)
		curl_http_proxy = xstrdup(remote->http_proxy);

	pragma_header = curl_slist_append(pragma_header, "Pragma: no-cache");
	no_pragma_header = curl_slist_append(no_pragma_header, "Pragma:");

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

	set_from_env(&ssl_cert, "GIT_SSL_CERT");
#if LIBCURL_VERSION_NUM >= 0x070903
	set_from_env(&ssl_key, "GIT_SSL_KEY");
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
	set_from_env(&ssl_capath, "GIT_SSL_CAPATH");
#endif
	set_from_env(&ssl_cainfo, "GIT_SSL_CAINFO");

	set_from_env(&user_agent, "GIT_HTTP_USER_AGENT");

	low_speed_limit = getenv("GIT_HTTP_LOW_SPEED_LIMIT");
	if (low_speed_limit != NULL)
		curl_low_speed_limit = strtol(low_speed_limit, NULL, 10);
	low_speed_time = getenv("GIT_HTTP_LOW_SPEED_TIME");
	if (low_speed_time != NULL)
		curl_low_speed_time = strtol(low_speed_time, NULL, 10);

	if (curl_ssl_verify == -1)
		curl_ssl_verify = 1;

	curl_session_count = 0;
#ifdef USE_CURL_MULTI
	if (max_requests < 1)
		max_requests = DEFAULT_MAX_REQUESTS;
#endif

	if (getenv("GIT_CURL_FTP_NO_EPSV"))
		curl_ftp_no_epsv = 1;

	if (url) {
		credential_from_url(&http_auth, url);
		if (!ssl_cert_password_required &&
		    getenv("GIT_SSL_CERT_PASSWORD_PROTECTED") &&
		    !prefixcmp(url, "https://"))
			ssl_cert_password_required = 1;
	}

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

	curl_slist_free_all(no_pragma_header);
	no_pragma_header = NULL;

	if (curl_http_proxy) {
		free((void *)curl_http_proxy);
		curl_http_proxy = NULL;
	}

	if (cert_auth.password != NULL) {
		memset(cert_auth.password, 0, strlen(cert_auth.password));
		free(cert_auth.password);
		cert_auth.password = NULL;
	}
	ssl_cert_password_required = 0;
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
		if (num_transfers < active_requests)
			process_curl_messages();
	}
#endif

	while (slot != NULL && slot->in_use)
		slot = slot->next;

	if (slot == NULL) {
		newslot = xmalloc(sizeof(*newslot));
		newslot->curl = NULL;
		newslot->in_use = 0;
		newslot->next = NULL;

		slot = active_queue_head;
		if (slot == NULL) {
			active_queue_head = newslot;
		} else {
			while (slot->next != NULL)
				slot = slot->next;
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
		curl_session_count++;
	}

	active_requests++;
	slot->in_use = 1;
	slot->results = NULL;
	slot->finished = NULL;
	slot->callback_data = NULL;
	slot->callback_func = NULL;
	curl_easy_setopt(slot->curl, CURLOPT_COOKIEFILE, curl_cookie_file);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, pragma_header);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, curl_errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 0);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
	if (http_auth.password)
		init_curl_http_auth(slot->curl);

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

static struct fill_chain *fill_cfg;

void add_fill_function(void *data, int (*fill)(void *))
{
	struct fill_chain *new = xmalloc(sizeof(*new));
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
		if (!slot->in_use && slot->curl != NULL
			&& curl_session_count > min_curl_sessions) {
			curl_easy_cleanup(slot->curl);
			slot->curl = NULL;
			curl_session_count--;
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
	fd_set readfds;
	fd_set writefds;
	fd_set excfds;
	int max_fd;
	struct timeval select_timeout;
	int finished = 0;

	slot->finished = &finished;
	while (!finished) {
		step_active_slots();

		if (slot->in_use) {
#if LIBCURL_VERSION_NUM >= 0x070f04
			long curl_timeout;
			curl_multi_timeout(curlm, &curl_timeout);
			if (curl_timeout == 0) {
				continue;
			} else if (curl_timeout == -1) {
				select_timeout.tv_sec  = 0;
				select_timeout.tv_usec = 50000;
			} else {
				select_timeout.tv_sec  =  curl_timeout / 1000;
				select_timeout.tv_usec = (curl_timeout % 1000) * 1000;
			}
#else
			select_timeout.tv_sec  = 0;
			select_timeout.tv_usec = 50000;
#endif

			max_fd = -1;
			FD_ZERO(&readfds);
			FD_ZERO(&writefds);
			FD_ZERO(&excfds);
			curl_multi_fdset(curlm, &readfds, &writefds, &excfds, &max_fd);

			select(max_fd+1, &readfds, &writefds, &excfds, &select_timeout);
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

static void release_active_slot(struct active_request_slot *slot)
{
	closedown_active_slot(slot);
	if (slot->curl && curl_session_count > min_curl_sessions) {
#ifdef USE_CURL_MULTI
		curl_multi_remove_handle(curlm, slot->curl);
#endif
		curl_easy_cleanup(slot->curl);
		slot->curl = NULL;
		curl_session_count--;
	}
#ifdef USE_CURL_MULTI
	fill_active_slots();
#endif
}

void finish_active_slot(struct active_request_slot *slot)
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
	if (slot->callback_func != NULL)
		slot->callback_func(slot->callback_data);
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

/* Helpers for modifying and creating URLs */
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

static char *quote_ref_url(const char *base, const char *ref)
{
	struct strbuf buf = STRBUF_INIT;
	const char *cp;
	int ch;

	end_url_with_slash(&buf, base);

	for (cp = ref; (ch = *cp) != 0; cp++)
		if (needs_quote(ch))
			strbuf_addf(&buf, "%%%02x", ch);
		else
			strbuf_addch(&buf, *cp);

	return strbuf_detach(&buf, NULL);
}

void append_remote_object_url(struct strbuf *buf, const char *url,
			      const char *hex,
			      int only_two_digit_prefix)
{
	end_url_with_slash(buf, url);

	strbuf_addf(buf, "objects/%.*s/", 2, hex);
	if (!only_two_digit_prefix)
		strbuf_addf(buf, "%s", hex+2);
}

char *get_remote_object_url(const char *url, const char *hex,
			    int only_two_digit_prefix)
{
	struct strbuf buf = STRBUF_INIT;
	append_remote_object_url(&buf, url, hex, only_two_digit_prefix);
	return strbuf_detach(&buf, NULL);
}

/* http_request() targets */
#define HTTP_REQUEST_STRBUF	0
#define HTTP_REQUEST_FILE	1

static int http_request(const char *url, void *result, int target, int options)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct curl_slist *headers = NULL;
	struct strbuf buf = STRBUF_INIT;
	int ret;

	slot = get_active_slot();
	slot->results = &results;
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);

	if (result == NULL) {
		curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 1);
	} else {
		curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
		curl_easy_setopt(slot->curl, CURLOPT_FILE, result);

		if (target == HTTP_REQUEST_FILE) {
			long posn = ftell(result);
			curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
					 fwrite);
			if (posn > 0) {
				strbuf_addf(&buf, "Range: bytes=%ld-", posn);
				headers = curl_slist_append(headers, buf.buf);
				strbuf_reset(&buf);
			}
		} else
			curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
					 fwrite_buffer);
	}

	strbuf_addstr(&buf, "Pragma:");
	if (options & HTTP_NO_CACHE)
		strbuf_addstr(&buf, " no-cache");

	headers = curl_slist_append(headers, buf.buf);

	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, headers);

	if (start_active_slot(slot)) {
		run_active_slot(slot);
		if (results.curl_result == CURLE_OK)
			ret = HTTP_OK;
		else if (missing_target(&results))
			ret = HTTP_MISSING_TARGET;
		else if (results.http_code == 401) {
			if (http_auth.username && http_auth.password) {
				credential_reject(&http_auth);
				ret = HTTP_NOAUTH;
			} else {
				credential_fill(&http_auth);
				init_curl_http_auth(slot->curl);
				ret = HTTP_REAUTH;
			}
		} else {
			if (!curl_errorstr[0])
				strlcpy(curl_errorstr,
					curl_easy_strerror(results.curl_result),
					sizeof(curl_errorstr));
			ret = HTTP_ERROR;
		}
	} else {
		error("Unable to start HTTP request for %s", url);
		ret = HTTP_START_FAILED;
	}

	curl_slist_free_all(headers);
	strbuf_release(&buf);

	if (ret == HTTP_OK)
		credential_approve(&http_auth);

	return ret;
}

static int http_request_reauth(const char *url, void *result, int target,
			       int options)
{
	int ret = http_request(url, result, target, options);
	if (ret != HTTP_REAUTH)
		return ret;
	return http_request(url, result, target, options);
}

int http_get_strbuf(const char *url, struct strbuf *result, int options)
{
	return http_request_reauth(url, result, HTTP_REQUEST_STRBUF, options);
}

/*
 * Downloads a URL and stores the result in the given file.
 *
 * If a previous interrupted download is detected (i.e. a previous temporary
 * file is still around) the download is resumed.
 */
static int http_get_file(const char *url, const char *filename, int options)
{
	int ret;
	struct strbuf tmpfile = STRBUF_INIT;
	FILE *result;

	strbuf_addf(&tmpfile, "%s.temp", filename);
	result = fopen(tmpfile.buf, "a");
	if (! result) {
		error("Unable to open local file %s", tmpfile.buf);
		ret = HTTP_ERROR;
		goto cleanup;
	}

	ret = http_request_reauth(url, result, HTTP_REQUEST_FILE, options);
	fclose(result);

	if ((ret == HTTP_OK) && move_temp_to_file(tmpfile.buf, filename))
		ret = HTTP_ERROR;
cleanup:
	strbuf_release(&tmpfile);
	return ret;
}

int http_error(const char *url, int ret)
{
	/* http_request has already handled HTTP_START_FAILED. */
	if (ret != HTTP_START_FAILED)
		error("%s while accessing %s", curl_errorstr, url);

	return ret;
}

int http_fetch_ref(const char *base, struct ref *ref)
{
	char *url;
	struct strbuf buffer = STRBUF_INIT;
	int ret = -1;

	url = quote_ref_url(base, ref->name);
	if (http_get_strbuf(url, &buffer, HTTP_NO_CACHE) == HTTP_OK) {
		strbuf_rtrim(&buffer);
		if (buffer.len == 40)
			ret = get_sha1_hex(buffer.buf, ref->old_sha1);
		else if (!prefixcmp(buffer.buf, "ref: ")) {
			ref->symref = xstrdup(buffer.buf + 5);
			ret = 0;
		}
	}

	strbuf_release(&buffer);
	free(url);
	return ret;
}

/* Helpers for fetching packs */
static char *fetch_pack_index(unsigned char *sha1, const char *base_url)
{
	char *url, *tmp;
	struct strbuf buf = STRBUF_INIT;

	if (http_is_verbose)
		fprintf(stderr, "Getting index for pack %s\n", sha1_to_hex(sha1));

	end_url_with_slash(&buf, base_url);
	strbuf_addf(&buf, "objects/pack/pack-%s.idx", sha1_to_hex(sha1));
	url = strbuf_detach(&buf, NULL);

	strbuf_addf(&buf, "%s.temp", sha1_pack_index_name(sha1));
	tmp = strbuf_detach(&buf, NULL);

	if (http_get_file(url, tmp, 0) != HTTP_OK) {
		error("Unable to get pack index %s\n", url);
		free(tmp);
		tmp = NULL;
	}

	free(url);
	return tmp;
}

static int fetch_and_setup_pack_index(struct packed_git **packs_head,
	unsigned char *sha1, const char *base_url)
{
	struct packed_git *new_pack;
	char *tmp_idx = NULL;
	int ret;

	if (has_pack_index(sha1)) {
		new_pack = parse_pack_index(sha1, NULL);
		if (!new_pack)
			return -1; /* parse_pack_index() already issued error message */
		goto add_pack;
	}

	tmp_idx = fetch_pack_index(sha1, base_url);
	if (!tmp_idx)
		return -1;

	new_pack = parse_pack_index(sha1, tmp_idx);
	if (!new_pack) {
		unlink(tmp_idx);
		free(tmp_idx);

		return -1; /* parse_pack_index() already issued error message */
	}

	ret = verify_pack_index(new_pack);
	if (!ret) {
		close_pack_index(new_pack);
		ret = move_temp_to_file(tmp_idx, sha1_pack_index_name(sha1));
	}
	free(tmp_idx);
	if (ret)
		return -1;

add_pack:
	new_pack->next = *packs_head;
	*packs_head = new_pack;
	return 0;
}

int http_get_info_packs(const char *base_url, struct packed_git **packs_head)
{
	int ret = 0, i = 0;
	char *url, *data;
	struct strbuf buf = STRBUF_INIT;
	unsigned char sha1[20];

	end_url_with_slash(&buf, base_url);
	strbuf_addstr(&buf, "objects/info/packs");
	url = strbuf_detach(&buf, NULL);

	ret = http_get_strbuf(url, &buf, HTTP_NO_CACHE);
	if (ret != HTTP_OK)
		goto cleanup;

	data = buf.buf;
	while (i < buf.len) {
		switch (data[i]) {
		case 'P':
			i++;
			if (i + 52 <= buf.len &&
			    !prefixcmp(data + i, " pack-") &&
			    !prefixcmp(data + i + 46, ".pack\n")) {
				get_sha1_hex(data + i + 6, sha1);
				fetch_and_setup_pack_index(packs_head, sha1,
						      base_url);
				i += 51;
				break;
			}
		default:
			while (i < buf.len && data[i] != '\n')
				i++;
		}
		i++;
	}

cleanup:
	free(url);
	return ret;
}

void release_http_pack_request(struct http_pack_request *preq)
{
	if (preq->packfile != NULL) {
		fclose(preq->packfile);
		preq->packfile = NULL;
	}
	if (preq->range_header != NULL) {
		curl_slist_free_all(preq->range_header);
		preq->range_header = NULL;
	}
	preq->slot = NULL;
	free(preq->url);
}

int finish_http_pack_request(struct http_pack_request *preq)
{
	struct packed_git **lst;
	struct packed_git *p = preq->target;
	char *tmp_idx;
	struct child_process ip;
	const char *ip_argv[8];

	close_pack_index(p);

	fclose(preq->packfile);
	preq->packfile = NULL;

	lst = preq->lst;
	while (*lst != p)
		lst = &((*lst)->next);
	*lst = (*lst)->next;

	tmp_idx = xstrdup(preq->tmpfile);
	strcpy(tmp_idx + strlen(tmp_idx) - strlen(".pack.temp"),
	       ".idx.temp");

	ip_argv[0] = "index-pack";
	ip_argv[1] = "-o";
	ip_argv[2] = tmp_idx;
	ip_argv[3] = preq->tmpfile;
	ip_argv[4] = NULL;

	memset(&ip, 0, sizeof(ip));
	ip.argv = ip_argv;
	ip.git_cmd = 1;
	ip.no_stdin = 1;
	ip.no_stdout = 1;

	if (run_command(&ip)) {
		unlink(preq->tmpfile);
		unlink(tmp_idx);
		free(tmp_idx);
		return -1;
	}

	unlink(sha1_pack_index_name(p->sha1));

	if (move_temp_to_file(preq->tmpfile, sha1_pack_name(p->sha1))
	 || move_temp_to_file(tmp_idx, sha1_pack_index_name(p->sha1))) {
		free(tmp_idx);
		return -1;
	}

	install_packed_git(p);
	free(tmp_idx);
	return 0;
}

struct http_pack_request *new_http_pack_request(
	struct packed_git *target, const char *base_url)
{
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct strbuf buf = STRBUF_INIT;
	struct http_pack_request *preq;

	preq = xcalloc(1, sizeof(*preq));
	preq->target = target;

	end_url_with_slash(&buf, base_url);
	strbuf_addf(&buf, "objects/pack/pack-%s.pack",
		sha1_to_hex(target->sha1));
	preq->url = strbuf_detach(&buf, NULL);

	snprintf(preq->tmpfile, sizeof(preq->tmpfile), "%s.temp",
		sha1_pack_name(target->sha1));
	preq->packfile = fopen(preq->tmpfile, "a");
	if (!preq->packfile) {
		error("Unable to open local file %s for pack",
		      preq->tmpfile);
		goto abort;
	}

	preq->slot = get_active_slot();
	curl_easy_setopt(preq->slot->curl, CURLOPT_FILE, preq->packfile);
	curl_easy_setopt(preq->slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(preq->slot->curl, CURLOPT_URL, preq->url);
	curl_easy_setopt(preq->slot->curl, CURLOPT_HTTPHEADER,
		no_pragma_header);

	/*
	 * If there is data present from a previous transfer attempt,
	 * resume where it left off
	 */
	prev_posn = ftell(preq->packfile);
	if (prev_posn>0) {
		if (http_is_verbose)
			fprintf(stderr,
				"Resuming fetch of pack %s at byte %ld\n",
				sha1_to_hex(target->sha1), prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		preq->range_header = curl_slist_append(NULL, range);
		curl_easy_setopt(preq->slot->curl, CURLOPT_HTTPHEADER,
			preq->range_header);
	}

	return preq;

abort:
	free(preq->url);
	free(preq);
	return NULL;
}

/* Helpers for fetching objects (loose) */
static size_t fwrite_sha1_file(char *ptr, size_t eltsize, size_t nmemb,
			       void *data)
{
	unsigned char expn[4096];
	size_t size = eltsize * nmemb;
	int posn = 0;
	struct http_object_request *freq =
		(struct http_object_request *)data;
	do {
		ssize_t retval = xwrite(freq->localfile,
					(char *) ptr + posn, size - posn);
		if (retval < 0)
			return posn;
		posn += retval;
	} while (posn < size);

	freq->stream.avail_in = size;
	freq->stream.next_in = (void *)ptr;
	do {
		freq->stream.next_out = expn;
		freq->stream.avail_out = sizeof(expn);
		freq->zret = git_inflate(&freq->stream, Z_SYNC_FLUSH);
		git_SHA1_Update(&freq->c, expn,
				sizeof(expn) - freq->stream.avail_out);
	} while (freq->stream.avail_in && freq->zret == Z_OK);
	return size;
}

struct http_object_request *new_http_object_request(const char *base_url,
	unsigned char *sha1)
{
	char *hex = sha1_to_hex(sha1);
	char *filename;
	char prevfile[PATH_MAX];
	int prevlocal;
	char prev_buf[PREV_BUF_SIZE];
	ssize_t prev_read = 0;
	long prev_posn = 0;
	char range[RANGE_HEADER_SIZE];
	struct curl_slist *range_header = NULL;
	struct http_object_request *freq;

	freq = xcalloc(1, sizeof(*freq));
	hashcpy(freq->sha1, sha1);
	freq->localfile = -1;

	filename = sha1_file_name(sha1);
	snprintf(freq->tmpfile, sizeof(freq->tmpfile),
		 "%s.temp", filename);

	snprintf(prevfile, sizeof(prevfile), "%s.prev", filename);
	unlink_or_warn(prevfile);
	rename(freq->tmpfile, prevfile);
	unlink_or_warn(freq->tmpfile);

	if (freq->localfile != -1)
		error("fd leakage in start: %d", freq->localfile);
	freq->localfile = open(freq->tmpfile,
			       O_WRONLY | O_CREAT | O_EXCL, 0666);
	/*
	 * This could have failed due to the "lazy directory creation";
	 * try to mkdir the last path component.
	 */
	if (freq->localfile < 0 && errno == ENOENT) {
		char *dir = strrchr(freq->tmpfile, '/');
		if (dir) {
			*dir = 0;
			mkdir(freq->tmpfile, 0777);
			*dir = '/';
		}
		freq->localfile = open(freq->tmpfile,
				       O_WRONLY | O_CREAT | O_EXCL, 0666);
	}

	if (freq->localfile < 0) {
		error("Couldn't create temporary file %s: %s",
		      freq->tmpfile, strerror(errno));
		goto abort;
	}

	git_inflate_init(&freq->stream);

	git_SHA1_Init(&freq->c);

	freq->url = get_remote_object_url(base_url, hex, 0);

	/*
	 * If a previous temp file is present, process what was already
	 * fetched.
	 */
	prevlocal = open(prevfile, O_RDONLY);
	if (prevlocal != -1) {
		do {
			prev_read = xread(prevlocal, prev_buf, PREV_BUF_SIZE);
			if (prev_read>0) {
				if (fwrite_sha1_file(prev_buf,
						     1,
						     prev_read,
						     freq) == prev_read) {
					prev_posn += prev_read;
				} else {
					prev_read = -1;
				}
			}
		} while (prev_read > 0);
		close(prevlocal);
	}
	unlink_or_warn(prevfile);

	/*
	 * Reset inflate/SHA1 if there was an error reading the previous temp
	 * file; also rewind to the beginning of the local file.
	 */
	if (prev_read == -1) {
		memset(&freq->stream, 0, sizeof(freq->stream));
		git_inflate_init(&freq->stream);
		git_SHA1_Init(&freq->c);
		if (prev_posn>0) {
			prev_posn = 0;
			lseek(freq->localfile, 0, SEEK_SET);
			if (ftruncate(freq->localfile, 0) < 0) {
				error("Couldn't truncate temporary file %s: %s",
					  freq->tmpfile, strerror(errno));
				goto abort;
			}
		}
	}

	freq->slot = get_active_slot();

	curl_easy_setopt(freq->slot->curl, CURLOPT_FILE, freq);
	curl_easy_setopt(freq->slot->curl, CURLOPT_WRITEFUNCTION, fwrite_sha1_file);
	curl_easy_setopt(freq->slot->curl, CURLOPT_ERRORBUFFER, freq->errorstr);
	curl_easy_setopt(freq->slot->curl, CURLOPT_URL, freq->url);
	curl_easy_setopt(freq->slot->curl, CURLOPT_HTTPHEADER, no_pragma_header);

	/*
	 * If we have successfully processed data from a previous fetch
	 * attempt, only fetch the data we don't already have.
	 */
	if (prev_posn>0) {
		if (http_is_verbose)
			fprintf(stderr,
				"Resuming fetch of object %s at byte %ld\n",
				hex, prev_posn);
		sprintf(range, "Range: bytes=%ld-", prev_posn);
		range_header = curl_slist_append(range_header, range);
		curl_easy_setopt(freq->slot->curl,
				 CURLOPT_HTTPHEADER, range_header);
	}

	return freq;

abort:
	free(freq->url);
	free(freq);
	return NULL;
}

void process_http_object_request(struct http_object_request *freq)
{
	if (freq->slot == NULL)
		return;
	freq->curl_result = freq->slot->curl_result;
	freq->http_code = freq->slot->http_code;
	freq->slot = NULL;
}

int finish_http_object_request(struct http_object_request *freq)
{
	struct stat st;

	close(freq->localfile);
	freq->localfile = -1;

	process_http_object_request(freq);

	if (freq->http_code == 416) {
		warning("requested range invalid; we may already have all the data.");
	} else if (freq->curl_result != CURLE_OK) {
		if (stat(freq->tmpfile, &st) == 0)
			if (st.st_size == 0)
				unlink_or_warn(freq->tmpfile);
		return -1;
	}

	git_inflate_end(&freq->stream);
	git_SHA1_Final(freq->real_sha1, &freq->c);
	if (freq->zret != Z_STREAM_END) {
		unlink_or_warn(freq->tmpfile);
		return -1;
	}
	if (hashcmp(freq->sha1, freq->real_sha1)) {
		unlink_or_warn(freq->tmpfile);
		return -1;
	}
	freq->rename =
		move_temp_to_file(freq->tmpfile, sha1_file_name(freq->sha1));

	return freq->rename;
}

void abort_http_object_request(struct http_object_request *freq)
{
	unlink_or_warn(freq->tmpfile);

	release_http_object_request(freq);
}

void release_http_object_request(struct http_object_request *freq)
{
	if (freq->localfile != -1) {
		close(freq->localfile);
		freq->localfile = -1;
	}
	if (freq->url != NULL) {
		free(freq->url);
		freq->url = NULL;
	}
	if (freq->slot != NULL) {
		freq->slot->callback_func = NULL;
		freq->slot->callback_data = NULL;
		release_active_slot(freq->slot);
		freq->slot = NULL;
	}
}
