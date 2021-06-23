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
 * Versions before curl 7.66.0 (September 2019) required manually setting the
 * transfer-encoding for a streaming POST; after that this is handled
 * automatically.
 */
#if LIBCURL_VERSION_NUM < 0x074200
#define GIT_CURL_NEED_TRANSFER_ENCODING_HEADER
#endif

/**
 * CURLOPT_PROTOCOLS_STR and CURLOPT_REDIR_PROTOCOLS_STR were added in 7.85.0,
 * released in August 2022.
 */
#if LIBCURL_VERSION_NUM >= 0x075500
#define GIT_CURL_HAVE_CURLOPT_PROTOCOLS_STR 1
#endif

/**
 * CURLSSLOPT_AUTO_CLIENT_CERT was added in 7.77.0, released in May
 * 2021.
 */
#if LIBCURL_VERSION_NUM >= 0x074d00
#define GIT_CURL_HAVE_CURLSSLOPT_AUTO_CLIENT_CERT
#endif

#endif
