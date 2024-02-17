#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Security/Security.h>

#define ENCODING kCFStringEncodingUTF8
static CFStringRef protocol; /* Stores constant strings - not memory managed */
static CFStringRef host;
static CFStringRef path;
static CFStringRef username;
static CFDataRef password;
static CFNumberRef port;

static void clear_credential(void)
{
	if (host) {
		CFRelease(host);
		host = NULL;
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
	if (port) {
		CFRelease(port);
		port = NULL;
	}
}

__attribute__((format (printf, 1, 2), __noreturn__))
static void die(const char *err, ...)
{
	char msg[4096];
	va_list params;
	va_start(params, err);
	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, "%s\n", msg);
	va_end(params);
	clear_credential();
	exit(1);
}

static void *xmalloc(size_t len)
{
	void *ret = malloc(len);
	if (!ret)
		die("Out of memory");
	return ret;
}

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
		write_item("username", username_buf, buffer_len - 1);
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

	write_item("password",
		   (const char *)CFDataGetBytePtr(data),
		   CFDataGetLength(data));
	if (!username)
		find_username_in_item(item);

	CFRelease(item);

out:
	CFRelease(attrs);

	/* We consider not found to not be an error */
	if (result == errSecItemNotFound)
		result = errSecSuccess;

	return result;
}

static OSStatus delete_internet_password(void)
{
	CFDictionaryRef attrs;
	OSStatus result;

	/*
	 * Require at least a protocol and host for removal, which is what git
	 * will give us; if you want to do something more fancy, use the
	 * Keychain manager.
	 */
	if (!protocol || !host)
		return -1;

	attrs = CREATE_SEC_ATTRIBUTES(kSecMatchLimit, kSecMatchLimitAll,
				      NULL);
	result = SecItemDelete(attrs);
	CFRelease(attrs);

	/* We consider not found to not be an error */
	if (result == errSecItemNotFound)
		result = errSecSuccess;

	return result;
}

static OSStatus add_internet_password(void)
{
	CFDictionaryRef attrs;
	OSStatus result;

	/* Only store complete credentials */
	if (!protocol || !host || !username || !password)
		return -1;

	attrs = CREATE_SEC_ATTRIBUTES(kSecValueData, password,
				      NULL);

	result = SecItemAdd(attrs, NULL);
	if (result == errSecDuplicateItem) {
		CFDictionaryRef query;
		query = CREATE_SEC_ATTRIBUTES(NULL);
		result = SecItemUpdate(query, attrs);
		CFRelease(query);
	}

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

	if (!argv[1])
		die("%s", usage);

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

	return 0;
}
