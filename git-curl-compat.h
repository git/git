#ifndef GIT_CURL_COMPAT_H
#define GIT_CURL_COMPAT_H
#include <curl/curl.h>

/**
 * This header centralizes the declaration of our libcurl dependencies
 * to make it easy to discover the oldest versions we support, and to
 * inform decisions about removing support for older libcurl in the
 * future.
 *
 * The oldest supported version of curl is documented in the "INSTALL"
 * document.
 *
 * The source of truth for what versions have which symbols is
 * https://github.com/curl/curl/blob/master/docs/libcurl/symbols-in-versions;
 * the release dates are taken from curl.git (at
 * https://github.com/curl/curl/).
 *
 * For each X symbol we need from curl we define our own
 * GIT_CURL_HAVE_X. If multiple similar symbols with the same prefix
 * were defined in the same version we pick one and check for that name.
 *
 * We may also define a missing CURL_* symbol to its known value, if
 * doing so is sufficient to add support for it to older versions that
 * don't have it.
 *
 * Keep any symbols in date order of when their support was
 * introduced, oldest first, in the official version of cURL library.
 */

/**
 * CURL_SOCKOPT_OK was added in 7.21.5, released in April 2011.
 */
#if LIBCURL_VERSION_NUM < 0x071505
#define CURL_SOCKOPT_OK 0
#endif

/**
 * CURLOPT_TCP_KEEPALIVE was added in 7.25.0, released in March 2012.
 */
#if LIBCURL_VERSION_NUM >= 0x071900
#define GITCURL_HAVE_CURLOPT_TCP_KEEPALIVE 1
#endif


/**
 * CURLOPT_LOGIN_OPTIONS was added in 7.34.0, released in December
 * 2013.
 *
 * If we start requiring 7.34.0 we might also be able to remove the
 * code conditional on USE_CURL_FOR_IMAP_SEND in imap-send.c, see
 * 1e16b255b95 (git-imap-send: use libcurl for implementation,
 * 2014-11-09) and the check it added for "072200" in the Makefile.

 */
#if LIBCURL_VERSION_NUM >= 0x072200
#define GIT_CURL_HAVE_CURLOPT_LOGIN_OPTIONS 1
#endif

/**
 * CURL_SSLVERSION_TLSv1_[012] was added in 7.34.0, released in
 * December 2013.
 */
#if LIBCURL_VERSION_NUM >= 0x072200
#define GIT_CURL_HAVE_CURL_SSLVERSION_TLSv1_0
#endif

/**
 * CURLOPT_PINNEDPUBLICKEY was added in 7.39.0, released in November
 * 2014.
 */
#if LIBCURL_VERSION_NUM >= 0x072c00
#define GIT_CURL_HAVE_CURLOPT_PINNEDPUBLICKEY 1
#endif

/**
 * CURL_HTTP_VERSION_2 was added in 7.43.0, released in June 2015.
 *
 * The CURL_HTTP_VERSION_2 alias (but not CURL_HTTP_VERSION_2_0) has
 * always been a macro, not an enum field (checked on curl version
 * 7.78.0)
 */
#if LIBCURL_VERSION_NUM >= 0x072b00
#define GIT_CURL_HAVE_CURL_HTTP_VERSION_2 1
#endif

/**
 * CURLSSLOPT_NO_REVOKE was added in 7.44.0, released in August 2015.
 *
 * The CURLSSLOPT_NO_REVOKE is, has always been a macro, not an enum
 * field (checked on curl version 7.78.0)
 */
#if LIBCURL_VERSION_NUM >= 0x072c00
#define GIT_CURL_HAVE_CURLSSLOPT_NO_REVOKE 1
#endif

/**
 * CURLOPT_PROXY_CAINFO was added in 7.52.0, released in August 2017.
 */
#if LIBCURL_VERSION_NUM >= 0x073400
#define GIT_CURL_HAVE_CURLOPT_PROXY_CAINFO 1
#endif

/**
 * CURLOPT_PROXY_{KEYPASSWD,SSLCERT,SSLKEY} was added in 7.52.0,
 * released in August 2017.
 */
#if LIBCURL_VERSION_NUM >= 0x073400
#define GIT_CURL_HAVE_CURLOPT_PROXY_KEYPASSWD 1
#endif

/**
 * CURL_SSLVERSION_TLSv1_3 was added in 7.53.0, released in February
 * 2017.
 */
#if LIBCURL_VERSION_NUM >= 0x073400
#define GIT_CURL_HAVE_CURL_SSLVERSION_TLSv1_3 1
#endif

/**
 * CURLSSLSET_{NO_BACKENDS,OK,TOO_LATE,UNKNOWN_BACKEND} were added in
 * 7.56.0, released in September 2017.
 */
#if LIBCURL_VERSION_NUM >= 0x073800
#define GIT_CURL_HAVE_CURLSSLSET_NO_BACKENDS
#endif

#endif
