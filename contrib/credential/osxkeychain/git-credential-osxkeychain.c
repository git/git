#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Security/Security.h>

static SecProtocolType protocol;
static char *host;
static char *path;
static char *username;
static char *password;
static UInt16 port;

__attribute__((format (printf, 1, 2)))
static void die(const char *err, ...)
{
	char msg[4096];
	va_list params;
	va_start(params, err);
	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, "%s\n", msg);
	va_end(params);
	exit(1);
}

static void *xstrdup(const char *s1)
{
	void *ret = strdup(s1);
	if (!ret)
		die("Out of memory");
	return ret;
}

#define KEYCHAIN_ITEM(x) (x ? strlen(x) : 0), x
#define KEYCHAIN_ARGS \
	NULL, /* default keychain */ \
	KEYCHAIN_ITEM(host), \
	0, NULL, /* account domain */ \
	KEYCHAIN_ITEM(username), \
	KEYCHAIN_ITEM(path), \
	port, \
	protocol, \
	kSecAuthenticationTypeDefault

static void write_item(const char *what, const char *buf, int len)
{
	printf("%s=", what);
	fwrite(buf, 1, len, stdout);
	putchar('\n');
}

static void find_username_in_item(SecKeychainItemRef item)
{
	SecKeychainAttributeList list;
	SecKeychainAttribute attr;

	list.count = 1;
	list.attr = &attr;
	attr.tag = kSecAccountItemAttr;

	if (SecKeychainItemCopyContent(item, NULL, &list, NULL, NULL))
		return;

	write_item("username", attr.data, attr.length);
	SecKeychainItemFreeContent(&list, NULL);
}

static void find_internet_password(void)
{
	void *buf;
	UInt32 len;
	SecKeychainItemRef item;

	if (SecKeychainFindInternetPassword(KEYCHAIN_ARGS, &len, &buf, &item))
		return;

	write_item("password", buf, len);
	if (!username)
		find_username_in_item(item);

	SecKeychainItemFreeContent(NULL, buf);
}

static void delete_internet_password(void)
{
	SecKeychainItemRef item;

	/*
	 * Require at least a protocol and host for removal, which is what git
	 * will give us; if you want to do something more fancy, use the
	 * Keychain manager.
	 */
	if (!protocol || !host)
		return;

	if (SecKeychainFindInternetPassword(KEYCHAIN_ARGS, 0, NULL, &item))
		return;

	SecKeychainItemDelete(item);
}

static void add_internet_password(void)
{
	/* Only store complete credentials */
	if (!protocol || !host || !username || !password)
		return;

	if (SecKeychainAddInternetPassword(
	      KEYCHAIN_ARGS,
	      KEYCHAIN_ITEM(password),
	      NULL))
		return;
}

static void read_credential(void)
{
	char buf[1024];

	while (fgets(buf, sizeof(buf), stdin)) {
		char *v;

		if (!strcmp(buf, "\n"))
			break;
		buf[strlen(buf)-1] = '\0';

		v = strchr(buf, '=');
		if (!v)
			die("bad input: %s", buf);
		*v++ = '\0';

		if (!strcmp(buf, "protocol")) {
			if (!strcmp(v, "imap"))
				protocol = kSecProtocolTypeIMAP;
			else if (!strcmp(v, "imaps"))
				protocol = kSecProtocolTypeIMAPS;
			else if (!strcmp(v, "ftp"))
				protocol = kSecProtocolTypeFTP;
			else if (!strcmp(v, "ftps"))
				protocol = kSecProtocolTypeFTPS;
			else if (!strcmp(v, "https"))
				protocol = kSecProtocolTypeHTTPS;
			else if (!strcmp(v, "http"))
				protocol = kSecProtocolTypeHTTP;
			else if (!strcmp(v, "smtp"))
				protocol = kSecProtocolTypeSMTP;
			else /* we don't yet handle other protocols */
				exit(0);
		}
		else if (!strcmp(buf, "host")) {
			char *colon = strchr(v, ':');
			if (colon) {
				*colon++ = '\0';
				port = atoi(colon);
			}
			host = xstrdup(v);
		}
		else if (!strcmp(buf, "path"))
			path = xstrdup(v);
		else if (!strcmp(buf, "username"))
			username = xstrdup(v);
		else if (!strcmp(buf, "password"))
			password = xstrdup(v);
		/*
		 * Ignore other lines; we don't know what they mean, but
		 * this future-proofs us when later versions of git do
		 * learn new lines, and the helpers are updated to match.
		 */
	}
}

int main(int argc, const char **argv)
{
	const char *usage =
		"usage: git credential-osxkeychain <get|store|erase>";

	if (!argv[1])
		die("%s", usage);

	read_credential();

	if (!strcmp(argv[1], "get"))
		find_internet_password();
	else if (!strcmp(argv[1], "store"))
		add_internet_password();
	else if (!strcmp(argv[1], "erase"))
		delete_internet_password();
	/* otherwise, ignore unknown action */

	return 0;
}
