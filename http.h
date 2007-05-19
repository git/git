#ifndef HTTP_H
#define HTTP_H

#include "cache.h"

#include <curl/curl.h>
#include <curl/easy.h>

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
        size_t posn;
        size_t size;
        void *buffer;
};

/* Curl request read/write callbacks */
extern size_t fread_buffer(void *ptr, size_t eltsize, size_t nmemb,
			   struct buffer *buffer);
extern size_t fwrite_buffer(const void *ptr, size_t eltsize,
			    size_t nmemb, struct buffer *buffer);
extern size_t fwrite_null(const void *ptr, size_t eltsize,
			  size_t nmemb, struct buffer *buffer);

/* Slot lifecycle functions */
extern struct active_request_slot *get_active_slot(void);
extern int start_active_slot(struct active_request_slot *slot);
extern void run_active_slot(struct active_request_slot *slot);
extern void finish_all_active_slots(void);
extern void release_active_slot(struct active_request_slot *slot);

#ifdef USE_CURL_MULTI
extern void fill_active_slots(void);
extern void step_active_slots(void);
#endif

extern void http_init(void);
extern void http_cleanup(void);

extern int data_received;
extern int active_requests;

#ifdef USE_CURL_MULTI
extern int max_requests;
extern CURLM *curlm;
#endif
#ifndef NO_CURL_EASY_DUPHANDLE
extern CURL *curl_default;
#endif
extern char curl_errorstr[CURL_ERROR_SIZE];

extern int curl_ssl_verify;
extern char *ssl_cert;
#if LIBCURL_VERSION_NUM >= 0x070902
extern char *ssl_key;
#endif
#if LIBCURL_VERSION_NUM >= 0x070908
extern char *ssl_capath;
#endif
extern char *ssl_cainfo;
extern long curl_low_speed_limit;
extern long curl_low_speed_time;

extern struct curl_slist *pragma_header;
extern struct curl_slist *no_range_header;

extern struct active_request_slot *active_queue_head;

#endif /* HTTP_H */
