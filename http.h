#ifndef HTTP_H
#define HTTP_H

#include "cache.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include "strbuf.h"
#include "remote.h"
#include "url.h"

/*
 * We detect based on the cURL version if multi-transfer is
 * usable in this implementation and define this symbol accordingly.
 * This shouldn't be set by the Makefile or by the user (e.g. via CFLAGS).
 */
#undef USE_CURL_MULTI

#if LIBCURL_VERSION_NUM >= 0x071000
#define USE_CURL_MULTI
#define DEFAULT_MAX_REQUESTS 5
#endif

#if LIBCURL_VERSION_NUM < 0x070704
#define curl_global_cleanup() do { /* nothing */ } while (0)
#endif

#if LIBCURL_VERSION_NUM < 0x070800
#define curl_global_init(a) do { /* nothing */ } while (0)
#elif LIBCURL_VERSION_NUM >= 0x070c00
#define curl_global_init(a) curl_global_init_mem(a, xmalloc, free, \
						xrealloc, xstrdup, xcalloc)
#endif

#if (LIBCURL_VERSION_NUM < 0x070c04) || (LIBCURL_VERSION_NUM == 0x071000)
#define NO_CURL_EASY_DUPHANDLE
#endif

#if LIBCURL_VERSION_NUM < 0x070a03
#define CURLE_HTTP_RETURNED_ERROR CURLE_HTTP_NOT_FOUND
#endif

#if LIBCURL_VERSION_NUM < 0x070c03
#define NO_CURL_IOCTL
#endif

/*
 * CURLOPT_USE_SSL was known as CURLOPT_FTP_SSL up to 7.16.4,
 * and the constants were known as CURLFTPSSL_*
*/
#if !defined(CURLOPT_USE_SSL) && defined(CURLOPT_FTP_SSL)
#define CURLOPT_USE_SSL CURLOPT_FTP_SSL
#define CURLUSESSL_TRY CURLFTPSSL_TRY
#endif

struct slot_results {
	CURLcode curl_result;
	long http_code;
	long auth_avail;
	long http_connectcode;
};

struct active_request_slot {
	CURL *curl;
	int in_use;
	CURLcode curl_result;
	long http_code;
	int *finished;
	struct slot_results *results;
	void *callback_data;
	void (*callback_func)(void *data);
	struct active_request_slot *next;
};

struct buffer {
	struct strbuf buf;
	size_t posn;
};

/* Curl request read/write callbacks */
size_t fread_buffer(char *ptr, size_t eltsize, size_t nmemb, void *strbuf);
size_t fwrite_buffer(char *ptr, size_t eltsize, size_t nmemb, void *strbuf);
size_t fwrite_null(char *ptr, size_t eltsize, size_t nmemb, void *strbuf);
#ifndef NO_CURL_IOCTL
curlioerr ioctl_buffer(CURL *handle, int cmd, void *clientp);
#endif

/* Slot lifecycle functions */
struct active_request_slot *get_active_slot(void);
int start_active_slot(struct active_request_slot *slot);
void run_active_slot(struct active_request_slot *slot);
void finish_all_active_slots(void);

/*
 * This will run one slot to completion in a blocking manner, similar to how
 * curl_easy_perform would work (but we don't want to use that, because
 * we do not want to intermingle calls to curl_multi and curl_easy).
 *
 */
int run_one_slot(struct active_request_slot *slot,
		 struct slot_results *results);

#ifdef USE_CURL_MULTI
void fill_active_slots(void);
void add_fill_function(void *data, int (*fill)(void *));
void step_active_slots(void);
#endif

void http_init(struct remote *remote, const char *url,
	       int proactive_auth);
void http_cleanup(void);
struct curl_slist *http_copy_default_headers(void);

extern long int git_curl_ipresolve;
extern int active_requests;
extern int http_is_verbose;
extern ssize_t http_post_buffer;
extern struct credential http_auth;

extern char curl_errorstr[CURL_ERROR_SIZE];

enum http_follow_config {
	HTTP_FOLLOW_NONE,
	HTTP_FOLLOW_ALWAYS,
	HTTP_FOLLOW_INITIAL
};
extern enum http_follow_config http_follow_config;

static inline int missing__target(int code, int result)
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

/*
 * Normalize curl results to handle CURL_FAILONERROR (or lack thereof). Failing
 * http codes have their "result" converted to CURLE_HTTP_RETURNED_ERROR, and
 * an appropriate string placed in the errorstr buffer (pass curl_errorstr if
 * you don't have a custom buffer).
 */
void normalize_curl_result(CURLcode *result, long http_code, char *errorstr,
			   size_t errorlen);

/* Helpers for modifying and creating URLs */
void append_remote_object_url(struct strbuf *buf, const char *url,
			      const char *hex,
			      int only_two_digit_prefix);
char *get_remote_object_url(const char *url, const char *hex,
			    int only_two_digit_prefix);

/* Options for http_get_*() */
struct http_get_options {
	unsigned no_cache:1,
		 initial_request:1;

	/* If non-NULL, returns the content-type of the response. */
	struct strbuf *content_type;

	/*
	 * If non-NULL, and content_type above is non-NULL, returns
	 * the charset parameter from the content-type. If none is
	 * present, returns an empty string.
	 */
	struct strbuf *charset;

	/*
	 * If non-NULL, returns the URL we ended up at, including any
	 * redirects we followed.
	 */
	struct strbuf *effective_url;

	/*
	 * If both base_url and effective_url are non-NULL, the base URL will
	 * be munged to reflect any redirections going from the requested url
	 * to effective_url. See the definition of update_url_from_redirect
	 * for details.
	 */
	struct strbuf *base_url;

	/*
	 * If not NULL, contains additional HTTP headers to be sent with the
	 * request. The strings in the list must not be freed until after the
	 * request has completed.
	 */
	struct string_list *extra_headers;
};

/* Return values for http_get_*() */
#define HTTP_OK			0
#define HTTP_MISSING_TARGET	1
#define HTTP_ERROR		2
#define HTTP_START_FAILED	3
#define HTTP_REAUTH	4
#define HTTP_NOAUTH	5

/*
 * Requests a URL and stores the result in a strbuf.
 *
 * If the result pointer is NULL, a HTTP HEAD request is made instead of GET.
 */
int http_get_strbuf(const char *url, struct strbuf *result, struct http_get_options *options);

int http_fetch_ref(const char *base, struct ref *ref);

/* Helpers for fetching packs */
int http_get_info_packs(const char *base_url,
			struct packed_git **packs_head);

struct http_pack_request {
	char *url;
	struct packed_git *target;
	struct packed_git **lst;
	FILE *packfile;
	struct strbuf tmpfile;
	struct active_request_slot *slot;
};

struct http_pack_request *new_http_pack_request(
	struct packed_git *target, const char *base_url);
int finish_http_pack_request(struct http_pack_request *preq);
void release_http_pack_request(struct http_pack_request *preq);

/* Helpers for fetching object */
struct http_object_request {
	char *url;
	struct strbuf tmpfile;
	int localfile;
	CURLcode curl_result;
	char errorstr[CURL_ERROR_SIZE];
	long http_code;
	struct object_id oid;
	struct object_id real_oid;
	git_hash_ctx c;
	git_zstream stream;
	int zret;
	int rename;
	struct active_request_slot *slot;
};

struct http_object_request *new_http_object_request(
	const char *base_url, const struct object_id *oid);
void process_http_object_request(struct http_object_request *freq);
int finish_http_object_request(struct http_object_request *freq);
void abort_http_object_request(struct http_object_request *freq);
void release_http_object_request(struct http_object_request *freq);

/* setup routine for curl_easy_setopt CURLOPT_DEBUGFUNCTION */
void setup_curl_trace(CURL *handle);
#endif /* HTTP_H */
