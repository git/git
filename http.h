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
 * This is not something Makefile should set nor users should pass
 * via CFLAGS.
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

struct slot_results {
	CURLcode curl_result;
	long http_code;
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
extern size_t fread_buffer(char *ptr, size_t eltsize, size_t nmemb, void *strbuf);
extern size_t fwrite_buffer(char *ptr, size_t eltsize, size_t nmemb, void *strbuf);
extern size_t fwrite_null(char *ptr, size_t eltsize, size_t nmemb, void *strbuf);
#ifndef NO_CURL_IOCTL
extern curlioerr ioctl_buffer(CURL *handle, int cmd, void *clientp);
#endif

/* Slot lifecycle functions */
extern struct active_request_slot *get_active_slot(void);
extern int start_active_slot(struct active_request_slot *slot);
extern void run_active_slot(struct active_request_slot *slot);
extern void finish_active_slot(struct active_request_slot *slot);
extern void finish_all_active_slots(void);

#ifdef USE_CURL_MULTI
extern void fill_active_slots(void);
extern void add_fill_function(void *data, int (*fill)(void *));
extern void step_active_slots(void);
#endif

extern void http_init(struct remote *remote, const char *url,
		      int proactive_auth);
extern void http_cleanup(void);

extern int active_requests;
extern int http_is_verbose;
extern size_t http_post_buffer;

extern char curl_errorstr[CURL_ERROR_SIZE];

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

/* Helpers for modifying and creating URLs */
extern void append_remote_object_url(struct strbuf *buf, const char *url,
				     const char *hex,
				     int only_two_digit_prefix);
extern char *get_remote_object_url(const char *url, const char *hex,
				   int only_two_digit_prefix);

/* Options for http_request_*() */
#define HTTP_NO_CACHE		1

/* Return values for http_request_*() */
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
int http_get_strbuf(const char *url, struct strbuf *result, int options);

/*
 * Prints an error message using error() containing url and curl_errorstr,
 * and returns ret.
 */
int http_error(const char *url, int ret);

extern int http_fetch_ref(const char *base, struct ref *ref);

/* Helpers for fetching packs */
extern int http_get_info_packs(const char *base_url,
	struct packed_git **packs_head);

struct http_pack_request {
	char *url;
	struct packed_git *target;
	struct packed_git **lst;
	FILE *packfile;
	char tmpfile[PATH_MAX];
	struct curl_slist *range_header;
	struct active_request_slot *slot;
};

extern struct http_pack_request *new_http_pack_request(
	struct packed_git *target, const char *base_url);
extern int finish_http_pack_request(struct http_pack_request *preq);
extern void release_http_pack_request(struct http_pack_request *preq);

/* Helpers for fetching object */
struct http_object_request {
	char *url;
	char tmpfile[PATH_MAX];
	int localfile;
	CURLcode curl_result;
	char errorstr[CURL_ERROR_SIZE];
	long http_code;
	unsigned char sha1[20];
	unsigned char real_sha1[20];
	git_SHA_CTX c;
	git_zstream stream;
	int zret;
	int rename;
	struct active_request_slot *slot;
};

extern struct http_object_request *new_http_object_request(
	const char *base_url, unsigned char *sha1);
extern void process_http_object_request(struct http_object_request *freq);
extern int finish_http_object_request(struct http_object_request *freq);
extern void abort_http_object_request(struct http_object_request *freq);
extern void release_http_object_request(struct http_object_request *freq);

#endif /* HTTP_H */
