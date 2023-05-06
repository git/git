#include "../git-compat-util.h"
#include "../git-curl-compat.h"
#include <dlfcn.h>

/*
 * The ABI version of libcurl is encoded in its shared libraries' file names.
 * This ABI version has not changed since October 2006 and is unlikely to be
 * changed in the future. See https://curl.se/libcurl/abi.html for details.
 */
#define LIBCURL_ABI_VERSION "4"

typedef void (*func_t)(void);

#ifdef __APPLE__
#define LIBCURL_FILE_NAME(base) base "." LIBCURL_ABI_VERSION ".dylib"
#else
#define LIBCURL_FILE_NAME(base) base ".so." LIBCURL_ABI_VERSION
#endif

static void *load_library(const char *name)
{
	return dlopen(name, RTLD_LAZY);
}

static func_t load_function(void *handle, const char *name)
{
	/*
	 * Casting the return value of `dlsym()` to a function pointer is
	 * explicitly allowed in recent POSIX standards, but GCC complains
	 * about this in pedantic mode nevertheless. For more about this issue,
	 * see https://stackoverflow.com/q/31526876/1860823 and
	 * http://stackoverflow.com/a/36385690/1905491.
	 */
	func_t f;
	*(void **)&f = dlsym(handle, name);
	return f;
}

typedef struct curl_version_info_data *(*curl_version_info_type)(CURLversion version);
static curl_version_info_type curl_version_info_func;

typedef char *(*curl_easy_escape_type)(CURL *handle, const char *string, int length);
static curl_easy_escape_type curl_easy_escape_func;

typedef void (*curl_free_type)(void *p);
static curl_free_type curl_free_func;

typedef CURLcode (*curl_global_init_type)(long flags);
static curl_global_init_type curl_global_init_func;

typedef CURLsslset (*curl_global_sslset_type)(curl_sslbackend id, const char *name, const curl_ssl_backend ***avail);
static curl_global_sslset_type curl_global_sslset_func;

typedef void (*curl_global_cleanup_type)(void);
static curl_global_cleanup_type curl_global_cleanup_func;

typedef struct curl_slist *(*curl_slist_append_type)(struct curl_slist *list, const char *data);
static curl_slist_append_type curl_slist_append_func;

typedef void (*curl_slist_free_all_type)(struct curl_slist *list);
static curl_slist_free_all_type curl_slist_free_all_func;

typedef const char *(*curl_easy_strerror_type)(CURLcode error);
static curl_easy_strerror_type curl_easy_strerror_func;

typedef CURLM *(*curl_multi_init_type)(void);
static curl_multi_init_type curl_multi_init_func;

typedef CURLMcode (*curl_multi_add_handle_type)(CURLM *multi_handle, CURL *curl_handle);
static curl_multi_add_handle_type curl_multi_add_handle_func;

typedef CURLMcode (*curl_multi_remove_handle_type)(CURLM *multi_handle, CURL *curl_handle);
static curl_multi_remove_handle_type curl_multi_remove_handle_func;

typedef CURLMcode (*curl_multi_fdset_type)(CURLM *multi_handle, fd_set *read_fd_set, fd_set *write_fd_set, fd_set *exc_fd_set, int *max_fd);
static curl_multi_fdset_type curl_multi_fdset_func;

typedef CURLMcode (*curl_multi_perform_type)(CURLM *multi_handle, int *running_handles);
static curl_multi_perform_type curl_multi_perform_func;

typedef CURLMcode (*curl_multi_cleanup_type)(CURLM *multi_handle);
static curl_multi_cleanup_type curl_multi_cleanup_func;

typedef CURLMsg *(*curl_multi_info_read_type)(CURLM *multi_handle, int *msgs_in_queue);
static curl_multi_info_read_type curl_multi_info_read_func;

typedef const char *(*curl_multi_strerror_type)(CURLMcode error);
static curl_multi_strerror_type curl_multi_strerror_func;

typedef CURLMcode (*curl_multi_timeout_type)(CURLM *multi_handle, long *milliseconds);
static curl_multi_timeout_type curl_multi_timeout_func;

typedef CURL *(*curl_easy_init_type)(void);
static curl_easy_init_type curl_easy_init_func;

typedef CURLcode (*curl_easy_perform_type)(CURL *curl);
static curl_easy_perform_type curl_easy_perform_func;

typedef void (*curl_easy_cleanup_type)(CURL *curl);
static curl_easy_cleanup_type curl_easy_cleanup_func;

typedef CURL *(*curl_easy_duphandle_type)(CURL *curl);
static curl_easy_duphandle_type curl_easy_duphandle_func;

typedef CURLcode (*curl_easy_getinfo_long_type)(CURL *curl, CURLINFO info, long *value);
static curl_easy_getinfo_long_type curl_easy_getinfo_long_func;

typedef CURLcode (*curl_easy_getinfo_pointer_type)(CURL *curl, CURLINFO info, void **value);
static curl_easy_getinfo_pointer_type curl_easy_getinfo_pointer_func;

typedef CURLcode (*curl_easy_getinfo_off_t_type)(CURL *curl, CURLINFO info, curl_off_t *value);
static curl_easy_getinfo_off_t_type curl_easy_getinfo_off_t_func;

typedef CURLcode (*curl_easy_setopt_long_type)(CURL *curl, CURLoption opt, long value);
static curl_easy_setopt_long_type curl_easy_setopt_long_func;

typedef CURLcode (*curl_easy_setopt_pointer_type)(CURL *curl, CURLoption opt, void *value);
static curl_easy_setopt_pointer_type curl_easy_setopt_pointer_func;

typedef CURLcode (*curl_easy_setopt_off_t_type)(CURL *curl, CURLoption opt, curl_off_t value);
static curl_easy_setopt_off_t_type curl_easy_setopt_off_t_func;

static void lazy_load_curl(void)
{
	static int initialized;
	void *libcurl;
	func_t curl_easy_getinfo_func, curl_easy_setopt_func;

	if (initialized)
		return;

	initialized = 1;
	libcurl = load_library(LIBCURL_FILE_NAME("libcurl"));
	if (!libcurl)
		die("failed to load library '%s'", LIBCURL_FILE_NAME("libcurl"));

	curl_version_info_func = (curl_version_info_type)load_function(libcurl, "curl_version_info");
	curl_easy_escape_func = (curl_easy_escape_type)load_function(libcurl, "curl_easy_escape");
	curl_free_func = (curl_free_type)load_function(libcurl, "curl_free");
	curl_global_init_func = (curl_global_init_type)load_function(libcurl, "curl_global_init");
	curl_global_sslset_func = (curl_global_sslset_type)load_function(libcurl, "curl_global_sslset");
	curl_global_cleanup_func = (curl_global_cleanup_type)load_function(libcurl, "curl_global_cleanup");
	curl_slist_append_func = (curl_slist_append_type)load_function(libcurl, "curl_slist_append");
	curl_slist_free_all_func = (curl_slist_free_all_type)load_function(libcurl, "curl_slist_free_all");
	curl_easy_strerror_func = (curl_easy_strerror_type)load_function(libcurl, "curl_easy_strerror");
	curl_multi_init_func = (curl_multi_init_type)load_function(libcurl, "curl_multi_init");
	curl_multi_add_handle_func = (curl_multi_add_handle_type)load_function(libcurl, "curl_multi_add_handle");
	curl_multi_remove_handle_func = (curl_multi_remove_handle_type)load_function(libcurl, "curl_multi_remove_handle");
	curl_multi_fdset_func = (curl_multi_fdset_type)load_function(libcurl, "curl_multi_fdset");
	curl_multi_perform_func = (curl_multi_perform_type)load_function(libcurl, "curl_multi_perform");
	curl_multi_cleanup_func = (curl_multi_cleanup_type)load_function(libcurl, "curl_multi_cleanup");
	curl_multi_info_read_func = (curl_multi_info_read_type)load_function(libcurl, "curl_multi_info_read");
	curl_multi_strerror_func = (curl_multi_strerror_type)load_function(libcurl, "curl_multi_strerror");
	curl_multi_timeout_func = (curl_multi_timeout_type)load_function(libcurl, "curl_multi_timeout");
	curl_easy_init_func = (curl_easy_init_type)load_function(libcurl, "curl_easy_init");
	curl_easy_perform_func = (curl_easy_perform_type)load_function(libcurl, "curl_easy_perform");
	curl_easy_cleanup_func = (curl_easy_cleanup_type)load_function(libcurl, "curl_easy_cleanup");
	curl_easy_duphandle_func = (curl_easy_duphandle_type)load_function(libcurl, "curl_easy_duphandle");

	curl_easy_getinfo_func = load_function(libcurl, "curl_easy_getinfo");
	curl_easy_getinfo_long_func = (curl_easy_getinfo_long_type)curl_easy_getinfo_func;
	curl_easy_getinfo_pointer_func = (curl_easy_getinfo_pointer_type)curl_easy_getinfo_func;
	curl_easy_getinfo_off_t_func = (curl_easy_getinfo_off_t_type)curl_easy_getinfo_func;

	curl_easy_setopt_func = load_function(libcurl, "curl_easy_setopt");
	curl_easy_setopt_long_func = (curl_easy_setopt_long_type)curl_easy_setopt_func;
	curl_easy_setopt_pointer_func = (curl_easy_setopt_pointer_type)curl_easy_setopt_func;
	curl_easy_setopt_off_t_func = (curl_easy_setopt_off_t_type)curl_easy_setopt_func;
}

struct curl_version_info_data *curl_version_info(CURLversion version)
{
	lazy_load_curl();
	return curl_version_info_func(version);
}

char *curl_easy_escape(CURL *handle, const char *string, int length)
{
	lazy_load_curl();
	return curl_easy_escape_func(handle, string, length);
}

void curl_free(void *p)
{
	lazy_load_curl();
	curl_free_func(p);
}

CURLcode curl_global_init(long flags)
{
	lazy_load_curl();
	return curl_global_init_func(flags);
}

CURLsslset curl_global_sslset(curl_sslbackend id, const char *name, const curl_ssl_backend ***avail)
{
	lazy_load_curl();
	return curl_global_sslset_func(id, name, avail);
}

void curl_global_cleanup(void)
{
	lazy_load_curl();
	curl_global_cleanup_func();
}

struct curl_slist *curl_slist_append(struct curl_slist *list, const char *data)
{
	lazy_load_curl();
	return curl_slist_append_func(list, data);
}

void curl_slist_free_all(struct curl_slist *list)
{
	lazy_load_curl();
	curl_slist_free_all_func(list);
}

const char *curl_easy_strerror(CURLcode error)
{
	lazy_load_curl();
	return curl_easy_strerror_func(error);
}

CURLM *curl_multi_init(void)
{
	lazy_load_curl();
	return curl_multi_init_func();
}

CURLMcode curl_multi_add_handle(CURLM *multi_handle, CURL *curl_handle)
{
	lazy_load_curl();
	return curl_multi_add_handle_func(multi_handle, curl_handle);
}

CURLMcode curl_multi_remove_handle(CURLM *multi_handle, CURL *curl_handle)
{
	lazy_load_curl();
	return curl_multi_remove_handle_func(multi_handle, curl_handle);
}

CURLMcode curl_multi_fdset(CURLM *multi_handle, fd_set *read_fd_set, fd_set *write_fd_set, fd_set *exc_fd_set, int *max_fd)
{
	lazy_load_curl();
	return curl_multi_fdset_func(multi_handle, read_fd_set, write_fd_set, exc_fd_set, max_fd);
}

CURLMcode curl_multi_perform(CURLM *multi_handle, int *running_handles)
{
	lazy_load_curl();
	return curl_multi_perform_func(multi_handle, running_handles);
}

CURLMcode curl_multi_cleanup(CURLM *multi_handle)
{
	lazy_load_curl();
	return curl_multi_cleanup_func(multi_handle);
}

CURLMsg *curl_multi_info_read(CURLM *multi_handle, int *msgs_in_queue)
{
	lazy_load_curl();
	return curl_multi_info_read_func(multi_handle, msgs_in_queue);
}

const char *curl_multi_strerror(CURLMcode error)
{
	lazy_load_curl();
	return curl_multi_strerror_func(error);
}

CURLMcode curl_multi_timeout(CURLM *multi_handle, long *milliseconds)
{
	lazy_load_curl();
	return curl_multi_timeout_func(multi_handle, milliseconds);
}

CURL *curl_easy_init(void)
{
	lazy_load_curl();
	return curl_easy_init_func();
}

CURLcode curl_easy_perform(CURL *curl)
{
	lazy_load_curl();
	return curl_easy_perform_func(curl);
}

void curl_easy_cleanup(CURL *curl)
{
	lazy_load_curl();
	curl_easy_cleanup_func(curl);
}

CURL *curl_easy_duphandle(CURL *curl)
{
	lazy_load_curl();
	return curl_easy_duphandle_func(curl);
}

#ifndef CURL_IGNORE_DEPRECATION
#define CURL_IGNORE_DEPRECATION(x) x
#endif

#ifndef CURLOPTTYPE_BLOB
#define CURLOPTTYPE_BLOB 40000
#endif

#undef curl_easy_getinfo
CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...)
{
	va_list ap;
	CURLcode res;

	va_start(ap, info);
	lazy_load_curl();
	CURL_IGNORE_DEPRECATION(
		if (info >= CURLINFO_LONG && info < CURLINFO_DOUBLE)
			res = curl_easy_getinfo_long_func(curl, info, va_arg(ap, long *));
		else if ((info >= CURLINFO_STRING && info < CURLINFO_LONG) ||
			 (info >= CURLINFO_SLIST && info < CURLINFO_SOCKET))
			res = curl_easy_getinfo_pointer_func(curl, info, va_arg(ap, void **));
		else if (info >= CURLINFO_OFF_T)
			res = curl_easy_getinfo_off_t_func(curl, info, va_arg(ap, curl_off_t *));
		else
			die("%s:%d: TODO (info: %d)!", __FILE__, __LINE__, info);
	)
	va_end(ap);
	return res;
}

#undef curl_easy_setopt
CURLcode curl_easy_setopt(CURL *curl, CURLoption opt, ...)
{
	va_list ap;
	CURLcode res;

	va_start(ap, opt);
	lazy_load_curl();
	CURL_IGNORE_DEPRECATION(
		if (opt >= CURLOPTTYPE_LONG && opt < CURLOPTTYPE_OBJECTPOINT)
			res = curl_easy_setopt_long_func(curl, opt, va_arg(ap, long));
		else if (opt >= CURLOPTTYPE_OBJECTPOINT && opt < CURLOPTTYPE_OFF_T)
			res = curl_easy_setopt_pointer_func(curl, opt, va_arg(ap, void *));
		else if (opt >= CURLOPTTYPE_OFF_T && opt < CURLOPTTYPE_BLOB)
			res = curl_easy_setopt_off_t_func(curl, opt, va_arg(ap, curl_off_t));
		else
			die("%s:%d: TODO (opt: %d)!", __FILE__, __LINE__, opt);
	)
	va_end(ap);
	return res;
}
