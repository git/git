#ifndef HTTP_H
#define HTTP_H

#include "cache.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include "strbuf.h"
#include "remote.h"

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
#define curl_global_cleanup() do { /* nothing */ } while(0)
#endif
#if LIBCURL_VERSION_NUM < 0x070800
#define curl_global_init(a) do { /* nothing */ } while(0)
#endif

#if (LIBCURL_VERSION_NUM < 0x070c04) || (LIBCURL_VERSION_NUM == 0x071000)
#define NO_CURL_EASY_DUPHANDLE
#endif

#if LIBCURL_VERSION_NUM < 0x070a03
#define CURLE_HTTP_RETURNED_ERROR CURLE_HTTP_NOT_FOUND
#endif

struct slot_results
{
	CURLcode curl_result;
	long http_code;
};

struct active_request_slot
{
	CURL *curl;
	FILE *local;
	int in_use;
	CURLcode curl_result;
	long http_code;
	int *finished;
	struct slot_results *results;
	void *callback_data;
	void (*callback_func)(void *data);
	struct active_request_slot *next;
};

struct buffer
{
	struct strbuf buf;
	size_t posn;
};

/* Curl request read/write callbacks */
extern size_t fread_buffer(void *ptr, size_t eltsize, size_t nmemb, void *strbuf);
extern size_t fwrite_buffer(const void *ptr, size_t eltsize, size_t nmemb, void *strbuf);
extern size_t fwrite_null(const void *ptr, size_t eltsize, size_t nmemb, void *strbuf);

/* Slot lifecycle functions */
extern struct active_request_slot *get_active_slot(void);
extern int start_active_slot(struct active_request_slot *slot);
extern void run_active_slot(struct active_request_slot *slot);
extern void finish_all_active_slots(void);
extern void release_active_slot(struct active_request_slot *slot);

#ifdef USE_CURL_MULTI
extern void fill_active_slots(void);
extern void add_fill_function(void *data, int (*fill)(void *));
extern void step_active_slots(void);
#endif

extern void http_init(struct remote *remote);
extern void http_cleanup(void);

extern int data_received;
extern int active_requests;

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

extern int http_fetch_ref(const char *base, struct ref *ref);

#endif /* HTTP_H */
