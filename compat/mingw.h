#include "mingw-posix.h"

struct config_context;
int mingw_core_config(const char *var, const char *value,
		      const struct config_context *ctx, void *cb);
#define platform_core_config mingw_core_config

#ifndef NO_OPENSSL
#include <openssl/ssl.h>
static inline int mingw_SSL_set_fd(SSL *ssl, int fd)
{
	return SSL_set_fd(ssl, _get_osfhandle(fd));
}
#define SSL_set_fd mingw_SSL_set_fd

static inline int mingw_SSL_set_rfd(SSL *ssl, int fd)
{
	return SSL_set_rfd(ssl, _get_osfhandle(fd));
}
#define SSL_set_rfd mingw_SSL_set_rfd

static inline int mingw_SSL_set_wfd(SSL *ssl, int fd)
{
	return SSL_set_wfd(ssl, _get_osfhandle(fd));
}
#define SSL_set_wfd mingw_SSL_set_wfd
#endif

/*
 * git specific compatibility
 */

static inline void convert_slashes(char *path)
{
	for (; *path; path++)
		if (*path == '\\')
			*path = '/';
}
#define PATH_SEP ';'
char *mingw_query_user_email(void);
#define query_user_email mingw_query_user_email

/**
 * Verifies that the specified path is owned by the user running the
 * current process.
 */
int is_path_owned_by_current_sid(const char *path, struct strbuf *report);
#define is_path_owned_by_current_user is_path_owned_by_current_sid

/**
 * Verifies that the given path is a valid one on Windows.
 *
 * In particular, path segments are disallowed which
 *
 * - end in a period or a space (except the special directories `.` and `..`).
 *
 * - contain any of the reserved characters, e.g. `:`, `;`, `*`, etc
 *
 * - correspond to reserved names (such as `AUX`, `PRN`, etc)
 *
 * The `allow_literal_nul` parameter controls whether the path `NUL` should
 * be considered valid (this makes sense e.g. before opening files, as it is
 * perfectly legitimate to open `NUL` on Windows, just as it is to open
 * `/dev/null` on Unix/Linux).
 *
 * Returns 1 upon success, otherwise 0.
 */
int is_valid_win32_path(const char *path, int allow_literal_nul);
#define is_valid_path(path) is_valid_win32_path(path, 0)

/**
 * Converts UTF-8 encoded string to UTF-16LE.
 *
 * To support repositories with legacy-encoded file names, invalid UTF-8 bytes
 * 0xa0 - 0xff are converted to corresponding printable Unicode chars \u00a0 -
 * \u00ff, and invalid UTF-8 bytes 0x80 - 0x9f (which would make non-printable
 * Unicode) are converted to hex-code.
 *
 * Lead-bytes not followed by an appropriate number of trail-bytes, over-long
 * encodings and 4-byte encodings > \u10ffff are detected as invalid UTF-8.
 *
 * Maximum space requirement for the target buffer is two wide chars per UTF-8
 * char (((strlen(utf) * 2) + 1) [* sizeof(wchar_t)]).
 *
 * The maximum space is needed only if the entire input string consists of
 * invalid UTF-8 bytes in range 0x80-0x9f, as per the following table:
 *
 *               |                   | UTF-8 | UTF-16 |
 *   Code point  |  UTF-8 sequence   | bytes | words  | ratio
 * --------------+-------------------+-------+--------+-------
 * 000000-00007f | 0-7f              |   1   |   1    |  1
 * 000080-0007ff | c2-df + 80-bf     |   2   |   1    |  0.5
 * 000800-00ffff | e0-ef + 2 * 80-bf |   3   |   1    |  0.33
 * 010000-10ffff | f0-f4 + 3 * 80-bf |   4   |  2 (a) |  0.5
 * invalid       | 80-9f             |   1   |  2 (b) |  2
 * invalid       | a0-ff             |   1   |   1    |  1
 *
 * (a) encoded as UTF-16 surrogate pair
 * (b) encoded as two hex digits
 *
 * Note that, while the UTF-8 encoding scheme can be extended to 5-byte, 6-byte
 * or even indefinite-byte sequences, the largest valid code point \u10ffff
 * encodes as only 4 UTF-8 bytes.
 *
 * Parameters:
 * wcs: wide char target buffer
 * utf: string to convert
 * wcslen: size of target buffer (in wchar_t's)
 * utflen: size of string to convert, or -1 if 0-terminated
 *
 * Returns:
 * length of converted string (_wcslen(wcs)), or -1 on failure
 *
 * Errors:
 * EINVAL: one of the input parameters is invalid (e.g. NULL)
 * ERANGE: the output buffer is too small
 */
int xutftowcsn(wchar_t *wcs, const char *utf, size_t wcslen, int utflen);

/**
 * Simplified variant of xutftowcsn, assumes input string is \0-terminated.
 */
static inline int xutftowcs(wchar_t *wcs, const char *utf, size_t wcslen)
{
	return xutftowcsn(wcs, utf, wcslen, -1);
}

/**
 * Simplified file system specific variant of xutftowcsn, assumes output
 * buffer size is MAX_PATH wide chars and input string is \0-terminated,
 * fails with ENAMETOOLONG if input string is too long.
 */
static inline int xutftowcs_path(wchar_t *wcs, const char *utf)
{
	int result = xutftowcsn(wcs, utf, MAX_PATH, -1);
	if (result < 0 && errno == ERANGE)
		errno = ENAMETOOLONG;
	return result;
}

/**
 * Converts UTF-16LE encoded string to UTF-8.
 *
 * Maximum space requirement for the target buffer is three UTF-8 chars per
 * wide char ((_wcslen(wcs) * 3) + 1).
 *
 * The maximum space is needed only if the entire input string consists of
 * UTF-16 words in range 0x0800-0xd7ff or 0xe000-0xffff (i.e. \u0800-\uffff
 * modulo surrogate pairs), as per the following table:
 *
 *               |                       | UTF-16 | UTF-8 |
 *   Code point  |  UTF-16 sequence      | words  | bytes | ratio
 * --------------+-----------------------+--------+-------+-------
 * 000000-00007f | 0000-007f             |   1    |   1   |  1
 * 000080-0007ff | 0080-07ff             |   1    |   2   |  2
 * 000800-00ffff | 0800-d7ff / e000-ffff |   1    |   3   |  3
 * 010000-10ffff | d800-dbff + dc00-dfff |   2    |   4   |  2
 *
 * Note that invalid code points > 10ffff cannot be represented in UTF-16.
 *
 * Parameters:
 * utf: target buffer
 * wcs: wide string to convert
 * utflen: size of target buffer
 *
 * Returns:
 * length of converted string, or -1 on failure
 *
 * Errors:
 * EINVAL: one of the input parameters is invalid (e.g. NULL)
 * ERANGE: the output buffer is too small
 */
int xwcstoutf(char *utf, const wchar_t *wcs, size_t utflen);

/*
 * A critical section used in the implementation of the spawn
 * functions (mingw_spawnv[p]e()) and waitpid(). Initialised in
 * the replacement main() macro below.
 */
extern CRITICAL_SECTION pinfo_cs;

/*
 * Git, like most portable C applications, implements a main() function. On
 * Windows, this main() function would receive parameters encoded in the
 * current locale, but Git for Windows would prefer UTF-8 encoded  parameters.
 *
 * To make that happen, we still declare main() here, and then declare and
 * implement wmain() (which is the Unicode variant of main()) and compile with
 * -municode. This wmain() function reencodes the parameters from UTF-16 to
 * UTF-8 format, sets up a couple of other things as required on Windows, and
 * then hands off to the main() function.
 */
int wmain(int argc, const wchar_t **w_argv);
int main(int argc, const char **argv);

/*
 * For debugging: if a problem occurs, say, in a Git process that is spawned
 * from another Git process which in turn is spawned from yet another Git
 * process, it can be quite daunting to figure out what is going on.
 *
 * Call this function to open a new MinTTY (this assumes you are in Git for
 * Windows' SDK) with a GDB that attaches to the current process right away.
 */
void open_in_gdb(void);

/*
 * Used by Pthread API implementation for Windows
 */
int err_win_to_posix(DWORD winerr);

#ifndef NO_UNIX_SOCKETS
int mingw_have_unix_sockets(void);
#undef have_unix_sockets
#define have_unix_sockets mingw_have_unix_sockets
#endif
