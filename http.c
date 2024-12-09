#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "git-curl-compat.h"
#include "hex.h"
#include "http.h"
#include "config.h"
#include "pack.h"
#include "run-command.h"
#include "url.h"
#include "urlmatch.h"
#include "credential.h"
#include "version.h"
#include "pkt-line.h"
#include "gettext.h"
#include "trace.h"
#include "transport.h"
#include "packfile.h"
#include "string-list.h"
#include "object-file.h"
#include "object-store-ll.h"
#include "tempfile.h"

static struct trace_key trace_curl = TRACE_KEY_INIT(CURL);
static int trace_curl_data = 1;
static int trace_curl_redact = 1;
long int git_curl_ipresolve = CURL_IPRESOLVE_WHATEVER;
int active_requests;
int http_is_verbose;
ssize_t http_post_buffer = 16 * LARGE_PACKET_MAX;

static int min_curl_sessions = 1;
static int curl_session_count;
static int max_requests = -1;
static CURLM *curlm;
static CURL *curl_default;

#define PREV_BUF_SIZE 4096

char curl_errorstr[CURL_ERROR_SIZE];

static int curl_ssl_verify = -1;
static int curl_ssl_try;
static char *curl_http_version;
static char *ssl_cert;
static char *ssl_cert_type;
static char *ssl_cipherlist;
static char *ssl_version;
static struct {
	const char *name;
	long ssl_version;
} sslversions[] = {
	{ "sslv2", CURL_SSLVERSION_SSLv2 },
	{ "sslv3", CURL_SSLVERSION_SSLv3 },
	{ "tlsv1", CURL_SSLVERSION_TLSv1 },
	{ "tlsv1.0", CURL_SSLVERSION_TLSv1_0 },
	{ "tlsv1.1", CURL_SSLVERSION_TLSv1_1 },
	{ "tlsv1.2", CURL_SSLVERSION_TLSv1_2 },
	{ "tlsv1.3", CURL_SSLVERSION_TLSv1_3 },
};
static char *ssl_key;
static char *ssl_key_type;
static char *ssl_capath;
static char *curl_no_proxy;
static char *ssl_pinnedkey;
static char *ssl_cainfo;
static long curl_low_speed_limit = -1;
static long curl_low_speed_time = -1;
static int curl_ftp_no_epsv;
static char *curl_http_proxy;
static char *http_proxy_authmethod;

static char *http_proxy_ssl_cert;
static char *http_proxy_ssl_key;
static char *http_proxy_ssl_ca_info;
static struct credential proxy_cert_auth = CREDENTIAL_INIT;
static int proxy_ssl_cert_password_required;

static struct {
	const char *name;
	long curlauth_param;
} proxy_authmethods[] = {
	{ "basic", CURLAUTH_BASIC },
	{ "digest", CURLAUTH_DIGEST },
	{ "negotiate", CURLAUTH_GSSNEGOTIATE },
	{ "ntlm", CURLAUTH_NTLM },
	{ "anyauth", CURLAUTH_ANY },
	/*
	 * CURLAUTH_DIGEST_IE has no corresponding command-line option in
	 * curl(1) and is not included in CURLAUTH_ANY, so we leave it out
	 * here, too
	 */
};
#ifdef CURLGSSAPI_DELEGATION_FLAG
static char *curl_deleg;
static struct {
	const char *name;
	long curl_deleg_param;
} curl_deleg_levels[] = {
	{ "none", CURLGSSAPI_DELEGATION_NONE },
	{ "policy", CURLGSSAPI_DELEGATION_POLICY_FLAG },
	{ "always", CURLGSSAPI_DELEGATION_FLAG },
};
#endif

enum proactive_auth {
	PROACTIVE_AUTH_NONE = 0,
	PROACTIVE_AUTH_IF_CREDENTIALS,
	PROACTIVE_AUTH_AUTO,
	PROACTIVE_AUTH_BASIC,
};

static struct credential proxy_auth = CREDENTIAL_INIT;
static const char *curl_proxyuserpwd;
static char *curl_cookie_file;
static int curl_save_cookies;
struct credential http_auth = CREDENTIAL_INIT;
static enum proactive_auth http_proactive_auth;
static char *user_agent;
static int curl_empty_auth = -1;

enum http_follow_config http_follow_config = HTTP_FOLLOW_INITIAL;

static struct credential cert_auth = CREDENTIAL_INIT;
static int ssl_cert_password_required;
static unsigned long http_auth_methods = CURLAUTH_ANY;
static int http_auth_methods_restricted;
/* Modes for which empty_auth cannot actually help us. */
static unsigned long empty_auth_useless =
	CURLAUTH_BASIC
	| CURLAUTH_DIGEST_IE
	| CURLAUTH_DIGEST;

static struct curl_slist *pragma_header;
static struct string_list extra_http_headers = STRING_LIST_INIT_DUP;

static struct curl_slist *host_resolutions;

static struct active_request_slot *active_queue_head;

static char *cached_accept_language;

static char *http_ssl_backend;

static int http_schannel_check_revoke = 1;
/*
 * With the backend being set to `schannel`, setting sslCAinfo would override
 * the Certificate Store in cURL v7.60.0 and later, which is not what we want
 * by default.
 */
static int http_schannel_use_ssl_cainfo;

static int always_auth_proactively(void)
{
	return http_proactive_auth != PROACTIVE_AUTH_NONE &&
	       http_proactive_auth != PROACTIVE_AUTH_IF_CREDENTIALS;
}

size_t fread_buffer(char *ptr, size_t eltsize, size_t nmemb, void *buffer_)
{
	size_t size = eltsize * nmemb;
	struct buffer *buffer = buffer_;

	if (size > buffer->buf.len - buffer->posn)
		size = buffer->buf.len - buffer->posn;
	memcpy(ptr, buffer->buf.buf + buffer->posn, size);
	buffer->posn += size;

	return size / eltsize;
}

int seek_buffer(void *clientp, curl_off_t offset, int origin)
{
	struct buffer *buffer = clientp;

	if (origin != SEEK_SET)
		BUG("seek_buffer only handles SEEK_SET");
	if (offset < 0 || offset >= buffer->buf.len) {
		error("curl seek would be outside of buffer");
		return CURL_SEEKFUNC_FAIL;
	}

	buffer->posn = offset;
	return CURL_SEEKFUNC_OK;
}

size_t fwrite_buffer(char *ptr, size_t eltsize, size_t nmemb, void *buffer_)
{
	size_t size = eltsize * nmemb;
	struct strbuf *buffer = buffer_;

	strbuf_add(buffer, ptr, size);
	return nmemb;
}

/*
 * A folded header continuation line starts with any number of spaces or
 * horizontal tab characters (SP or HTAB) as per RFC 7230 section 3.2.
 * It is not a continuation line if the line starts with any other character.
 */
static inline int is_hdr_continuation(const char *ptr, const size_t size)
{
	return size && (*ptr == ' ' || *ptr == '\t');
}

static size_t fwrite_wwwauth(char *ptr, size_t eltsize, size_t nmemb, void *p UNUSED)
{
	size_t size = eltsize * nmemb;
	struct strvec *values = &http_auth.wwwauth_headers;
	struct strbuf buf = STRBUF_INIT;
	const char *val;
	size_t val_len;

	/*
	 * Header lines may not come NULL-terminated from libcurl so we must
	 * limit all scans to the maximum length of the header line, or leverage
	 * strbufs for all operations.
	 *
	 * In addition, it is possible that header values can be split over
	 * multiple lines as per RFC 7230. 'Line folding' has been deprecated
	 * but older servers may still emit them. A continuation header field
	 * value is identified as starting with a space or horizontal tab.
	 *
	 * The formal definition of a header field as given in RFC 7230 is:
	 *
	 * header-field   = field-name ":" OWS field-value OWS
	 *
	 * field-name     = token
	 * field-value    = *( field-content / obs-fold )
	 * field-content  = field-vchar [ 1*( SP / HTAB ) field-vchar ]
	 * field-vchar    = VCHAR / obs-text
	 *
	 * obs-fold       = CRLF 1*( SP / HTAB )
	 *                ; obsolete line folding
	 *                ; see Section 3.2.4
	 */

	/* Start of a new WWW-Authenticate header */
	if (skip_iprefix_mem(ptr, size, "www-authenticate:", &val, &val_len)) {
		strbuf_add(&buf, val, val_len);

		/*
		 * Strip the CRLF that should be present at the end of each
		 * field as well as any trailing or leading whitespace from the
		 * value.
		 */
		strbuf_trim(&buf);

		strvec_push(values, buf.buf);
		http_auth.header_is_last_match = 1;
		goto exit;
	}

	/*
	 * This line could be a continuation of the previously matched header
	 * field. If this is the case then we should append this value to the
	 * end of the previously consumed value.
	 */
	if (http_auth.header_is_last_match && is_hdr_continuation(ptr, size)) {
		/*
		 * Trim the CRLF and any leading or trailing from this line.
		 */
		strbuf_add(&buf, ptr, size);
		strbuf_trim(&buf);

		/*
		 * At this point we should always have at least one existing
		 * value, even if it is empty. Do not bother appending the new
		 * value if this continuation header is itself empty.
		 */
		if (!values->nr) {
			BUG("should have at least one existing header value");
		} else if (buf.len) {
			char *prev = xstrdup(values->v[values->nr - 1]);

			/* Join two non-empty values with a single space. */
			const char *const sp = *prev ? " " : "";

			strvec_pop(values);
			strvec_pushf(values, "%s%s%s", prev, sp, buf.buf);
			free(prev);
		}

		goto exit;
	}

	/* Not a continuation of a previously matched auth header line. */
	http_auth.header_is_last_match = 0;

	/*
	 * If this is a HTTP status line and not a header field, this signals
	 * a different HTTP response. libcurl writes all the output of all
	 * response headers of all responses, including redirects.
	 * We only care about the last HTTP request response's headers so clear
	 * the existing array.
	 */
	if (skip_iprefix_mem(ptr, size, "http/", &val, &val_len))
		strvec_clear(values);

exit:
	strbuf_release(&buf);
	return size;
}

size_t fwrite_null(char *ptr UNUSED, size_t eltsize UNUSED, size_t nmemb,
		   void *data UNUSED)
{
	return nmemb;
}

static struct curl_slist *object_request_headers(void)
{
	return curl_slist_append(http_copy_default_headers(), "Pragma:");
}

static void closedown_active_slot(struct active_request_slot *slot)
{
	active_requests--;
	slot->in_use = 0;
}

static void finish_active_slot(struct active_request_slot *slot)
{
	closedown_active_slot(slot);
	curl_easy_getinfo(slot->curl, CURLINFO_HTTP_CODE, &slot->http_code);

	if (slot->finished)
		(*slot->finished) = 1;

	/* Store slot results so they can be read after the slot is reused */
	if (slot->results) {
		slot->results->curl_result = slot->curl_result;
		slot->results->http_code = slot->http_code;
		curl_easy_getinfo(slot->curl, CURLINFO_HTTPAUTH_AVAIL,
				  &slot->results->auth_avail);

		curl_easy_getinfo(slot->curl, CURLINFO_HTTP_CONNECTCODE,
			&slot->results->http_connectcode);
	}

	/* Run callback if appropriate */
	if (slot->callback_func)
		slot->callback_func(slot->callback_data);
}

static void xmulti_remove_handle(struct active_request_slot *slot)
{
	curl_multi_remove_handle(curlm, slot->curl);
}

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
			if (slot) {
				xmulti_remove_handle(slot);
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

static int http_options(const char *var, const char *value,
			const struct config_context *ctx, void *data)
{
	if (!strcmp("http.version", var)) {
		return git_config_string(&curl_http_version, var, value);
	}
	if (!strcmp("http.sslverify", var)) {
		curl_ssl_verify = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp("http.sslcipherlist", var))
		return git_config_string(&ssl_cipherlist, var, value);
	if (!strcmp("http.sslversion", var))
		return git_config_string(&ssl_version, var, value);
	if (!strcmp("http.sslcert", var))
		return git_config_pathname(&ssl_cert, var, value);
	if (!strcmp("http.sslcerttype", var))
		return git_config_string(&ssl_cert_type, var, value);
	if (!strcmp("http.sslkey", var))
		return git_config_pathname(&ssl_key, var, value);
	if (!strcmp("http.sslkeytype", var))
		return git_config_string(&ssl_key_type, var, value);
	if (!strcmp("http.sslcapath", var))
		return git_config_pathname(&ssl_capath, var, value);
	if (!strcmp("http.sslcainfo", var))
		return git_config_pathname(&ssl_cainfo, var, value);
	if (!strcmp("http.sslcertpasswordprotected", var)) {
		ssl_cert_password_required = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp("http.ssltry", var)) {
		curl_ssl_try = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp("http.sslbackend", var)) {
		free(http_ssl_backend);
		http_ssl_backend = xstrdup_or_null(value);
		return 0;
	}

	if (!strcmp("http.schannelcheckrevoke", var)) {
		http_schannel_check_revoke = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp("http.schannelusesslcainfo", var)) {
		http_schannel_use_ssl_cainfo = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp("http.minsessions", var)) {
		min_curl_sessions = git_config_int(var, value, ctx->kvi);
		if (min_curl_sessions > 1)
			min_curl_sessions = 1;
		return 0;
	}
	if (!strcmp("http.maxrequests", var)) {
		max_requests = git_config_int(var, value, ctx->kvi);
		return 0;
	}
	if (!strcmp("http.lowspeedlimit", var)) {
		curl_low_speed_limit = (long)git_config_int(var, value, ctx->kvi);
		return 0;
	}
	if (!strcmp("http.lowspeedtime", var)) {
		curl_low_speed_time = (long)git_config_int(var, value, ctx->kvi);
		return 0;
	}

	if (!strcmp("http.noepsv", var)) {
		curl_ftp_no_epsv = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp("http.proxy", var))
		return git_config_string(&curl_http_proxy, var, value);

	if (!strcmp("http.proxyauthmethod", var))
		return git_config_string(&http_proxy_authmethod, var, value);

	if (!strcmp("http.proxysslcert", var))
		return git_config_string(&http_proxy_ssl_cert, var, value);

	if (!strcmp("http.proxysslkey", var))
		return git_config_string(&http_proxy_ssl_key, var, value);

	if (!strcmp("http.proxysslcainfo", var))
		return git_config_string(&http_proxy_ssl_ca_info, var, value);

	if (!strcmp("http.proxysslcertpasswordprotected", var)) {
		proxy_ssl_cert_password_required = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp("http.cookiefile", var))
		return git_config_pathname(&curl_cookie_file, var, value);
	if (!strcmp("http.savecookies", var)) {
		curl_save_cookies = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp("http.postbuffer", var)) {
		http_post_buffer = git_config_ssize_t(var, value, ctx->kvi);
		if (http_post_buffer < 0)
			warning(_("negative value for http.postBuffer; defaulting to %d"), LARGE_PACKET_MAX);
		if (http_post_buffer < LARGE_PACKET_MAX)
			http_post_buffer = LARGE_PACKET_MAX;
		return 0;
	}

	if (!strcmp("http.useragent", var))
		return git_config_string(&user_agent, var, value);

	if (!strcmp("http.emptyauth", var)) {
		if (value && !strcmp("auto", value))
			curl_empty_auth = -1;
		else
			curl_empty_auth = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp("http.delegation", var)) {
#ifdef CURLGSSAPI_DELEGATION_FLAG
		return git_config_string(&curl_deleg, var, value);
#else
		warning(_("Delegation control is not supported with cURL < 7.22.0"));
		return 0;
#endif
	}

	if (!strcmp("http.pinnedpubkey", var)) {
		return git_config_pathname(&ssl_pinnedkey, var, value);
	}

	if (!strcmp("http.extraheader", var)) {
		if (!value) {
			return config_error_nonbool(var);
		} else if (!*value) {
			string_list_clear(&extra_http_headers, 0);
		} else {
			string_list_append(&extra_http_headers, value);
		}
		return 0;
	}

	if (!strcmp("http.curloptresolve", var)) {
		if (!value) {
			return config_error_nonbool(var);
		} else if (!*value) {
			curl_slist_free_all(host_resolutions);
			host_resolutions = NULL;
		} else {
			host_resolutions = curl_slist_append(host_resolutions, value);
		}
		return 0;
	}

	if (!strcmp("http.followredirects", var)) {
		if (value && !strcmp(value, "initial"))
			http_follow_config = HTTP_FOLLOW_INITIAL;
		else if (git_config_bool(var, value))
			http_follow_config = HTTP_FOLLOW_ALWAYS;
		else
			http_follow_config = HTTP_FOLLOW_NONE;
		return 0;
	}

	if (!strcmp("http.proactiveauth", var)) {
		if (!value)
			return config_error_nonbool(var);
		if (!strcmp(value, "auto"))
			http_proactive_auth = PROACTIVE_AUTH_AUTO;
		else if (!strcmp(value, "basic"))
			http_proactive_auth = PROACTIVE_AUTH_BASIC;
		else if (!strcmp(value, "none"))
			http_proactive_auth = PROACTIVE_AUTH_NONE;
		else
			warning(_("Unknown value for http.proactiveauth"));
		return 0;
	}

	/* Fall back on the default ones */
	return git_default_config(var, value, ctx, data);
}

static int curl_empty_auth_enabled(void)
{
	if (curl_empty_auth >= 0)
		return curl_empty_auth;

	/*
	 * In the automatic case, kick in the empty-auth
	 * hack as long as we would potentially try some
	 * method more exotic than "Basic" or "Digest".
	 *
	 * But only do this when this is our second or
	 * subsequent request, as by then we know what
	 * methods are available.
	 */
	if (http_auth_methods_restricted &&
	    (http_auth_methods & ~empty_auth_useless))
		return 1;
	return 0;
}

struct curl_slist *http_append_auth_header(const struct credential *c,
					   struct curl_slist *headers)
{
	if (c->authtype && c->credential) {
		struct strbuf auth = STRBUF_INIT;
		strbuf_addf(&auth, "Authorization: %s %s",
			    c->authtype, c->credential);
		headers = curl_slist_append(headers, auth.buf);
		strbuf_release(&auth);
	}
	return headers;
}

static void init_curl_http_auth(CURL *result)
{
	if ((!http_auth.username || !*http_auth.username) &&
	    (!http_auth.credential || !*http_auth.credential)) {
		int empty_auth = curl_empty_auth_enabled();
		if ((empty_auth != -1 && !always_auth_proactively()) || empty_auth == 1) {
			curl_easy_setopt(result, CURLOPT_USERPWD, ":");
			return;
		} else if (!always_auth_proactively()) {
			return;
		} else if (http_proactive_auth == PROACTIVE_AUTH_BASIC) {
			strvec_push(&http_auth.wwwauth_headers, "Basic");
		}
	}

	credential_fill(&http_auth, 1);

	if (http_auth.password) {
		if (always_auth_proactively()) {
			/*
			 * We got a credential without an authtype and we don't
			 * know what's available.  Since our only two options at
			 * the moment are auto (which defaults to basic) and
			 * basic, use basic for now.
			 */
			curl_easy_setopt(result, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
		}
		curl_easy_setopt(result, CURLOPT_USERNAME, http_auth.username);
		curl_easy_setopt(result, CURLOPT_PASSWORD, http_auth.password);
	}
}

/* *var must be free-able */
static void var_override(char **var, char *value)
{
	if (value) {
		free(*var);
		*var = xstrdup(value);
	}
}

static void set_proxyauth_name_password(CURL *result)
{
	if (proxy_auth.password) {
		curl_easy_setopt(result, CURLOPT_PROXYUSERNAME,
			proxy_auth.username);
		curl_easy_setopt(result, CURLOPT_PROXYPASSWORD,
			proxy_auth.password);
	} else if (proxy_auth.authtype && proxy_auth.credential) {
		curl_easy_setopt(result, CURLOPT_PROXYHEADER,
				 http_append_auth_header(&proxy_auth, NULL));
	}
}

static void init_curl_proxy_auth(CURL *result)
{
	if (proxy_auth.username) {
		if (!proxy_auth.password && !proxy_auth.credential)
			credential_fill(&proxy_auth, 1);
		set_proxyauth_name_password(result);
	}

	var_override(&http_proxy_authmethod, getenv("GIT_HTTP_PROXY_AUTHMETHOD"));

	if (http_proxy_authmethod) {
		int i;
		for (i = 0; i < ARRAY_SIZE(proxy_authmethods); i++) {
			if (!strcmp(http_proxy_authmethod, proxy_authmethods[i].name)) {
				curl_easy_setopt(result, CURLOPT_PROXYAUTH,
						proxy_authmethods[i].curlauth_param);
				break;
			}
		}
		if (i == ARRAY_SIZE(proxy_authmethods)) {
			warning("unsupported proxy authentication method %s: using anyauth",
					http_proxy_authmethod);
			curl_easy_setopt(result, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
		}
	}
	else
		curl_easy_setopt(result, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
}

static int has_cert_password(void)
{
	if (ssl_cert == NULL || ssl_cert_password_required != 1)
		return 0;
	if (!cert_auth.password) {
		cert_auth.protocol = xstrdup("cert");
		cert_auth.host = xstrdup("");
		cert_auth.username = xstrdup("");
		cert_auth.path = xstrdup(ssl_cert);
		credential_fill(&cert_auth, 0);
	}
	return 1;
}

static int has_proxy_cert_password(void)
{
	if (http_proxy_ssl_cert == NULL || proxy_ssl_cert_password_required != 1)
		return 0;
	if (!proxy_cert_auth.password) {
		proxy_cert_auth.protocol = xstrdup("cert");
		proxy_cert_auth.host = xstrdup("");
		proxy_cert_auth.username = xstrdup("");
		proxy_cert_auth.path = xstrdup(http_proxy_ssl_cert);
		credential_fill(&proxy_cert_auth, 0);
	}
	return 1;
}

static void set_curl_keepalive(CURL *c)
{
	curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1);
}

/* Return 1 if redactions have been made, 0 otherwise. */
static int redact_sensitive_header(struct strbuf *header, size_t offset)
{
	int ret = 0;
	const char *sensitive_header;

	if (trace_curl_redact &&
	    (skip_iprefix(header->buf + offset, "Authorization:", &sensitive_header) ||
	     skip_iprefix(header->buf + offset, "Proxy-Authorization:", &sensitive_header))) {
		/* The first token is the type, which is OK to log */
		while (isspace(*sensitive_header))
			sensitive_header++;
		while (*sensitive_header && !isspace(*sensitive_header))
			sensitive_header++;
		/* Everything else is opaque and possibly sensitive */
		strbuf_setlen(header,  sensitive_header - header->buf);
		strbuf_addstr(header, " <redacted>");
		ret = 1;
	} else if (trace_curl_redact &&
		   skip_iprefix(header->buf + offset, "Cookie:", &sensitive_header)) {
		struct strbuf redacted_header = STRBUF_INIT;
		const char *cookie;

		while (isspace(*sensitive_header))
			sensitive_header++;

		cookie = sensitive_header;

		while (cookie) {
			char *equals;
			char *semicolon = strstr(cookie, "; ");
			if (semicolon)
				*semicolon = 0;
			equals = strchrnul(cookie, '=');
			if (!equals) {
				/* invalid cookie, just append and continue */
				strbuf_addstr(&redacted_header, cookie);
				continue;
			}
			strbuf_add(&redacted_header, cookie, equals - cookie);
			strbuf_addstr(&redacted_header, "=<redacted>");
			if (semicolon) {
				/*
				 * There are more cookies. (Or, for some
				 * reason, the input string ends in "; ".)
				 */
				strbuf_addstr(&redacted_header, "; ");
				cookie = semicolon + strlen("; ");
			} else {
				cookie = NULL;
			}
		}

		strbuf_setlen(header, sensitive_header - header->buf);
		strbuf_addbuf(header, &redacted_header);
		strbuf_release(&redacted_header);
		ret = 1;
	}
	return ret;
}

static int match_curl_h2_trace(const char *line, const char **out)
{
	const char *p;

	/*
	 * curl prior to 8.1.0 gives us:
	 *
	 *     h2h3 [<header-name>: <header-val>]
	 *
	 * Starting in 8.1.0, the first token became just "h2".
	 */
	if (skip_iprefix(line, "h2h3 [", out) ||
	    skip_iprefix(line, "h2 [", out))
		return 1;

	/*
	 * curl 8.3.0 uses:
	 *   [HTTP/2] [<stream-id>] [<header-name>: <header-val>]
	 * where <stream-id> is numeric.
	 */
	if (skip_iprefix(line, "[HTTP/2] [", &p)) {
		while (isdigit(*p))
			p++;
		if (skip_prefix(p, "] [", out))
			return 1;
	}

	return 0;
}

/* Redact headers in info */
static void redact_sensitive_info_header(struct strbuf *header)
{
	const char *sensitive_header;

	if (trace_curl_redact &&
	    match_curl_h2_trace(header->buf, &sensitive_header)) {
		if (redact_sensitive_header(header, sensitive_header - header->buf)) {
			/* redaction ate our closing bracket */
			strbuf_addch(header, ']');
		}
	}
}

static void curl_dump_header(const char *text, unsigned char *ptr, size_t size, int hide_sensitive_header)
{
	struct strbuf out = STRBUF_INIT;
	struct strbuf **headers, **header;

	strbuf_addf(&out, "%s, %10.10ld bytes (0x%8.8lx)\n",
		text, (long)size, (long)size);
	trace_strbuf(&trace_curl, &out);
	strbuf_reset(&out);
	strbuf_add(&out, ptr, size);
	headers = strbuf_split_max(&out, '\n', 0);

	for (header = headers; *header; header++) {
		if (hide_sensitive_header)
			redact_sensitive_header(*header, 0);
		strbuf_insertstr((*header), 0, text);
		strbuf_insertstr((*header), strlen(text), ": ");
		strbuf_rtrim((*header));
		strbuf_addch((*header), '\n');
		trace_strbuf(&trace_curl, (*header));
	}
	strbuf_list_free(headers);
	strbuf_release(&out);
}

static void curl_dump_data(const char *text, unsigned char *ptr, size_t size)
{
	size_t i;
	struct strbuf out = STRBUF_INIT;
	unsigned int width = 60;

	strbuf_addf(&out, "%s, %10.10ld bytes (0x%8.8lx)\n",
		text, (long)size, (long)size);
	trace_strbuf(&trace_curl, &out);

	for (i = 0; i < size; i += width) {
		size_t w;

		strbuf_reset(&out);
		strbuf_addf(&out, "%s: ", text);
		for (w = 0; (w < width) && (i + w < size); w++) {
			unsigned char ch = ptr[i + w];

			strbuf_addch(&out,
				       (ch >= 0x20) && (ch < 0x80)
				       ? ch : '.');
		}
		strbuf_addch(&out, '\n');
		trace_strbuf(&trace_curl, &out);
	}
	strbuf_release(&out);
}

static void curl_dump_info(char *data, size_t size)
{
	struct strbuf buf = STRBUF_INIT;

	strbuf_add(&buf, data, size);

	redact_sensitive_info_header(&buf);
	trace_printf_key(&trace_curl, "== Info: %s", buf.buf);

	strbuf_release(&buf);
}

static int curl_trace(CURL *handle UNUSED, curl_infotype type,
		      char *data, size_t size,
		      void *userp UNUSED)
{
	const char *text;
	enum { NO_FILTER = 0, DO_FILTER = 1 };

	switch (type) {
	case CURLINFO_TEXT:
		curl_dump_info(data, size);
		break;
	case CURLINFO_HEADER_OUT:
		text = "=> Send header";
		curl_dump_header(text, (unsigned char *)data, size, DO_FILTER);
		break;
	case CURLINFO_DATA_OUT:
		if (trace_curl_data) {
			text = "=> Send data";
			curl_dump_data(text, (unsigned char *)data, size);
		}
		break;
	case CURLINFO_SSL_DATA_OUT:
		if (trace_curl_data) {
			text = "=> Send SSL data";
			curl_dump_data(text, (unsigned char *)data, size);
		}
		break;
	case CURLINFO_HEADER_IN:
		text = "<= Recv header";
		curl_dump_header(text, (unsigned char *)data, size, NO_FILTER);
		break;
	case CURLINFO_DATA_IN:
		if (trace_curl_data) {
			text = "<= Recv data";
			curl_dump_data(text, (unsigned char *)data, size);
		}
		break;
	case CURLINFO_SSL_DATA_IN:
		if (trace_curl_data) {
			text = "<= Recv SSL data";
			curl_dump_data(text, (unsigned char *)data, size);
		}
		break;

	default:		/* we ignore unknown types by default */
		return 0;
	}
	return 0;
}

void http_trace_curl_no_data(void)
{
	trace_override_envvar(&trace_curl, "1");
	trace_curl_data = 0;
}

void setup_curl_trace(CURL *handle)
{
	if (!trace_want(&trace_curl))
		return;
	curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(handle, CURLOPT_DEBUGFUNCTION, curl_trace);
	curl_easy_setopt(handle, CURLOPT_DEBUGDATA, NULL);
}

static void proto_list_append(struct strbuf *list, const char *proto)
{
	if (!list)
		return;
	if (list->len)
		strbuf_addch(list, ',');
	strbuf_addstr(list, proto);
}

static long get_curl_allowed_protocols(int from_user, struct strbuf *list)
{
	long bits = 0;

	if (is_transport_allowed("http", from_user)) {
		bits |= CURLPROTO_HTTP;
		proto_list_append(list, "http");
	}
	if (is_transport_allowed("https", from_user)) {
		bits |= CURLPROTO_HTTPS;
		proto_list_append(list, "https");
	}
	if (is_transport_allowed("ftp", from_user)) {
		bits |= CURLPROTO_FTP;
		proto_list_append(list, "ftp");
	}
	if (is_transport_allowed("ftps", from_user)) {
		bits |= CURLPROTO_FTPS;
		proto_list_append(list, "ftps");
	}

	return bits;
}

static int get_curl_http_version_opt(const char *version_string, long *opt)
{
	int i;
	static struct {
		const char *name;
		long opt_token;
	} choice[] = {
		{ "HTTP/1.1", CURL_HTTP_VERSION_1_1 },
		{ "HTTP/2", CURL_HTTP_VERSION_2 }
	};

	for (i = 0; i < ARRAY_SIZE(choice); i++) {
		if (!strcmp(version_string, choice[i].name)) {
			*opt = choice[i].opt_token;
			return 0;
		}
	}

	warning("unknown value given to http.version: '%s'", version_string);
	return -1; /* not found */
}

static CURL *get_curl_handle(void)
{
	CURL *result = curl_easy_init();

	if (!result)
		die("curl_easy_init failed");

	if (!curl_ssl_verify) {
		curl_easy_setopt(result, CURLOPT_SSL_VERIFYPEER, 0);
		curl_easy_setopt(result, CURLOPT_SSL_VERIFYHOST, 0);
	} else {
		/* Verify authenticity of the peer's certificate */
		curl_easy_setopt(result, CURLOPT_SSL_VERIFYPEER, 1);
		/* The name in the cert must match whom we tried to connect */
		curl_easy_setopt(result, CURLOPT_SSL_VERIFYHOST, 2);
	}

    if (curl_http_version) {
		long opt;
		if (!get_curl_http_version_opt(curl_http_version, &opt)) {
			/* Set request use http version */
			curl_easy_setopt(result, CURLOPT_HTTP_VERSION, opt);
		}
    }

	curl_easy_setopt(result, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
	curl_easy_setopt(result, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

#ifdef CURLGSSAPI_DELEGATION_FLAG
	if (curl_deleg) {
		int i;
		for (i = 0; i < ARRAY_SIZE(curl_deleg_levels); i++) {
			if (!strcmp(curl_deleg, curl_deleg_levels[i].name)) {
				curl_easy_setopt(result, CURLOPT_GSSAPI_DELEGATION,
						curl_deleg_levels[i].curl_deleg_param);
				break;
			}
		}
		if (i == ARRAY_SIZE(curl_deleg_levels))
			warning("Unknown delegation method '%s': using default",
				curl_deleg);
	}
#endif

	if (http_ssl_backend && !strcmp("schannel", http_ssl_backend) &&
	    !http_schannel_check_revoke) {
		curl_easy_setopt(result, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
	}

	if (http_proactive_auth != PROACTIVE_AUTH_NONE)
		init_curl_http_auth(result);

	if (getenv("GIT_SSL_VERSION"))
		ssl_version = getenv("GIT_SSL_VERSION");
	if (ssl_version && *ssl_version) {
		int i;
		for (i = 0; i < ARRAY_SIZE(sslversions); i++) {
			if (!strcmp(ssl_version, sslversions[i].name)) {
				curl_easy_setopt(result, CURLOPT_SSLVERSION,
						 sslversions[i].ssl_version);
				break;
			}
		}
		if (i == ARRAY_SIZE(sslversions))
			warning("unsupported ssl version %s: using default",
				ssl_version);
	}

	if (getenv("GIT_SSL_CIPHER_LIST"))
		ssl_cipherlist = getenv("GIT_SSL_CIPHER_LIST");
	if (ssl_cipherlist != NULL && *ssl_cipherlist)
		curl_easy_setopt(result, CURLOPT_SSL_CIPHER_LIST,
				ssl_cipherlist);

	if (ssl_cert)
		curl_easy_setopt(result, CURLOPT_SSLCERT, ssl_cert);
	if (ssl_cert_type)
		curl_easy_setopt(result, CURLOPT_SSLCERTTYPE, ssl_cert_type);
	if (has_cert_password())
		curl_easy_setopt(result, CURLOPT_KEYPASSWD, cert_auth.password);
	if (ssl_key)
		curl_easy_setopt(result, CURLOPT_SSLKEY, ssl_key);
	if (ssl_key_type)
		curl_easy_setopt(result, CURLOPT_SSLKEYTYPE, ssl_key_type);
	if (ssl_capath)
		curl_easy_setopt(result, CURLOPT_CAPATH, ssl_capath);
	if (ssl_pinnedkey)
		curl_easy_setopt(result, CURLOPT_PINNEDPUBLICKEY, ssl_pinnedkey);
	if (http_ssl_backend && !strcmp("schannel", http_ssl_backend) &&
	    !http_schannel_use_ssl_cainfo) {
		curl_easy_setopt(result, CURLOPT_CAINFO, NULL);
		curl_easy_setopt(result, CURLOPT_PROXY_CAINFO, NULL);
	} else if (ssl_cainfo != NULL || http_proxy_ssl_ca_info != NULL) {
		if (ssl_cainfo)
			curl_easy_setopt(result, CURLOPT_CAINFO, ssl_cainfo);
		if (http_proxy_ssl_ca_info)
			curl_easy_setopt(result, CURLOPT_PROXY_CAINFO, http_proxy_ssl_ca_info);
	}

	if (curl_low_speed_limit > 0 && curl_low_speed_time > 0) {
		curl_easy_setopt(result, CURLOPT_LOW_SPEED_LIMIT,
				 curl_low_speed_limit);
		curl_easy_setopt(result, CURLOPT_LOW_SPEED_TIME,
				 curl_low_speed_time);
	}

	curl_easy_setopt(result, CURLOPT_MAXREDIRS, 20);
	curl_easy_setopt(result, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

#ifdef GIT_CURL_HAVE_CURLOPT_PROTOCOLS_STR
	{
		struct strbuf buf = STRBUF_INIT;

		get_curl_allowed_protocols(0, &buf);
		curl_easy_setopt(result, CURLOPT_REDIR_PROTOCOLS_STR, buf.buf);
		strbuf_reset(&buf);

		get_curl_allowed_protocols(-1, &buf);
		curl_easy_setopt(result, CURLOPT_PROTOCOLS_STR, buf.buf);
		strbuf_release(&buf);
	}
#else
	curl_easy_setopt(result, CURLOPT_REDIR_PROTOCOLS,
			 get_curl_allowed_protocols(0, NULL));
	curl_easy_setopt(result, CURLOPT_PROTOCOLS,
			 get_curl_allowed_protocols(-1, NULL));
#endif

	if (getenv("GIT_CURL_VERBOSE"))
		http_trace_curl_no_data();
	setup_curl_trace(result);
	if (getenv("GIT_TRACE_CURL_NO_DATA"))
		trace_curl_data = 0;
	if (!git_env_bool("GIT_TRACE_REDACT", 1))
		trace_curl_redact = 0;

	curl_easy_setopt(result, CURLOPT_USERAGENT,
		user_agent ? user_agent : git_user_agent());

	if (curl_ftp_no_epsv)
		curl_easy_setopt(result, CURLOPT_FTP_USE_EPSV, 0);

	if (curl_ssl_try)
		curl_easy_setopt(result, CURLOPT_USE_SSL, CURLUSESSL_TRY);

	/*
	 * CURL also examines these variables as a fallback; but we need to query
	 * them here in order to decide whether to prompt for missing password (cf.
	 * init_curl_proxy_auth()).
	 *
	 * Unlike many other common environment variables, these are historically
	 * lowercase only. It appears that CURL did not know this and implemented
	 * only uppercase variants, which was later corrected to take both - with
	 * the exception of http_proxy, which is lowercase only also in CURL. As
	 * the lowercase versions are the historical quasi-standard, they take
	 * precedence here, as in CURL.
	 */
	if (!curl_http_proxy) {
		if (http_auth.protocol && !strcmp(http_auth.protocol, "https")) {
			var_override(&curl_http_proxy, getenv("HTTPS_PROXY"));
			var_override(&curl_http_proxy, getenv("https_proxy"));
		} else {
			var_override(&curl_http_proxy, getenv("http_proxy"));
		}
		if (!curl_http_proxy) {
			var_override(&curl_http_proxy, getenv("ALL_PROXY"));
			var_override(&curl_http_proxy, getenv("all_proxy"));
		}
	}

	if (curl_http_proxy && curl_http_proxy[0] == '\0') {
		/*
		 * Handle case with the empty http.proxy value here to keep
		 * common code clean.
		 * NB: empty option disables proxying at all.
		 */
		curl_easy_setopt(result, CURLOPT_PROXY, "");
	} else if (curl_http_proxy) {
		struct strbuf proxy = STRBUF_INIT;

		if (starts_with(curl_http_proxy, "socks5h"))
			curl_easy_setopt(result,
				CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
		else if (starts_with(curl_http_proxy, "socks5"))
			curl_easy_setopt(result,
				CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);
		else if (starts_with(curl_http_proxy, "socks4a"))
			curl_easy_setopt(result,
				CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4A);
		else if (starts_with(curl_http_proxy, "socks"))
			curl_easy_setopt(result,
				CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
		else if (starts_with(curl_http_proxy, "https")) {
			curl_easy_setopt(result, CURLOPT_PROXYTYPE, CURLPROXY_HTTPS);

			if (http_proxy_ssl_cert)
				curl_easy_setopt(result, CURLOPT_PROXY_SSLCERT, http_proxy_ssl_cert);

			if (http_proxy_ssl_key)
				curl_easy_setopt(result, CURLOPT_PROXY_SSLKEY, http_proxy_ssl_key);

			if (has_proxy_cert_password())
				curl_easy_setopt(result, CURLOPT_PROXY_KEYPASSWD, proxy_cert_auth.password);
		}
		if (strstr(curl_http_proxy, "://"))
			credential_from_url(&proxy_auth, curl_http_proxy);
		else {
			struct strbuf url = STRBUF_INIT;
			strbuf_addf(&url, "http://%s", curl_http_proxy);
			credential_from_url(&proxy_auth, url.buf);
			strbuf_release(&url);
		}

		if (!proxy_auth.host)
			die("Invalid proxy URL '%s'", curl_http_proxy);

		strbuf_addstr(&proxy, proxy_auth.host);
		if (proxy_auth.path) {
			curl_version_info_data *ver = curl_version_info(CURLVERSION_NOW);

			if (ver->version_num < 0x075400)
				die("libcurl 7.84 or later is required to support paths in proxy URLs");

			if (!starts_with(proxy_auth.protocol, "socks"))
				die("Invalid proxy URL '%s': only SOCKS proxies support paths",
				    curl_http_proxy);

			if (strcasecmp(proxy_auth.host, "localhost"))
				die("Invalid proxy URL '%s': host must be localhost if a path is present",
				    curl_http_proxy);

			strbuf_addch(&proxy, '/');
			strbuf_add_percentencode(&proxy, proxy_auth.path, 0);
		}
		curl_easy_setopt(result, CURLOPT_PROXY, proxy.buf);
		strbuf_release(&proxy);

		var_override(&curl_no_proxy, getenv("NO_PROXY"));
		var_override(&curl_no_proxy, getenv("no_proxy"));
		curl_easy_setopt(result, CURLOPT_NOPROXY, curl_no_proxy);
	}
	init_curl_proxy_auth(result);

	set_curl_keepalive(result);

	return result;
}

static void set_from_env(char **var, const char *envname)
{
	const char *val = getenv(envname);
	if (val) {
		FREE_AND_NULL(*var);
		*var = xstrdup(val);
	}
}

void http_init(struct remote *remote, const char *url, int proactive_auth)
{
	char *low_speed_limit;
	char *low_speed_time;
	char *normalized_url;
	struct urlmatch_config config = URLMATCH_CONFIG_INIT;

	config.section = "http";
	config.key = NULL;
	config.collect_fn = http_options;
	config.cascade_fn = git_default_config;
	config.cb = NULL;

	http_is_verbose = 0;
	normalized_url = url_normalize(url, &config.url);

	git_config(urlmatch_config_entry, &config);
	free(normalized_url);
	string_list_clear(&config.vars, 1);

	if (http_ssl_backend) {
		const curl_ssl_backend **backends;
		struct strbuf buf = STRBUF_INIT;
		int i;

		switch (curl_global_sslset(-1, http_ssl_backend, &backends)) {
		case CURLSSLSET_UNKNOWN_BACKEND:
			strbuf_addf(&buf, _("Unsupported SSL backend '%s'. "
					    "Supported SSL backends:"),
					    http_ssl_backend);
			for (i = 0; backends[i]; i++)
				strbuf_addf(&buf, "\n\t%s", backends[i]->name);
			die("%s", buf.buf);
		case CURLSSLSET_NO_BACKENDS:
			die(_("Could not set SSL backend to '%s': "
			      "cURL was built without SSL backends"),
			    http_ssl_backend);
		case CURLSSLSET_TOO_LATE:
			die(_("Could not set SSL backend to '%s': already set"),
			    http_ssl_backend);
		case CURLSSLSET_OK:
			break; /* Okay! */
		}
	}

	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
		die("curl_global_init failed");

	if (proactive_auth && http_proactive_auth == PROACTIVE_AUTH_NONE)
		http_proactive_auth = PROACTIVE_AUTH_IF_CREDENTIALS;

	if (remote && remote->http_proxy)
		curl_http_proxy = xstrdup(remote->http_proxy);

	if (remote)
		var_override(&http_proxy_authmethod, remote->http_proxy_authmethod);

	pragma_header = curl_slist_append(http_copy_default_headers(),
		"Pragma: no-cache");

	{
		char *http_max_requests = getenv("GIT_HTTP_MAX_REQUESTS");
		if (http_max_requests)
			max_requests = atoi(http_max_requests);
	}

	curlm = curl_multi_init();
	if (!curlm)
		die("curl_multi_init failed");

	if (getenv("GIT_SSL_NO_VERIFY"))
		curl_ssl_verify = 0;

	set_from_env(&ssl_cert, "GIT_SSL_CERT");
	set_from_env(&ssl_cert_type, "GIT_SSL_CERT_TYPE");
	set_from_env(&ssl_key, "GIT_SSL_KEY");
	set_from_env(&ssl_key_type, "GIT_SSL_KEY_TYPE");
	set_from_env(&ssl_capath, "GIT_SSL_CAPATH");
	set_from_env(&ssl_cainfo, "GIT_SSL_CAINFO");

	set_from_env(&user_agent, "GIT_HTTP_USER_AGENT");

	low_speed_limit = getenv("GIT_HTTP_LOW_SPEED_LIMIT");
	if (low_speed_limit)
		curl_low_speed_limit = strtol(low_speed_limit, NULL, 10);
	low_speed_time = getenv("GIT_HTTP_LOW_SPEED_TIME");
	if (low_speed_time)
		curl_low_speed_time = strtol(low_speed_time, NULL, 10);

	if (curl_ssl_verify == -1)
		curl_ssl_verify = 1;

	curl_session_count = 0;
	if (max_requests < 1)
		max_requests = DEFAULT_MAX_REQUESTS;

	set_from_env(&http_proxy_ssl_cert, "GIT_PROXY_SSL_CERT");
	set_from_env(&http_proxy_ssl_key, "GIT_PROXY_SSL_KEY");
	set_from_env(&http_proxy_ssl_ca_info, "GIT_PROXY_SSL_CAINFO");

	if (getenv("GIT_PROXY_SSL_CERT_PASSWORD_PROTECTED"))
		proxy_ssl_cert_password_required = 1;

	if (getenv("GIT_CURL_FTP_NO_EPSV"))
		curl_ftp_no_epsv = 1;

	if (url) {
		credential_from_url(&http_auth, url);
		if (!ssl_cert_password_required &&
		    getenv("GIT_SSL_CERT_PASSWORD_PROTECTED") &&
		    starts_with(url, "https://"))
			ssl_cert_password_required = 1;
	}

	curl_default = get_curl_handle();
}

void http_cleanup(void)
{
	struct active_request_slot *slot = active_queue_head;

	while (slot != NULL) {
		struct active_request_slot *next = slot->next;
		if (slot->curl) {
			xmulti_remove_handle(slot);
			curl_easy_cleanup(slot->curl);
		}
		free(slot);
		slot = next;
	}
	active_queue_head = NULL;

	curl_easy_cleanup(curl_default);

	curl_multi_cleanup(curlm);
	curl_global_cleanup();

	string_list_clear(&extra_http_headers, 0);

	curl_slist_free_all(pragma_header);
	pragma_header = NULL;

	curl_slist_free_all(host_resolutions);
	host_resolutions = NULL;

	if (curl_http_proxy) {
		free((void *)curl_http_proxy);
		curl_http_proxy = NULL;
	}

	if (proxy_auth.password) {
		memset(proxy_auth.password, 0, strlen(proxy_auth.password));
		FREE_AND_NULL(proxy_auth.password);
	}

	free((void *)curl_proxyuserpwd);
	curl_proxyuserpwd = NULL;

	free((void *)http_proxy_authmethod);
	http_proxy_authmethod = NULL;

	if (cert_auth.password) {
		memset(cert_auth.password, 0, strlen(cert_auth.password));
		FREE_AND_NULL(cert_auth.password);
	}
	ssl_cert_password_required = 0;

	if (proxy_cert_auth.password) {
		memset(proxy_cert_auth.password, 0, strlen(proxy_cert_auth.password));
		FREE_AND_NULL(proxy_cert_auth.password);
	}
	proxy_ssl_cert_password_required = 0;

	FREE_AND_NULL(cached_accept_language);
}

struct active_request_slot *get_active_slot(void)
{
	struct active_request_slot *slot = active_queue_head;
	struct active_request_slot *newslot;

	int num_transfers;

	/* Wait for a slot to open up if the queue is full */
	while (active_requests >= max_requests) {
		curl_multi_perform(curlm, &num_transfers);
		if (num_transfers < active_requests)
			process_curl_messages();
	}

	while (slot != NULL && slot->in_use)
		slot = slot->next;

	if (!slot) {
		newslot = xmalloc(sizeof(*newslot));
		newslot->curl = NULL;
		newslot->in_use = 0;
		newslot->next = NULL;

		slot = active_queue_head;
		if (!slot) {
			active_queue_head = newslot;
		} else {
			while (slot->next != NULL)
				slot = slot->next;
			slot->next = newslot;
		}
		slot = newslot;
	}

	if (!slot->curl) {
		slot->curl = curl_easy_duphandle(curl_default);
		curl_session_count++;
	}

	active_requests++;
	slot->in_use = 1;
	slot->results = NULL;
	slot->finished = NULL;
	slot->callback_data = NULL;
	slot->callback_func = NULL;

	if (curl_cookie_file && !strcmp(curl_cookie_file, "-")) {
		warning(_("refusing to read cookies from http.cookiefile '-'"));
		FREE_AND_NULL(curl_cookie_file);
	}
	curl_easy_setopt(slot->curl, CURLOPT_COOKIEFILE, curl_cookie_file);
	if (curl_save_cookies && (!curl_cookie_file || !curl_cookie_file[0])) {
		curl_save_cookies = 0;
		warning(_("ignoring http.savecookies for empty http.cookiefile"));
	}
	if (curl_save_cookies)
		curl_easy_setopt(slot->curl, CURLOPT_COOKIEJAR, curl_cookie_file);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, pragma_header);
	curl_easy_setopt(slot->curl, CURLOPT_RESOLVE, host_resolutions);
	curl_easy_setopt(slot->curl, CURLOPT_ERRORBUFFER, curl_errorstr);
	curl_easy_setopt(slot->curl, CURLOPT_CUSTOMREQUEST, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_READFUNCTION, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDS, NULL);
	curl_easy_setopt(slot->curl, CURLOPT_POSTFIELDSIZE, -1L);
	curl_easy_setopt(slot->curl, CURLOPT_UPLOAD, 0);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(slot->curl, CURLOPT_FAILONERROR, 1);
	curl_easy_setopt(slot->curl, CURLOPT_RANGE, NULL);

	/*
	 * Default following to off unless "ALWAYS" is configured; this gives
	 * callers a sane starting point, and they can tweak for individual
	 * HTTP_FOLLOW_* cases themselves.
	 */
	if (http_follow_config == HTTP_FOLLOW_ALWAYS)
		curl_easy_setopt(slot->curl, CURLOPT_FOLLOWLOCATION, 1);
	else
		curl_easy_setopt(slot->curl, CURLOPT_FOLLOWLOCATION, 0);

	curl_easy_setopt(slot->curl, CURLOPT_IPRESOLVE, git_curl_ipresolve);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPAUTH, http_auth_methods);
	if (http_auth.password || http_auth.credential || curl_empty_auth_enabled())
		init_curl_http_auth(slot->curl);

	return slot;
}

int start_active_slot(struct active_request_slot *slot)
{
	CURLMcode curlm_result = curl_multi_add_handle(curlm, slot->curl);
	int num_transfers;

	if (curlm_result != CURLM_OK &&
	    curlm_result != CURLM_CALL_MULTI_PERFORM) {
		warning("curl_multi_add_handle failed: %s",
			curl_multi_strerror(curlm_result));
		active_requests--;
		slot->in_use = 0;
		return 0;
	}

	/*
	 * We know there must be something to do, since we just added
	 * something.
	 */
	curl_multi_perform(curlm, &num_transfers);
	return 1;
}

struct fill_chain {
	void *data;
	int (*fill)(void *);
	struct fill_chain *next;
};

static struct fill_chain *fill_cfg;

void add_fill_function(void *data, int (*fill)(void *))
{
	struct fill_chain *new_fill = xmalloc(sizeof(*new_fill));
	struct fill_chain **linkp = &fill_cfg;
	new_fill->data = data;
	new_fill->fill = fill;
	new_fill->next = NULL;
	while (*linkp)
		linkp = &(*linkp)->next;
	*linkp = new_fill;
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

void run_active_slot(struct active_request_slot *slot)
{
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

			max_fd = -1;
			FD_ZERO(&readfds);
			FD_ZERO(&writefds);
			FD_ZERO(&excfds);
			curl_multi_fdset(curlm, &readfds, &writefds, &excfds, &max_fd);

			/*
			 * It can happen that curl_multi_timeout returns a pathologically
			 * long timeout when curl_multi_fdset returns no file descriptors
			 * to read.  See commit message for more details.
			 */
			if (max_fd < 0 &&
			    (select_timeout.tv_sec > 0 ||
			     select_timeout.tv_usec > 50000)) {
				select_timeout.tv_sec  = 0;
				select_timeout.tv_usec = 50000;
			}

			select(max_fd+1, &readfds, &writefds, &excfds, &select_timeout);
		}
	}

	/*
	 * The value of slot->finished we set before the loop was used
	 * to set our "finished" variable when our request completed.
	 *
	 * 1. The slot may not have been reused for another request
	 *    yet, in which case it still has &finished.
	 *
	 * 2. The slot may already be in-use to serve another request,
	 *    which can further be divided into two cases:
	 *
	 * (a) If call run_active_slot() hasn't been called for that
	 *     other request, slot->finished would have been cleared
	 *     by get_active_slot() and has NULL.
	 *
	 * (b) If the request did call run_active_slot(), then the
	 *     call would have updated slot->finished at the beginning
	 *     of this function, and with the clearing of the member
	 *     below, we would find that slot->finished is now NULL.
	 *
	 * In all cases, slot->finished has no useful information to
	 * anybody at this point.  Some compilers warn us for
	 * attempting to smuggle a pointer that is about to become
	 * invalid, i.e. &finished.  We clear it here to assure them.
	 */
	slot->finished = NULL;
}

static void release_active_slot(struct active_request_slot *slot)
{
	closedown_active_slot(slot);
	if (slot->curl) {
		xmulti_remove_handle(slot);
		if (curl_session_count > min_curl_sessions) {
			curl_easy_cleanup(slot->curl);
			slot->curl = NULL;
			curl_session_count--;
		}
	}
	fill_active_slots();
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
		strbuf_addstr(buf, hex + 2);
}

char *get_remote_object_url(const char *url, const char *hex,
			    int only_two_digit_prefix)
{
	struct strbuf buf = STRBUF_INIT;
	append_remote_object_url(&buf, url, hex, only_two_digit_prefix);
	return strbuf_detach(&buf, NULL);
}

void normalize_curl_result(CURLcode *result, long http_code,
			   char *errorstr, size_t errorlen)
{
	/*
	 * If we see a failing http code with CURLE_OK, we have turned off
	 * FAILONERROR (to keep the server's custom error response), and should
	 * translate the code into failure here.
	 *
	 * Likewise, if we see a redirect (30x code), that means we turned off
	 * redirect-following, and we should treat the result as an error.
	 */
	if (*result == CURLE_OK && http_code >= 300) {
		*result = CURLE_HTTP_RETURNED_ERROR;
		/*
		 * Normally curl will already have put the "reason phrase"
		 * from the server into curl_errorstr; unfortunately without
		 * FAILONERROR it is lost, so we can give only the numeric
		 * status code.
		 */
		xsnprintf(errorstr, errorlen,
			  "The requested URL returned error: %ld",
			  http_code);
	}
}

static int handle_curl_result(struct slot_results *results)
{
	normalize_curl_result(&results->curl_result, results->http_code,
			      curl_errorstr, sizeof(curl_errorstr));

	if (results->curl_result == CURLE_OK) {
		credential_approve(&http_auth);
		credential_approve(&proxy_auth);
		credential_approve(&cert_auth);
		return HTTP_OK;
	} else if (results->curl_result == CURLE_SSL_CERTPROBLEM) {
		/*
		 * We can't tell from here whether it's a bad path, bad
		 * certificate, bad password, or something else wrong
		 * with the certificate.  So we reject the credential to
		 * avoid caching or saving a bad password.
		 */
		credential_reject(&cert_auth);
		return HTTP_NOAUTH;
	} else if (results->curl_result == CURLE_SSL_PINNEDPUBKEYNOTMATCH) {
		return HTTP_NOMATCHPUBLICKEY;
	} else if (missing_target(results))
		return HTTP_MISSING_TARGET;
	else if (results->http_code == 401) {
		if ((http_auth.username && http_auth.password) ||\
		    (http_auth.authtype && http_auth.credential)) {
			if (http_auth.multistage) {
				credential_clear_secrets(&http_auth);
				return HTTP_REAUTH;
			}
			credential_reject(&http_auth);
			if (always_auth_proactively())
				http_proactive_auth = PROACTIVE_AUTH_NONE;
			return HTTP_NOAUTH;
		} else {
			http_auth_methods &= ~CURLAUTH_GSSNEGOTIATE;
			if (results->auth_avail) {
				http_auth_methods &= results->auth_avail;
				http_auth_methods_restricted = 1;
			}
			return HTTP_REAUTH;
		}
	} else {
		if (results->http_connectcode == 407)
			credential_reject(&proxy_auth);
		if (!curl_errorstr[0])
			strlcpy(curl_errorstr,
				curl_easy_strerror(results->curl_result),
				sizeof(curl_errorstr));
		return HTTP_ERROR;
	}
}

int run_one_slot(struct active_request_slot *slot,
		 struct slot_results *results)
{
	slot->results = results;
	if (!start_active_slot(slot)) {
		xsnprintf(curl_errorstr, sizeof(curl_errorstr),
			  "failed to start HTTP request");
		return HTTP_START_FAILED;
	}

	run_active_slot(slot);
	return handle_curl_result(results);
}

struct curl_slist *http_copy_default_headers(void)
{
	struct curl_slist *headers = NULL;
	const struct string_list_item *item;

	for_each_string_list_item(item, &extra_http_headers)
		headers = curl_slist_append(headers, item->string);

	return headers;
}

static CURLcode curlinfo_strbuf(CURL *curl, CURLINFO info, struct strbuf *buf)
{
	char *ptr;
	CURLcode ret;

	strbuf_reset(buf);
	ret = curl_easy_getinfo(curl, info, &ptr);
	if (!ret && ptr)
		strbuf_addstr(buf, ptr);
	return ret;
}

/*
 * Check for and extract a content-type parameter. "raw"
 * should be positioned at the start of the potential
 * parameter, with any whitespace already removed.
 *
 * "name" is the name of the parameter. The value is appended
 * to "out".
 */
static int extract_param(const char *raw, const char *name,
			 struct strbuf *out)
{
	size_t len = strlen(name);

	if (strncasecmp(raw, name, len))
		return -1;
	raw += len;

	if (*raw != '=')
		return -1;
	raw++;

	while (*raw && !isspace(*raw) && *raw != ';')
		strbuf_addch(out, *raw++);
	return 0;
}

/*
 * Extract a normalized version of the content type, with any
 * spaces suppressed, all letters lowercased, and no trailing ";"
 * or parameters.
 *
 * Note that we will silently remove even invalid whitespace. For
 * example, "text / plain" is specifically forbidden by RFC 2616,
 * but "text/plain" is the only reasonable output, and this keeps
 * our code simple.
 *
 * If the "charset" argument is not NULL, store the value of any
 * charset parameter there.
 *
 * Example:
 *   "TEXT/PLAIN; charset=utf-8" -> "text/plain", "utf-8"
 *   "text / plain" -> "text/plain"
 */
static void extract_content_type(struct strbuf *raw, struct strbuf *type,
				 struct strbuf *charset)
{
	const char *p;

	strbuf_reset(type);
	strbuf_grow(type, raw->len);
	for (p = raw->buf; *p; p++) {
		if (isspace(*p))
			continue;
		if (*p == ';') {
			p++;
			break;
		}
		strbuf_addch(type, tolower(*p));
	}

	if (!charset)
		return;

	strbuf_reset(charset);
	while (*p) {
		while (isspace(*p) || *p == ';')
			p++;
		if (!extract_param(p, "charset", charset))
			return;
		while (*p && !isspace(*p))
			p++;
	}

	if (!charset->len && starts_with(type->buf, "text/"))
		strbuf_addstr(charset, "ISO-8859-1");
}

static void write_accept_language(struct strbuf *buf)
{
	/*
	 * MAX_DECIMAL_PLACES must not be larger than 3. If it is larger than
	 * that, q-value will be smaller than 0.001, the minimum q-value the
	 * HTTP specification allows. See
	 * https://datatracker.ietf.org/doc/html/rfc7231#section-5.3.1 for q-value.
	 */
	const int MAX_DECIMAL_PLACES = 3;
	const int MAX_LANGUAGE_TAGS = 1000;
	const int MAX_ACCEPT_LANGUAGE_HEADER_SIZE = 4000;
	char **language_tags = NULL;
	int num_langs = 0;
	const char *s = get_preferred_languages();
	int i;
	struct strbuf tag = STRBUF_INIT;

	/* Don't add Accept-Language header if no language is preferred. */
	if (!s)
		return;

	/*
	 * Split the colon-separated string of preferred languages into
	 * language_tags array.
	 */
	do {
		/* collect language tag */
		for (; *s && (isalnum(*s) || *s == '_'); s++)
			strbuf_addch(&tag, *s == '_' ? '-' : *s);

		/* skip .codeset, @modifier and any other unnecessary parts */
		while (*s && *s != ':')
			s++;

		if (tag.len) {
			num_langs++;
			REALLOC_ARRAY(language_tags, num_langs);
			language_tags[num_langs - 1] = strbuf_detach(&tag, NULL);
			if (num_langs >= MAX_LANGUAGE_TAGS - 1) /* -1 for '*' */
				break;
		}
	} while (*s++);

	/* write Accept-Language header into buf */
	if (num_langs) {
		int last_buf_len = 0;
		int max_q;
		int decimal_places;
		char q_format[32];

		/* add '*' */
		REALLOC_ARRAY(language_tags, num_langs + 1);
		language_tags[num_langs++] = xstrdup("*");

		/* compute decimal_places */
		for (max_q = 1, decimal_places = 0;
		     max_q < num_langs && decimal_places <= MAX_DECIMAL_PLACES;
		     decimal_places++, max_q *= 10)
			;

		xsnprintf(q_format, sizeof(q_format), ";q=0.%%0%dd", decimal_places);

		strbuf_addstr(buf, "Accept-Language: ");

		for (i = 0; i < num_langs; i++) {
			if (i > 0)
				strbuf_addstr(buf, ", ");

			strbuf_addstr(buf, language_tags[i]);

			if (i > 0)
				strbuf_addf(buf, q_format, max_q - i);

			if (buf->len > MAX_ACCEPT_LANGUAGE_HEADER_SIZE) {
				strbuf_remove(buf, last_buf_len, buf->len - last_buf_len);
				break;
			}

			last_buf_len = buf->len;
		}
	}

	for (i = 0; i < num_langs; i++)
		free(language_tags[i]);
	free(language_tags);
}

/*
 * Get an Accept-Language header which indicates user's preferred languages.
 *
 * Examples:
 *   LANGUAGE= -> ""
 *   LANGUAGE=ko:en -> "Accept-Language: ko, en; q=0.9, *; q=0.1"
 *   LANGUAGE=ko_KR.UTF-8:sr@latin -> "Accept-Language: ko-KR, sr; q=0.9, *; q=0.1"
 *   LANGUAGE=ko LANG=en_US.UTF-8 -> "Accept-Language: ko, *; q=0.1"
 *   LANGUAGE= LANG=en_US.UTF-8 -> "Accept-Language: en-US, *; q=0.1"
 *   LANGUAGE= LANG=C -> ""
 */
const char *http_get_accept_language_header(void)
{
	if (!cached_accept_language) {
		struct strbuf buf = STRBUF_INIT;
		write_accept_language(&buf);
		if (buf.len > 0)
			cached_accept_language = strbuf_detach(&buf, NULL);
	}

	return cached_accept_language;
}

static void http_opt_request_remainder(CURL *curl, off_t pos)
{
	char buf[128];
	xsnprintf(buf, sizeof(buf), "%"PRIuMAX"-", (uintmax_t)pos);
	curl_easy_setopt(curl, CURLOPT_RANGE, buf);
}

/* http_request() targets */
#define HTTP_REQUEST_STRBUF	0
#define HTTP_REQUEST_FILE	1

static int http_request(const char *url,
			void *result, int target,
			const struct http_get_options *options)
{
	struct active_request_slot *slot;
	struct slot_results results;
	struct curl_slist *headers = http_copy_default_headers();
	struct strbuf buf = STRBUF_INIT;
	const char *accept_language;
	int ret;

	slot = get_active_slot();
	curl_easy_setopt(slot->curl, CURLOPT_HTTPGET, 1);

	if (!result) {
		curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 1);
	} else {
		curl_easy_setopt(slot->curl, CURLOPT_NOBODY, 0);
		curl_easy_setopt(slot->curl, CURLOPT_WRITEDATA, result);

		if (target == HTTP_REQUEST_FILE) {
			off_t posn = ftello(result);
			curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
					 fwrite);
			if (posn > 0)
				http_opt_request_remainder(slot->curl, posn);
		} else
			curl_easy_setopt(slot->curl, CURLOPT_WRITEFUNCTION,
					 fwrite_buffer);
	}

	curl_easy_setopt(slot->curl, CURLOPT_HEADERFUNCTION, fwrite_wwwauth);

	accept_language = http_get_accept_language_header();

	if (accept_language)
		headers = curl_slist_append(headers, accept_language);

	strbuf_addstr(&buf, "Pragma:");
	if (options && options->no_cache)
		strbuf_addstr(&buf, " no-cache");
	if (options && options->initial_request &&
	    http_follow_config == HTTP_FOLLOW_INITIAL)
		curl_easy_setopt(slot->curl, CURLOPT_FOLLOWLOCATION, 1);

	headers = curl_slist_append(headers, buf.buf);

	/* Add additional headers here */
	if (options && options->extra_headers) {
		const struct string_list_item *item;
		if (options && options->extra_headers) {
			for_each_string_list_item(item, options->extra_headers) {
				headers = curl_slist_append(headers, item->string);
			}
		}
	}

	headers = http_append_auth_header(&http_auth, headers);

	curl_easy_setopt(slot->curl, CURLOPT_URL, url);
	curl_easy_setopt(slot->curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(slot->curl, CURLOPT_ENCODING, "");
	curl_easy_setopt(slot->curl, CURLOPT_FAILONERROR, 0);

	ret = run_one_slot(slot, &results);

	if (options && options->content_type) {
		struct strbuf raw = STRBUF_INIT;
		curlinfo_strbuf(slot->curl, CURLINFO_CONTENT_TYPE, &raw);
		extract_content_type(&raw, options->content_type,
				     options->charset);
		strbuf_release(&raw);
	}

	if (options && options->effective_url)
		curlinfo_strbuf(slot->curl, CURLINFO_EFFECTIVE_URL,
				options->effective_url);

	curl_slist_free_all(headers);
	strbuf_release(&buf);

	return ret;
}

/*
 * Update the "base" url to a more appropriate value, as deduced by
 * redirects seen when requesting a URL starting with "url".
 *
 * The "asked" parameter is a URL that we asked curl to access, and must begin
 * with "base".
 *
 * The "got" parameter is the URL that curl reported to us as where we ended
 * up.
 *
 * Returns 1 if we updated the base url, 0 otherwise.
 *
 * Our basic strategy is to compare "base" and "asked" to find the bits
 * specific to our request. We then strip those bits off of "got" to yield the
 * new base. So for example, if our base is "http://example.com/foo.git",
 * and we ask for "http://example.com/foo.git/info/refs", we might end up
 * with "https://other.example.com/foo.git/info/refs". We would want the
 * new URL to become "https://other.example.com/foo.git".
 *
 * Note that this assumes a sane redirect scheme. It's entirely possible
 * in the example above to end up at a URL that does not even end in
 * "info/refs".  In such a case we die. There's not much we can do, such a
 * scheme is unlikely to represent a real git repository, and failing to
 * rewrite the base opens options for malicious redirects to do funny things.
 */
static int update_url_from_redirect(struct strbuf *base,
				    const char *asked,
				    const struct strbuf *got)
{
	const char *tail;
	size_t new_len;

	if (!strcmp(asked, got->buf))
		return 0;

	if (!skip_prefix(asked, base->buf, &tail))
		BUG("update_url_from_redirect: %s is not a superset of %s",
		    asked, base->buf);

	new_len = got->len;
	if (!strip_suffix_mem(got->buf, &new_len, tail))
		die(_("unable to update url base from redirection:\n"
		      "  asked for: %s\n"
		      "   redirect: %s"),
		    asked, got->buf);

	strbuf_reset(base);
	strbuf_add(base, got->buf, new_len);

	return 1;
}

static int http_request_reauth(const char *url,
			       void *result, int target,
			       struct http_get_options *options)
{
	int i = 3;
	int ret;

	if (always_auth_proactively())
		credential_fill(&http_auth, 1);

	ret = http_request(url, result, target, options);

	if (ret != HTTP_OK && ret != HTTP_REAUTH)
		return ret;

	if (options && options->effective_url && options->base_url) {
		if (update_url_from_redirect(options->base_url,
					     url, options->effective_url)) {
			credential_from_url(&http_auth, options->base_url->buf);
			url = options->effective_url->buf;
		}
	}

	while (ret == HTTP_REAUTH && --i) {
		/*
		 * The previous request may have put cruft into our output stream; we
		 * should clear it out before making our next request.
		 */
		switch (target) {
		case HTTP_REQUEST_STRBUF:
			strbuf_reset(result);
			break;
		case HTTP_REQUEST_FILE: {
			FILE *f = result;
			if (fflush(f)) {
				error_errno("unable to flush a file");
				return HTTP_START_FAILED;
			}
			rewind(f);
			if (ftruncate(fileno(f), 0) < 0) {
				error_errno("unable to truncate a file");
				return HTTP_START_FAILED;
			}
			break;
		}
		default:
			BUG("Unknown http_request target");
		}

		credential_fill(&http_auth, 1);

		ret = http_request(url, result, target, options);
	}
	return ret;
}

int http_get_strbuf(const char *url,
		    struct strbuf *result,
		    struct http_get_options *options)
{
	return http_request_reauth(url, result, HTTP_REQUEST_STRBUF, options);
}

/*
 * Downloads a URL and stores the result in the given file.
 *
 * If a previous interrupted download is detected (i.e. a previous temporary
 * file is still around) the download is resumed.
 */
int http_get_file(const char *url, const char *filename,
		  struct http_get_options *options)
{
	int ret;
	struct strbuf tmpfile = STRBUF_INIT;
	FILE *result;

	strbuf_addf(&tmpfile, "%s.temp", filename);
	result = fopen(tmpfile.buf, "a");
	if (!result) {
		error("Unable to open local file %s", tmpfile.buf);
		ret = HTTP_ERROR;
		goto cleanup;
	}

	ret = http_request_reauth(url, result, HTTP_REQUEST_FILE, options);
	fclose(result);

	if (ret == HTTP_OK && finalize_object_file(tmpfile.buf, filename))
		ret = HTTP_ERROR;
cleanup:
	strbuf_release(&tmpfile);
	return ret;
}

int http_fetch_ref(const char *base, struct ref *ref)
{
	struct http_get_options options = {0};
	char *url;
	struct strbuf buffer = STRBUF_INIT;
	int ret = -1;

	options.no_cache = 1;

	url = quote_ref_url(base, ref->name);
	if (http_get_strbuf(url, &buffer, &options) == HTTP_OK) {
		strbuf_rtrim(&buffer);
		if (buffer.len == the_hash_algo->hexsz)
			ret = get_oid_hex(buffer.buf, &ref->old_oid);
		else if (starts_with(buffer.buf, "ref: ")) {
			ref->symref = xstrdup(buffer.buf + 5);
			ret = 0;
		}
	}

	strbuf_release(&buffer);
	free(url);
	return ret;
}

/* Helpers for fetching packs */
static char *fetch_pack_index(unsigned char *hash, const char *base_url)
{
	char *url, *tmp;
	struct strbuf buf = STRBUF_INIT;

	if (http_is_verbose)
		fprintf(stderr, "Getting index for pack %s\n", hash_to_hex(hash));

	end_url_with_slash(&buf, base_url);
	strbuf_addf(&buf, "objects/pack/pack-%s.idx", hash_to_hex(hash));
	url = strbuf_detach(&buf, NULL);

	/*
	 * Don't put this into packs/, since it's just temporary and we don't
	 * want to confuse it with our local .idx files.  We'll generate our
	 * own index if we choose to download the matching packfile.
	 *
	 * It's tempting to use xmks_tempfile() here, but it's important that
	 * the file not exist, otherwise http_get_file() complains. So we
	 * create a filename that should be unique, and then just register it
	 * as a tempfile so that it will get cleaned up on exit.
	 *
	 * In theory we could hold on to the tempfile and delete these as soon
	 * as we download the matching pack, but it would take a bit of
	 * refactoring. Leaving them until the process ends is probably OK.
	 */
	tmp = xstrfmt("%s/tmp_pack_%s.idx",
		      repo_get_object_directory(the_repository),
		      hash_to_hex(hash));
	register_tempfile(tmp);

	if (http_get_file(url, tmp, NULL) != HTTP_OK) {
		error("Unable to get pack index %s", url);
		FREE_AND_NULL(tmp);
	}

	free(url);
	return tmp;
}

static int fetch_and_setup_pack_index(struct packed_git **packs_head,
	unsigned char *sha1, const char *base_url)
{
	struct packed_git *new_pack, *p;
	char *tmp_idx = NULL;
	int ret;

	/*
	 * If we already have the pack locally, no need to fetch its index or
	 * even add it to list; we already have all of its objects.
	 */
	for (p = get_all_packs(the_repository); p; p = p->next) {
		if (hasheq(p->hash, sha1, the_repository->hash_algo))
			return 0;
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
	if (!ret)
		close_pack_index(new_pack);
	free(tmp_idx);
	if (ret)
		return -1;

	new_pack->next = *packs_head;
	*packs_head = new_pack;
	return 0;
}

int http_get_info_packs(const char *base_url, struct packed_git **packs_head)
{
	struct http_get_options options = {0};
	int ret = 0;
	char *url;
	const char *data;
	struct strbuf buf = STRBUF_INIT;
	struct object_id oid;

	end_url_with_slash(&buf, base_url);
	strbuf_addstr(&buf, "objects/info/packs");
	url = strbuf_detach(&buf, NULL);

	options.no_cache = 1;
	ret = http_get_strbuf(url, &buf, &options);
	if (ret != HTTP_OK)
		goto cleanup;

	data = buf.buf;
	while (*data) {
		if (skip_prefix(data, "P pack-", &data) &&
		    !parse_oid_hex(data, &oid, &data) &&
		    skip_prefix(data, ".pack", &data) &&
		    (*data == '\n' || *data == '\0')) {
			fetch_and_setup_pack_index(packs_head, oid.hash, base_url);
		} else {
			data = strchrnul(data, '\n');
		}
		if (*data)
			data++; /* skip past newline */
	}

cleanup:
	free(url);
	strbuf_release(&buf);
	return ret;
}

void release_http_pack_request(struct http_pack_request *preq)
{
	if (preq->packfile) {
		fclose(preq->packfile);
		preq->packfile = NULL;
	}
	preq->slot = NULL;
	strbuf_release(&preq->tmpfile);
	curl_slist_free_all(preq->headers);
	free(preq->url);
	free(preq);
}

static const char *default_index_pack_args[] =
	{"index-pack", "--stdin", NULL};

int finish_http_pack_request(struct http_pack_request *preq)
{
	struct child_process ip = CHILD_PROCESS_INIT;
	int tmpfile_fd;
	int ret = 0;

	fclose(preq->packfile);
	preq->packfile = NULL;

	tmpfile_fd = xopen(preq->tmpfile.buf, O_RDONLY);

	ip.git_cmd = 1;
	ip.in = tmpfile_fd;
	strvec_pushv(&ip.args, preq->index_pack_args ?
		     preq->index_pack_args :
		     default_index_pack_args);

	if (preq->preserve_index_pack_stdout)
		ip.out = 0;
	else
		ip.no_stdout = 1;

	if (run_command(&ip)) {
		ret = -1;
		goto cleanup;
	}

cleanup:
	close(tmpfile_fd);
	unlink(preq->tmpfile.buf);
	return ret;
}

void http_install_packfile(struct packed_git *p,
			   struct packed_git **list_to_remove_from)
{
	struct packed_git **lst = list_to_remove_from;

	while (*lst != p)
		lst = &((*lst)->next);
	*lst = (*lst)->next;

	install_packed_git(the_repository, p);
}

struct http_pack_request *new_http_pack_request(
	const unsigned char *packed_git_hash, const char *base_url) {

	struct strbuf buf = STRBUF_INIT;

	end_url_with_slash(&buf, base_url);
	strbuf_addf(&buf, "objects/pack/pack-%s.pack",
		hash_to_hex(packed_git_hash));
	return new_direct_http_pack_request(packed_git_hash,
					    strbuf_detach(&buf, NULL));
}

struct http_pack_request *new_direct_http_pack_request(
	const unsigned char *packed_git_hash, char *url)
{
	off_t prev_posn = 0;
	struct http_pack_request *preq;

	CALLOC_ARRAY(preq, 1);
	strbuf_init(&preq->tmpfile, 0);

	preq->url = url;

	odb_pack_name(&preq->tmpfile, packed_git_hash, "pack");
	strbuf_addstr(&preq->tmpfile, ".temp");
	preq->packfile = fopen(preq->tmpfile.buf, "a");
	if (!preq->packfile) {
		error("Unable to open local file %s for pack",
		      preq->tmpfile.buf);
		goto abort;
	}

	preq->slot = get_active_slot();
	preq->headers = object_request_headers();
	curl_easy_setopt(preq->slot->curl, CURLOPT_WRITEDATA, preq->packfile);
	curl_easy_setopt(preq->slot->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(preq->slot->curl, CURLOPT_URL, preq->url);
	curl_easy_setopt(preq->slot->curl, CURLOPT_HTTPHEADER, preq->headers);

	/*
	 * If there is data present from a previous transfer attempt,
	 * resume where it left off
	 */
	prev_posn = ftello(preq->packfile);
	if (prev_posn>0) {
		if (http_is_verbose)
			fprintf(stderr,
				"Resuming fetch of pack %s at byte %"PRIuMAX"\n",
				hash_to_hex(packed_git_hash),
				(uintmax_t)prev_posn);
		http_opt_request_remainder(preq->slot->curl, prev_posn);
	}

	return preq;

abort:
	strbuf_release(&preq->tmpfile);
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
	struct http_object_request *freq = data;
	struct active_request_slot *slot = freq->slot;

	if (slot) {
		CURLcode c = curl_easy_getinfo(slot->curl, CURLINFO_HTTP_CODE,
						&slot->http_code);
		if (c != CURLE_OK)
			BUG("curl_easy_getinfo for HTTP code failed: %s",
				curl_easy_strerror(c));
		if (slot->http_code >= 300)
			return nmemb;
	}

	do {
		ssize_t retval = xwrite(freq->localfile,
					(char *) ptr + posn, size - posn);
		if (retval < 0)
			return posn / eltsize;
		posn += retval;
	} while (posn < size);

	freq->stream.avail_in = size;
	freq->stream.next_in = (void *)ptr;
	do {
		freq->stream.next_out = expn;
		freq->stream.avail_out = sizeof(expn);
		freq->zret = git_inflate(&freq->stream, Z_SYNC_FLUSH);
		the_hash_algo->update_fn(&freq->c, expn,
					 sizeof(expn) - freq->stream.avail_out);
	} while (freq->stream.avail_in && freq->zret == Z_OK);
	return nmemb;
}

struct http_object_request *new_http_object_request(const char *base_url,
						    const struct object_id *oid)
{
	char *hex = oid_to_hex(oid);
	struct strbuf filename = STRBUF_INIT;
	struct strbuf prevfile = STRBUF_INIT;
	int prevlocal;
	char prev_buf[PREV_BUF_SIZE];
	ssize_t prev_read = 0;
	off_t prev_posn = 0;
	struct http_object_request *freq;

	CALLOC_ARRAY(freq, 1);
	strbuf_init(&freq->tmpfile, 0);
	oidcpy(&freq->oid, oid);
	freq->localfile = -1;

	loose_object_path(the_repository, &filename, oid);
	strbuf_addf(&freq->tmpfile, "%s.temp", filename.buf);

	strbuf_addf(&prevfile, "%s.prev", filename.buf);
	unlink_or_warn(prevfile.buf);
	rename(freq->tmpfile.buf, prevfile.buf);
	unlink_or_warn(freq->tmpfile.buf);
	strbuf_release(&filename);

	if (freq->localfile != -1)
		error("fd leakage in start: %d", freq->localfile);
	freq->localfile = open(freq->tmpfile.buf,
			       O_WRONLY | O_CREAT | O_EXCL, 0666);
	/*
	 * This could have failed due to the "lazy directory creation";
	 * try to mkdir the last path component.
	 */
	if (freq->localfile < 0 && errno == ENOENT) {
		char *dir = strrchr(freq->tmpfile.buf, '/');
		if (dir) {
			*dir = 0;
			mkdir(freq->tmpfile.buf, 0777);
			*dir = '/';
		}
		freq->localfile = open(freq->tmpfile.buf,
				       O_WRONLY | O_CREAT | O_EXCL, 0666);
	}

	if (freq->localfile < 0) {
		error_errno("Couldn't create temporary file %s",
			    freq->tmpfile.buf);
		goto abort;
	}

	git_inflate_init(&freq->stream);

	the_hash_algo->init_fn(&freq->c);

	freq->url = get_remote_object_url(base_url, hex, 0);

	/*
	 * If a previous temp file is present, process what was already
	 * fetched.
	 */
	prevlocal = open(prevfile.buf, O_RDONLY);
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
	unlink_or_warn(prevfile.buf);
	strbuf_release(&prevfile);

	/*
	 * Reset inflate/SHA1 if there was an error reading the previous temp
	 * file; also rewind to the beginning of the local file.
	 */
	if (prev_read == -1) {
		git_inflate_end(&freq->stream);
		memset(&freq->stream, 0, sizeof(freq->stream));
		git_inflate_init(&freq->stream);
		the_hash_algo->init_fn(&freq->c);
		if (prev_posn>0) {
			prev_posn = 0;
			lseek(freq->localfile, 0, SEEK_SET);
			if (ftruncate(freq->localfile, 0) < 0) {
				error_errno("Couldn't truncate temporary file %s",
					    freq->tmpfile.buf);
				goto abort;
			}
		}
	}

	freq->slot = get_active_slot();
	freq->headers = object_request_headers();

	curl_easy_setopt(freq->slot->curl, CURLOPT_WRITEDATA, freq);
	curl_easy_setopt(freq->slot->curl, CURLOPT_FAILONERROR, 0);
	curl_easy_setopt(freq->slot->curl, CURLOPT_WRITEFUNCTION, fwrite_sha1_file);
	curl_easy_setopt(freq->slot->curl, CURLOPT_ERRORBUFFER, freq->errorstr);
	curl_easy_setopt(freq->slot->curl, CURLOPT_URL, freq->url);
	curl_easy_setopt(freq->slot->curl, CURLOPT_HTTPHEADER, freq->headers);

	/*
	 * If we have successfully processed data from a previous fetch
	 * attempt, only fetch the data we don't already have.
	 */
	if (prev_posn>0) {
		if (http_is_verbose)
			fprintf(stderr,
				"Resuming fetch of object %s at byte %"PRIuMAX"\n",
				hex, (uintmax_t)prev_posn);
		http_opt_request_remainder(freq->slot->curl, prev_posn);
	}

	return freq;

abort:
	strbuf_release(&prevfile);
	free(freq->url);
	free(freq);
	return NULL;
}

void process_http_object_request(struct http_object_request *freq)
{
	if (!freq->slot)
		return;
	freq->curl_result = freq->slot->curl_result;
	freq->http_code = freq->slot->http_code;
	freq->slot = NULL;
}

int finish_http_object_request(struct http_object_request *freq)
{
	struct stat st;
	struct strbuf filename = STRBUF_INIT;

	close(freq->localfile);
	freq->localfile = -1;

	process_http_object_request(freq);

	if (freq->http_code == 416) {
		warning("requested range invalid; we may already have all the data.");
	} else if (freq->curl_result != CURLE_OK) {
		if (stat(freq->tmpfile.buf, &st) == 0)
			if (st.st_size == 0)
				unlink_or_warn(freq->tmpfile.buf);
		return -1;
	}

	the_hash_algo->final_oid_fn(&freq->real_oid, &freq->c);
	if (freq->zret != Z_STREAM_END) {
		unlink_or_warn(freq->tmpfile.buf);
		return -1;
	}
	if (!oideq(&freq->oid, &freq->real_oid)) {
		unlink_or_warn(freq->tmpfile.buf);
		return -1;
	}
	loose_object_path(the_repository, &filename, &freq->oid);
	freq->rename = finalize_object_file(freq->tmpfile.buf, filename.buf);
	strbuf_release(&filename);

	return freq->rename;
}

void abort_http_object_request(struct http_object_request **freq_p)
{
	struct http_object_request *freq = *freq_p;
	unlink_or_warn(freq->tmpfile.buf);

	release_http_object_request(freq_p);
}

void release_http_object_request(struct http_object_request **freq_p)
{
	struct http_object_request *freq = *freq_p;
	if (freq->localfile != -1) {
		close(freq->localfile);
		freq->localfile = -1;
	}
	FREE_AND_NULL(freq->url);
	if (freq->slot) {
		freq->slot->callback_func = NULL;
		freq->slot->callback_data = NULL;
		release_active_slot(freq->slot);
		freq->slot = NULL;
	}
	curl_slist_free_all(freq->headers);
	strbuf_release(&freq->tmpfile);
	git_inflate_end(&freq->stream);

	free(freq);
	*freq_p = NULL;
}
