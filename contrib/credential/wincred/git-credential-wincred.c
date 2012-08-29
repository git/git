/*
 * A git credential helper that interface with Windows' Credential Manager
 *
 */
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>

/* common helpers */

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

static void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (!ret && !size)
		ret = malloc(1);
	if (!ret)
		 die("Out of memory");
	return ret;
}

static char *xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret)
		die("Out of memory");
	return ret;
}

/* MinGW doesn't have wincred.h, so we need to define stuff */

typedef struct _CREDENTIAL_ATTRIBUTEW {
	LPWSTR Keyword;
	DWORD  Flags;
	DWORD  ValueSize;
	LPBYTE Value;
} CREDENTIAL_ATTRIBUTEW, *PCREDENTIAL_ATTRIBUTEW;

typedef struct _CREDENTIALW {
	DWORD                  Flags;
	DWORD                  Type;
	LPWSTR                 TargetName;
	LPWSTR                 Comment;
	FILETIME               LastWritten;
	DWORD                  CredentialBlobSize;
	LPBYTE                 CredentialBlob;
	DWORD                  Persist;
	DWORD                  AttributeCount;
	PCREDENTIAL_ATTRIBUTEW Attributes;
	LPWSTR                 TargetAlias;
	LPWSTR                 UserName;
} CREDENTIALW, *PCREDENTIALW;

#define CRED_TYPE_GENERIC 1
#define CRED_PERSIST_LOCAL_MACHINE 2
#define CRED_MAX_ATTRIBUTES 64

typedef BOOL (WINAPI *CredWriteWT)(PCREDENTIALW, DWORD);
typedef BOOL (WINAPI *CredUnPackAuthenticationBufferWT)(DWORD, PVOID, DWORD,
    LPWSTR, DWORD *, LPWSTR, DWORD *, LPWSTR, DWORD *);
typedef BOOL (WINAPI *CredEnumerateWT)(LPCWSTR, DWORD, DWORD *,
    PCREDENTIALW **);
typedef BOOL (WINAPI *CredPackAuthenticationBufferWT)(DWORD, LPWSTR, LPWSTR,
    PBYTE, DWORD *);
typedef VOID (WINAPI *CredFreeT)(PVOID);
typedef BOOL (WINAPI *CredDeleteWT)(LPCWSTR, DWORD, DWORD);

static HMODULE advapi, credui;
static CredWriteWT CredWriteW;
static CredUnPackAuthenticationBufferWT CredUnPackAuthenticationBufferW;
static CredEnumerateWT CredEnumerateW;
static CredPackAuthenticationBufferWT CredPackAuthenticationBufferW;
static CredFreeT CredFree;
static CredDeleteWT CredDeleteW;

static void load_cred_funcs(void)
{
	/* load DLLs */
	advapi = LoadLibrary("advapi32.dll");
	credui = LoadLibrary("credui.dll");
	if (!advapi || !credui)
		die("failed to load DLLs");

	/* get function pointers */
	CredWriteW = (CredWriteWT)GetProcAddress(advapi, "CredWriteW");
	CredUnPackAuthenticationBufferW = (CredUnPackAuthenticationBufferWT)
	    GetProcAddress(credui, "CredUnPackAuthenticationBufferW");
	CredEnumerateW = (CredEnumerateWT)GetProcAddress(advapi,
	    "CredEnumerateW");
	CredPackAuthenticationBufferW = (CredPackAuthenticationBufferWT)
	    GetProcAddress(credui, "CredPackAuthenticationBufferW");
	CredFree = (CredFreeT)GetProcAddress(advapi, "CredFree");
	CredDeleteW = (CredDeleteWT)GetProcAddress(advapi, "CredDeleteW");
	if (!CredWriteW || !CredUnPackAuthenticationBufferW ||
	    !CredEnumerateW || !CredPackAuthenticationBufferW || !CredFree ||
	    !CredDeleteW)
		die("failed to load functions");
}

static char target_buf[1024];
static char *protocol, *host, *path, *username;
static WCHAR *wusername, *password, *target;

static void write_item(const char *what, WCHAR *wbuf)
{
	char *buf;
	int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL,
	    FALSE);
	buf = xmalloc(len);

	if (!WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, len, NULL, FALSE))
		die("WideCharToMultiByte failed!");

	printf("%s=", what);
	fwrite(buf, 1, len - 1, stdout);
	putchar('\n');
	free(buf);
}

static int match_attr(const CREDENTIALW *cred, const WCHAR *keyword,
    const char *want)
{
	int i;
	if (!want)
		return 1;

	for (i = 0; i < cred->AttributeCount; ++i)
		if (!wcscmp(cred->Attributes[i].Keyword, keyword))
			return !strcmp((const char *)cred->Attributes[i].Value,
			    want);

	return 0; /* not found */
}

static int match_cred(const CREDENTIALW *cred)
{
	return (!wusername || !wcscmp(wusername, cred->UserName)) &&
	    match_attr(cred, L"git_protocol", protocol) &&
	    match_attr(cred, L"git_host", host) &&
	    match_attr(cred, L"git_path", path);
}

static void get_credential(void)
{
	WCHAR *user_buf, *pass_buf;
	DWORD user_buf_size = 0, pass_buf_size = 0;
	CREDENTIALW **creds, *cred = NULL;
	DWORD num_creds;
	int i;

	if (!CredEnumerateW(L"git:*", 0, &num_creds, &creds))
		return;

	/* search for the first credential that matches username */
	for (i = 0; i < num_creds; ++i)
		if (match_cred(creds[i])) {
			cred = creds[i];
			break;
		}
	if (!cred)
		return;

	CredUnPackAuthenticationBufferW(0, cred->CredentialBlob,
	    cred->CredentialBlobSize, NULL, &user_buf_size, NULL, NULL,
	    NULL, &pass_buf_size);

	user_buf = xmalloc(user_buf_size * sizeof(WCHAR));
	pass_buf = xmalloc(pass_buf_size * sizeof(WCHAR));

	if (!CredUnPackAuthenticationBufferW(0, cred->CredentialBlob,
	    cred->CredentialBlobSize, user_buf, &user_buf_size, NULL, NULL,
	    pass_buf, &pass_buf_size))
		die("CredUnPackAuthenticationBuffer failed");

	CredFree(creds);

	/* zero-terminate (sizes include zero-termination) */
	user_buf[user_buf_size - 1] = L'\0';
	pass_buf[pass_buf_size - 1] = L'\0';

	write_item("username", user_buf);
	write_item("password", pass_buf);

	free(user_buf);
	free(pass_buf);
}

static void write_attr(CREDENTIAL_ATTRIBUTEW *attr, const WCHAR *keyword,
    const char *value)
{
	attr->Keyword = (LPWSTR)keyword;
	attr->Flags = 0;
	attr->ValueSize = strlen(value) + 1; /* store zero-termination */
	attr->Value = (LPBYTE)value;
}

static void store_credential(void)
{
	CREDENTIALW cred;
	BYTE *auth_buf;
	DWORD auth_buf_size = 0;
	CREDENTIAL_ATTRIBUTEW attrs[CRED_MAX_ATTRIBUTES];

	if (!wusername || !password)
		return;

	/* query buffer size */
	CredPackAuthenticationBufferW(0, wusername, password,
	    NULL, &auth_buf_size);

	auth_buf = xmalloc(auth_buf_size);

	if (!CredPackAuthenticationBufferW(0, wusername, password,
	    auth_buf, &auth_buf_size))
		die("CredPackAuthenticationBuffer failed");

	cred.Flags = 0;
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = target;
	cred.Comment = L"saved by git-credential-wincred";
	cred.CredentialBlobSize = auth_buf_size;
	cred.CredentialBlob = auth_buf;
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
	cred.AttributeCount = 1;
	cred.Attributes = attrs;
	cred.TargetAlias = NULL;
	cred.UserName = wusername;

	write_attr(attrs, L"git_protocol", protocol);

	if (host) {
		write_attr(attrs + cred.AttributeCount, L"git_host", host);
		cred.AttributeCount++;
	}

	if (path) {
		write_attr(attrs + cred.AttributeCount, L"git_path", path);
		cred.AttributeCount++;
	}

	if (!CredWriteW(&cred, 0))
		die("CredWrite failed");
}

static void erase_credential(void)
{
	CREDENTIALW **creds;
	DWORD num_creds;
	int i;

	if (!CredEnumerateW(L"git:*", 0, &num_creds, &creds))
		return;

	for (i = 0; i < num_creds; ++i) {
		if (match_cred(creds[i]))
			CredDeleteW(creds[i]->TargetName, creds[i]->Type, 0);
	}

	CredFree(creds);
}

static WCHAR *utf8_to_utf16_dup(const char *str)
{
	int wlen = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	WCHAR *wstr = xmalloc(sizeof(WCHAR) * wlen);
	MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, wlen);
	return wstr;
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

		if (!strcmp(buf, "protocol"))
			protocol = xstrdup(v);
		else if (!strcmp(buf, "host"))
			host = xstrdup(v);
		else if (!strcmp(buf, "path"))
			path = xstrdup(v);
		else if (!strcmp(buf, "username")) {
			username = xstrdup(v);
			wusername = utf8_to_utf16_dup(v);
		} else if (!strcmp(buf, "password"))
			password = utf8_to_utf16_dup(v);
		else
			die("unrecognized input");
	}
}

int main(int argc, char *argv[])
{
	const char *usage =
	    "Usage: git credential-wincred <get|store|erase>\n";

	if (!argv[1])
		die(usage);

	/* git use binary pipes to avoid CRLF-issues */
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);

	read_credential();

	load_cred_funcs();

	if (!protocol || !(host || path))
		return 0;

	/* prepare 'target', the unique key for the credential */
	strncat(target_buf, "git:", sizeof(target_buf));
	strncat(target_buf, protocol, sizeof(target_buf));
	strncat(target_buf, "://", sizeof(target_buf));
	if (username) {
		strncat(target_buf, username, sizeof(target_buf));
		strncat(target_buf, "@", sizeof(target_buf));
	}
	if (host)
		strncat(target_buf, host, sizeof(target_buf));
	if (path) {
		strncat(target_buf, "/", sizeof(target_buf));
		strncat(target_buf, path, sizeof(target_buf));
	}

	target = utf8_to_utf16_dup(target_buf);

	if (!strcmp(argv[1], "get"))
		get_credential();
	else if (!strcmp(argv[1], "store"))
		store_credential();
	else if (!strcmp(argv[1], "erase"))
		erase_credential();
	/* otherwise, ignore unknown action */
	return 0;
}
