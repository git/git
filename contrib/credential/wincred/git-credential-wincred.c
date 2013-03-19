/*
 * A git credential helper that interface with Windows' Credential Manager
 *
 */
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>

/* common helpers */

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

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
typedef BOOL (WINAPI *CredEnumerateWT)(LPCWSTR, DWORD, DWORD *,
    PCREDENTIALW **);
typedef VOID (WINAPI *CredFreeT)(PVOID);
typedef BOOL (WINAPI *CredDeleteWT)(LPCWSTR, DWORD, DWORD);

static HMODULE advapi;
static CredWriteWT CredWriteW;
static CredEnumerateWT CredEnumerateW;
static CredFreeT CredFree;
static CredDeleteWT CredDeleteW;

static void load_cred_funcs(void)
{
	/* load DLLs */
	advapi = LoadLibrary("advapi32.dll");
	if (!advapi)
		die("failed to load advapi32.dll");

	/* get function pointers */
	CredWriteW = (CredWriteWT)GetProcAddress(advapi, "CredWriteW");
	CredEnumerateW = (CredEnumerateWT)GetProcAddress(advapi,
	    "CredEnumerateW");
	CredFree = (CredFreeT)GetProcAddress(advapi, "CredFree");
	CredDeleteW = (CredDeleteWT)GetProcAddress(advapi, "CredDeleteW");
	if (!CredWriteW || !CredEnumerateW || !CredFree || !CredDeleteW)
		die("failed to load functions");
}

static WCHAR *wusername, *password, *protocol, *host, *path, target[1024];

static void write_item(const char *what, LPCWSTR wbuf, int wlen)
{
	char *buf;
	int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, NULL, 0, NULL,
	    FALSE);
	buf = xmalloc(len);

	if (!WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, buf, len, NULL, FALSE))
		die("WideCharToMultiByte failed!");

	printf("%s=", what);
	fwrite(buf, 1, len, stdout);
	putchar('\n');
	free(buf);
}

/*
 * Match an (optional) expected string and a delimiter in the target string,
 * consuming the matched text by updating the target pointer.
 */
static int match_part(LPCWSTR *ptarget, LPCWSTR want, LPCWSTR delim)
{
	LPCWSTR delim_pos, start = *ptarget;
	int len;

	/* find start of delimiter (or end-of-string if delim is empty) */
	if (*delim)
		delim_pos = wcsstr(start, delim);
	else
		delim_pos = start + wcslen(start);

	/*
	 * match text up to delimiter, or end of string (e.g. the '/' after
	 * host is optional if not followed by a path)
	 */
	if (delim_pos)
		len = delim_pos - start;
	else
		len = wcslen(start);

	/* update ptarget if we either found a delimiter or need a match */
	if (delim_pos || want)
		*ptarget = delim_pos ? delim_pos + wcslen(delim) : start + len;

	return !want || (!wcsncmp(want, start, len) && !want[len]);
}

static int match_cred(const CREDENTIALW *cred)
{
	LPCWSTR target = cred->TargetName;
	if (wusername && wcscmp(wusername, cred->UserName))
		return 0;

	return match_part(&target, L"git", L":") &&
		match_part(&target, protocol, L"://") &&
		match_part(&target, wusername, L"@") &&
		match_part(&target, host, L"/") &&
		match_part(&target, path, L"");
}

static void get_credential(void)
{
	CREDENTIALW **creds;
	DWORD num_creds;
	int i;

	if (!CredEnumerateW(L"git:*", 0, &num_creds, &creds))
		return;

	/* search for the first credential that matches username */
	for (i = 0; i < num_creds; ++i)
		if (match_cred(creds[i])) {
			write_item("username", creds[i]->UserName,
				wcslen(creds[i]->UserName));
			write_item("password",
				(LPCWSTR)creds[i]->CredentialBlob,
				creds[i]->CredentialBlobSize / sizeof(WCHAR));
			break;
		}

	CredFree(creds);
}

static void store_credential(void)
{
	CREDENTIALW cred;

	if (!wusername || !password)
		return;

	cred.Flags = 0;
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = target;
	cred.Comment = L"saved by git-credential-wincred";
	cred.CredentialBlobSize = (wcslen(password)) * sizeof(WCHAR);
	cred.CredentialBlob = (LPVOID)password;
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
	cred.AttributeCount = 0;
	cred.Attributes = NULL;
	cred.TargetAlias = NULL;
	cred.UserName = wusername;

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
		int len = strlen(buf);
		/* strip trailing CR / LF */
		while (len && strchr("\r\n", buf[len - 1]))
			buf[--len] = 0;

		if (!*buf)
			break;

		v = strchr(buf, '=');
		if (!v)
			die("bad input: %s", buf);
		*v++ = '\0';

		if (!strcmp(buf, "protocol"))
			protocol = utf8_to_utf16_dup(v);
		else if (!strcmp(buf, "host"))
			host = utf8_to_utf16_dup(v);
		else if (!strcmp(buf, "path"))
			path = utf8_to_utf16_dup(v);
		else if (!strcmp(buf, "username")) {
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
	    "usage: git credential-wincred <get|store|erase>\n";

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
	wcscpy(target, L"git:");
	wcsncat(target, protocol, ARRAY_SIZE(target));
	wcsncat(target, L"://", ARRAY_SIZE(target));
	if (wusername) {
		wcsncat(target, wusername, ARRAY_SIZE(target));
		wcsncat(target, L"@", ARRAY_SIZE(target));
	}
	if (host)
		wcsncat(target, host, ARRAY_SIZE(target));
	if (path) {
		wcsncat(target, L"/", ARRAY_SIZE(target));
		wcsncat(target, path, ARRAY_SIZE(target));
	}

	if (!strcmp(argv[1], "get"))
		get_credential();
	else if (!strcmp(argv[1], "store"))
		store_credential();
	else if (!strcmp(argv[1], "erase"))
		erase_credential();
	/* otherwise, ignore unknown action */
	return 0;
}
