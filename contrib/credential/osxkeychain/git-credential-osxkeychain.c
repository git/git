#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Security/Security.h>
#include "git-compat-util.h"
#include "strbuf.h"
#include "wrapper.h"

#define ENCODING kCFStringEncodingUTF8
static CFStringRef protocol; /* Stores constant strings - not memory managed */
static CFStringRef host;
static CFNumberRef port;
static CFStringRef path;
static CFStringRef username;
static CFDataRef password;
static CFDataRef password_expiry_utc;
static CFDataRef oauth_refresh_token;
static char *state_seen;

static void clear_credential(void)
{
	if (host) {
		CFRelease(host);
		host = NULL;
	}
	if (port) {
		CFRelease(port);
		port = NULL;
	}
	if (path) {
		CFRelease(path);
		path = NULL;
	}
	if (username) {
		CFRelease(username);
		username = NULL;
	}
	if (password) {
		CFRelease(password);
		password = NULL;
	}
	if (password_expiry_utc) {
		CFRelease(password_expiry_utc);
		password_expiry_utc = NULL;
	}
	if (oauth_refresh_token) {
		CFRelease(oauth_refresh_token);
		oauth_refresh_token = NULL;
	}
}

#define STRING_WITH_LENGTH(s) s, sizeof(s) - 1

static CFDictionaryRef create_dictionary(CFAllocatorRef allocator, ...)
{
	va_list args;
	const void *key;
	CFMutableDictionaryRef result;

	result = CFDictionaryCreateMutable(allocator,
					   0,
					   &kCFTypeDictionaryKeyCallBacks,
					   &kCFTypeDictionaryValueCallBacks);


	va_start(args, allocator);
	while ((key = va_arg(args, const void *)) != NULL) {
		const void *value;
		value = va_arg(args, const void *);
		if (value)
			CFDictionarySetValue(result, key, value);
	}
	va_end(args);

	return result;
}

#define CREATE_SEC_ATTRIBUTES(...) \
	create_dictionary(kCFAllocatorDefault, \
			  kSecClass, kSecClassInternetPassword, \
			  kSecAttrServer, host, \
			  kSecAttrAccount, username, \
			  kSecAttrPath, path, \
			  kSecAttrPort, port, \
			  kSecAttrProtocol, protocol, \
			  kSecAttrAuthenticationType, \
			  kSecAttrAuthenticationTypeDefault, \
			  __VA_ARGS__);

static void write_item(const char *what, const char *buf, size_t len)
{
	printf("%s=", what);
	fwrite(buf, 1, len, stdout);
	putchar('\n');
}

static void write_item_strbuf(struct strbuf *sb, const char *what, const char *buf, int n)
{
	char s[32];

	xsnprintf(s, sizeof(s), "__%s=", what);
	strbuf_add(sb, s, strlen(s));
	strbuf_add(sb, buf, n);
}

static void write_item_strbuf_cfstring(struct strbuf *sb, const char *what, CFStringRef ref)
{
	char *buf;
	int len;

	if (!ref)
		return;
	len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(ref), ENCODING) + 1;
	buf = xmalloc(len);
	if (CFStringGetCString(ref, buf, len, ENCODING))
		write_item_strbuf(sb, what, buf, strlen(buf));
	free(buf);
}

static void write_item_strbuf_cfnumber(struct strbuf *sb, const char *what, CFNumberRef ref)
{
	short n;
	char buf[32];

	if (!ref)
		return;
	if (!CFNumberGetValue(ref, kCFNumberShortType, &n))
		return;
	xsnprintf(buf, sizeof(buf), "%d", n);
	write_item_strbuf(sb, what, buf, strlen(buf));
}

static void write_item_strbuf_cfdata(struct strbuf *sb, const char *what, CFDataRef ref)
{
	char *buf;
	int len;

	if (!ref)
		return;
	buf = (char *)CFDataGetBytePtr(ref);
	if (!buf || strlen(buf) == 0)
		return;
	len = CFDataGetLength(ref);
	write_item_strbuf(sb, what, buf, len);
}

static void encode_state_seen(struct strbuf *sb)
{
	strbuf_add(sb, "osxkeychain:seen=", strlen("osxkeychain:seen="));
	write_item_strbuf_cfstring(sb, "host", host);
	write_item_strbuf_cfnumber(sb, "port", port);
	write_item_strbuf_cfstring(sb, "path", path);
	write_item_strbuf_cfstring(sb, "username", username);
	write_item_strbuf_cfdata(sb, "password", password);
}

static void find_username_in_item(CFDictionaryRef item)
{
	CFStringRef account_ref;
	char *username_buf;
	CFIndex buffer_len;

	account_ref = CFDictionaryGetValue(item, kSecAttrAccount);
	if (!account_ref)
	{
		write_item("username", "", 0);
		return;
	}
	username = CFStringCreateCopy(kCFAllocatorDefault, account_ref);

	username_buf = (char *)CFStringGetCStringPtr(account_ref, ENCODING);
	if (username_buf)
	{
		write_item("username", username_buf, strlen(username_buf));
		return;
	}

	/* If we can't get a CString pointer then
	 * we need to allocate our own buffer */
	buffer_len = CFStringGetMaximumSizeForEncoding(
			CFStringGetLength(account_ref), ENCODING) + 1;
	username_buf = xmalloc(buffer_len);
	if (CFStringGetCString(account_ref,
				username_buf,
				buffer_len,
				ENCODING)) {
		write_item("username", username_buf, strlen(username_buf));
	}
	free(username_buf);
}

static OSStatus find_internet_password(void)
{
	CFDictionaryRef attrs;
	CFDictionaryRef item;
	CFDataRef data;
	OSStatus result;

	attrs = CREATE_SEC_ATTRIBUTES(kSecMatchLimit, kSecMatchLimitOne,
				      kSecReturnAttributes, kCFBooleanTrue,
				      kSecReturnData, kCFBooleanTrue,
				      NULL);
	result = SecItemCopyMatching(attrs, (CFTypeRef *)&item);
	if (result) {
		goto out;
	}

	data = CFDictionaryGetValue(item, kSecValueData);
	password = CFDataCreateCopy(kCFAllocatorDefault, data);

	write_item("password",
		   (const char *)CFDataGetBytePtr(data),
		   CFDataGetLength(data));
	if (!username)
		find_username_in_item(item);

	CFRelease(item);

	write_item("capability[]", "state", strlen("state"));
	{
		struct strbuf sb;

		strbuf_init(&sb, 1024);
		encode_state_seen(&sb);
		write_item("state[]", sb.buf, strlen(sb.buf));
		strbuf_release(&sb);
	}

out:
	CFRelease(attrs);

	/* We consider not found to not be an error */
	if (result == errSecItemNotFound)
		result = errSecSuccess;

	return result;
}

static OSStatus delete_ref(const void *itemRef)
{
	CFArrayRef item_ref_list;
	CFDictionaryRef delete_query;
	OSStatus result;

	item_ref_list = CFArrayCreate(kCFAllocatorDefault,
				      &itemRef,
				      1,
				      &kCFTypeArrayCallBacks);
	delete_query = create_dictionary(kCFAllocatorDefault,
					 kSecClass, kSecClassInternetPassword,
					 kSecMatchItemList, item_ref_list,
					 NULL);

	if (password) {
		/* We only want to delete items with a matching password */
		CFIndex capacity;
		CFMutableDictionaryRef query;
		CFDataRef data;

		capacity = CFDictionaryGetCount(delete_query) + 1;
		query = CFDictionaryCreateMutableCopy(kCFAllocatorDefault,
						      capacity,
						      delete_query);
		CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
		result = SecItemCopyMatching(query, (CFTypeRef *)&data);
		if (!result) {
			CFDataRef kc_password;
			const UInt8 *raw_data;
			const UInt8 *line;

			/* Don't match appended metadata */
			raw_data = CFDataGetBytePtr(data);
			line = memchr(raw_data, '\n', CFDataGetLength(data));
			if (line)
				kc_password = CFDataCreateWithBytesNoCopy(
						kCFAllocatorDefault,
						raw_data,
						line - raw_data,
						kCFAllocatorNull);
			else
				kc_password = data;

			if (CFEqual(kc_password, password))
				result = SecItemDelete(delete_query);

			if (line)
				CFRelease(kc_password);
			CFRelease(data);
		}

		CFRelease(query);
	} else {
		result = SecItemDelete(delete_query);
	}

	CFRelease(delete_query);
	CFRelease(item_ref_list);

	return result;
}

static OSStatus delete_internet_password(void)
{
	CFDictionaryRef attrs;
	CFArrayRef refs;
	OSStatus result;

	/*
	 * Require at least a protocol and host for removal, which is what git
	 * will give us; if you want to do something more fancy, use the
	 * Keychain manager.
	 */
	if (!protocol || !host)
		return -1;

	attrs = CREATE_SEC_ATTRIBUTES(kSecMatchLimit, kSecMatchLimitAll,
				      kSecReturnRef, kCFBooleanTrue,
				      NULL);
	result = SecItemCopyMatching(attrs, (CFTypeRef *)&refs);
	CFRelease(attrs);

	if (!result) {
		for (CFIndex i = 0; !result && i < CFArrayGetCount(refs); i++)
			result = delete_ref(CFArrayGetValueAtIndex(refs, i));

		CFRelease(refs);
	}

	/* We consider not found to not be an error */
	if (result == errSecItemNotFound)
		result = errSecSuccess;

	return result;
}

static OSStatus add_internet_password(void)
{
	CFMutableDataRef data;
	CFDictionaryRef attrs;
	OSStatus result;

	/* Only store complete credentials */
	if (!protocol || !host || !username || !password)
		return -1;

	if (state_seen) {
		struct strbuf sb;

		strbuf_init(&sb, 1024);
		encode_state_seen(&sb);
		if (!strcmp(state_seen, sb.buf)) {
			strbuf_release(&sb);
			return errSecSuccess;
		}
		strbuf_release(&sb);
	}

	data = CFDataCreateMutableCopy(kCFAllocatorDefault, 0, password);
	if (password_expiry_utc) {
		CFDataAppendBytes(data,
		    (const UInt8 *)STRING_WITH_LENGTH("\npassword_expiry_utc="));
		CFDataAppendBytes(data,
				  CFDataGetBytePtr(password_expiry_utc),
				  CFDataGetLength(password_expiry_utc));
	}
	if (oauth_refresh_token) {
		CFDataAppendBytes(data,
		    (const UInt8 *)STRING_WITH_LENGTH("\noauth_refresh_token="));
		CFDataAppendBytes(data,
				  CFDataGetBytePtr(oauth_refresh_token),
				  CFDataGetLength(oauth_refresh_token));
	}

	attrs = CREATE_SEC_ATTRIBUTES(kSecValueData, data,
				      NULL);

	result = SecItemAdd(attrs, NULL);
	if (result == errSecDuplicateItem) {
		CFDictionaryRef query;
		query = CREATE_SEC_ATTRIBUTES(NULL);
		result = SecItemUpdate(query, attrs);
		CFRelease(query);
	}

	CFRelease(data);
	CFRelease(attrs);

	return result;
}

static void read_credential(void)
{
	char *buf = NULL;
	size_t alloc;
	ssize_t line_len;

	while ((line_len = getline(&buf, &alloc, stdin)) > 0) {
		char *v;

		if (!strcmp(buf, "\n"))
			break;
		buf[line_len-1] = '\0';

		v = strchr(buf, '=');
		if (!v)
			die("bad input: %s", buf);
		*v++ = '\0';

		if (!strcmp(buf, "protocol")) {
			if (!strcmp(v, "imap"))
				protocol = kSecAttrProtocolIMAP;
			else if (!strcmp(v, "imaps"))
				protocol = kSecAttrProtocolIMAPS;
			else if (!strcmp(v, "ftp"))
				protocol = kSecAttrProtocolFTP;
			else if (!strcmp(v, "ftps"))
				protocol = kSecAttrProtocolFTPS;
			else if (!strcmp(v, "https"))
				protocol = kSecAttrProtocolHTTPS;
			else if (!strcmp(v, "http"))
				protocol = kSecAttrProtocolHTTP;
			else if (!strcmp(v, "smtp"))
				protocol = kSecAttrProtocolSMTP;
			else {
				/* we don't yet handle other protocols */
				clear_credential();
				exit(0);
			}
		}
		else if (!strcmp(buf, "host")) {
			char *colon = strchr(v, ':');
			if (colon) {
				UInt16 port_i;
				*colon++ = '\0';
				port_i = atoi(colon);
				port = CFNumberCreate(kCFAllocatorDefault,
						      kCFNumberShortType,
						      &port_i);
			}
			host = CFStringCreateWithCString(kCFAllocatorDefault,
							 v,
							 ENCODING);
		}
		else if (!strcmp(buf, "path"))
			path = CFStringCreateWithCString(kCFAllocatorDefault,
							 v,
							 ENCODING);
		else if (!strcmp(buf, "username"))
			username = CFStringCreateWithCString(
					kCFAllocatorDefault,
					v,
					ENCODING);
		else if (!strcmp(buf, "password"))
			password = CFDataCreate(kCFAllocatorDefault,
						(UInt8 *)v,
						strlen(v));
		else if (!strcmp(buf, "password_expiry_utc"))
			password_expiry_utc = CFDataCreate(kCFAllocatorDefault,
							   (UInt8 *)v,
							   strlen(v));
		else if (!strcmp(buf, "oauth_refresh_token"))
			oauth_refresh_token = CFDataCreate(kCFAllocatorDefault,
							   (UInt8 *)v,
							   strlen(v));
		else if (!strcmp(buf, "state[]")) {
			int len = strlen("osxkeychain:seen=");
			if (!strncmp(v, "osxkeychain:seen=", len))
				state_seen = xstrdup(v);
		}
		/*
		 * Ignore other lines; we don't know what they mean, but
		 * this future-proofs us when later versions of git do
		 * learn new lines, and the helpers are updated to match.
		 */
	}

	free(buf);
}

int main(int argc, const char **argv)
{
	OSStatus result = 0;
	const char *usage =
		"usage: git credential-osxkeychain <get|store|erase>";

	if (argc < 2 || !*argv[1])
		die("%s", usage);

	if (open(argv[0], O_RDONLY | O_EXLOCK) == -1)
		die("failed to lock %s", argv[0]);

	read_credential();

	if (!strcmp(argv[1], "get"))
		result = find_internet_password();
	else if (!strcmp(argv[1], "store"))
		result = add_internet_password();
	else if (!strcmp(argv[1], "erase"))
		result = delete_internet_password();
	/* otherwise, ignore unknown action */

	if (result)
		die("failed to %s: %d", argv[1], (int)result);

	clear_credential();

	if (state_seen)
		free(state_seen);

	return 0;
}
