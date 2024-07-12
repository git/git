#include "../../git-compat-util.h"
#include "../../environment.h"
#include "../../wrapper.h"
#include "../../strbuf.h"
#include "../../versioncmp.h"

int win32_has_dos_drive_prefix(const char *path)
{
	int i;

	/*
	 * Does it start with an ASCII letter (i.e. highest bit not set),
	 * followed by a colon?
	 */
	if (!(0x80 & (unsigned char)*path))
		return *path && path[1] == ':' ? 2 : 0;

	/*
	 * While drive letters must be letters of the English alphabet, it is
	 * possible to assign virtually _any_ Unicode character via `subst` as
	 * a drive letter to "virtual drives". Even `1`, or `ä`. Or fun stuff
	 * like this:
	 *
	 *      subst ֍: %USERPROFILE%\Desktop
	 */
	for (i = 1; i < 4 && (0x80 & (unsigned char)path[i]); i++)
		; /* skip first UTF-8 character */
	return path[i] == ':' ? i + 1 : 0;
}

int win32_skip_dos_drive_prefix(char **path)
{
	int ret = has_dos_drive_prefix(*path);
	*path += ret;
	return ret;
}

int win32_offset_1st_component(const char *path)
{
	char *pos = (char *)path;

	/* unc paths */
	if (!skip_dos_drive_prefix(&pos) &&
			is_dir_sep(pos[0]) && is_dir_sep(pos[1])) {
		/* skip server name */
		pos = strpbrk(pos + 2, "\\/");
		if (!pos)
			return 0; /* Error: malformed unc path */

		do {
			pos++;
		} while (*pos && !is_dir_sep(*pos));
	}

	return pos + is_dir_sep(*pos) - path;
}

int win32_fspathncmp(const char *a, const char *b, size_t count)
{
	int diff;

	for (;;) {
		if (!count--)
			return 0;
		if (!*a)
			return *b ? -1 : 0;
		if (!*b)
			return +1;

		if (is_dir_sep(*a)) {
			if (!is_dir_sep(*b))
				return -1;
			a++;
			b++;
			continue;
		} else if (is_dir_sep(*b))
			return +1;

		diff = ignore_case ?
			(unsigned char)tolower(*a) - (int)(unsigned char)tolower(*b) :
			(unsigned char)*a - (int)(unsigned char)*b;
		if (diff)
			return diff;
		a++;
		b++;
	}
}

int win32_fspathcmp(const char *a, const char *b)
{
	return win32_fspathncmp(a, b, (size_t)-1);
}

static int read_at(int fd, char *buffer, size_t offset, size_t size)
{
	if (lseek(fd, offset, SEEK_SET) < 0) {
		fprintf(stderr, "could not seek to 0x%x\n", (unsigned int)offset);
		return -1;
	}

	return read_in_full(fd, buffer, size);
}

static size_t le16(const char *buffer)
{
	unsigned char *u = (unsigned char *)buffer;
	return u[0] | (u[1] << 8);
}

static size_t le32(const char *buffer)
{
	return le16(buffer) | (le16(buffer + 2) << 16);
}

/*
 * Determine the Go version of a given executable, if it was built with Go.
 *
 * This recapitulates the logic from
 * https://github.com/golang/go/blob/master/src/cmd/go/internal/version/version.go
 * (without requiring the user to install `go.exe` to find out).
 */
static ssize_t get_go_version(const char *path, char *go_version, size_t go_version_size)
{
	int fd = open(path, O_RDONLY);
	char buffer[1024];
	off_t offset;
	size_t num_sections, opt_header_size, i;
	char *p = NULL, *q;
	ssize_t res = -1;

	if (fd < 0)
		return -1;

	if (read_in_full(fd, buffer, 2) < 0)
		goto fail;

	/*
	 * Parse the PE file format, for more details, see
	 * https://en.wikipedia.org/wiki/Portable_Executable#Layout and
	 * https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
	 */
	if (buffer[0] != 'M' || buffer[1] != 'Z')
		goto fail;

	if (read_at(fd, buffer, 0x3c, 4) < 0)
		goto fail;

	/* Read the `PE\0\0` signature and the COFF file header */
	offset = le32(buffer);
	if (read_at(fd, buffer, offset, 24) < 0)
		goto fail;

	if (buffer[0] != 'P' || buffer[1] != 'E' || buffer[2] != '\0' || buffer[3] != '\0')
		goto fail;

	num_sections = le16(buffer + 6);
	opt_header_size = le16(buffer + 20);
	offset += 24; /* skip file header */

	/*
	 * Validate magic number 0x10b or 0x20b, for full details see
	 * https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#optional-header-standard-fields-image-only
	 */
	if (read_at(fd, buffer, offset, 2) < 0 ||
	    ((i = le16(buffer)) != 0x10b && i != 0x20b))
		goto fail;

	offset += opt_header_size;

	for (i = 0; i < num_sections; i++) {
		if (read_at(fd, buffer, offset + i * 40, 40) < 0)
			goto fail;

		/*
		 * For full details about the section headers, see
		 * https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#section-table-section-headers
		 */
		if ((le32(buffer + 36) /* characteristics */ & ~0x600000) /* IMAGE_SCN_ALIGN_32BYTES */ ==
		    (/* IMAGE_SCN_CNT_INITIALIZED_DATA */ 0x00000040 |
		     /* IMAGE_SCN_MEM_READ */ 0x40000000 |
		     /* IMAGE_SCN_MEM_WRITE */ 0x80000000)) {
			size_t size = le32(buffer + 16); /* "SizeOfRawData " */
			size_t pointer = le32(buffer + 20); /* "PointerToRawData " */

			/*
			 * Skip the section if either size or pointer is 0, see
			 * https://github.com/golang/go/blob/go1.21.0/src/debug/buildinfo/buildinfo.go#L333
			 * for full details.
			 *
			 * Merely seeing a non-zero size will not actually do,
			 * though: he size must be at least `buildInfoSize`,
			 * i.e. 32, and we expect a UVarint (at least another
			 * byte) _and_ the bytes representing the string,
			 * which we expect to start with the letters "go" and
			 * continue with the Go version number.
			 */
			if (size < 32 + 1 + 2 + 1 || !pointer)
				continue;

			p = malloc(size);

			if (!p || read_at(fd, p, pointer, size) < 0)
				goto fail;

			/*
			 * Look for the build information embedded by Go, see
			 * https://github.com/golang/go/blob/go1.21.0/src/debug/buildinfo/buildinfo.go#L165-L175
			 * for full details.
			 *
			 * Note: Go contains code to enforce alignment along a
			 * 16-byte boundary. In practice, no `.exe` has been
			 * observed that required any adjustment, therefore
			 * this here code skips that logic for simplicity.
			 */
			q = memmem(p, size - 18, "\xff Go buildinf:", 14);
			if (!q)
				goto fail;
			/*
			 * Decode the build blob. For full details, see
			 * https://github.com/golang/go/blob/go1.21.0/src/debug/buildinfo/buildinfo.go#L177-L191
			 *
			 * Note: The `endianness` values observed in practice
			 * were always 2, therefore the complex logic to handle
			 * any other value is skipped for simplicty.
			 */
			if ((q[14] == 8 || q[14] == 4) && q[15] == 2) {
				/*
				 * Only handle a Go version string with fewer
				 * than 128 characters, so the Go UVarint at
				 * q[32] that indicates the string's length must
				 * be only one byte (without the high bit set).
				 */
				if ((q[32] & 0x80) ||
				    !q[32] ||
				    (q + 33 + q[32] - p) > size ||
				    q[32] + 1 > go_version_size)
					goto fail;
				res = q[32];
				memcpy(go_version, q + 33, res);
				go_version[res] = '\0';
				break;
			}
		}
	}

fail:
	free(p);
	close(fd);
	return res;
}

void win32_warn_about_git_lfs_on_windows7(int exit_code, const char *argv0)
{
	char buffer[128], *git_lfs = NULL;
	const char *p;

	/*
	 * Git LFS v3.5.1 fails with an Access Violation on Windows 7; That
	 * would usually show up as an exit code 0xc0000005. For some reason
	 * (probably because at this point, we no longer have the _original_
	 * HANDLE that was returned by `CreateProcess()`) we observe other
	 * values like 0xb00 and 0x2 instead. Since the exact exit code
	 * seems to be inconsistent, we check for a non-zero exit status.
	 */
	if (exit_code == 0)
		return;
	if (GetVersion() >> 16 > 7601)
		return; /* Warn only on Windows 7 or older */
	if (!istarts_with(argv0, "git-lfs ") &&
	    strcasecmp(argv0, "git-lfs"))
		return;
	if (!(git_lfs = locate_in_PATH("git-lfs")))
		return;
	if (get_go_version(git_lfs, buffer, sizeof(buffer)) > 0 &&
	    skip_prefix(buffer, "go", &p) &&
	    versioncmp("1.21.0", p) <= 0)
		warning("This program was built with Go v%s\n"
			"i.e. without support for this Windows version:\n"
			"\n\t%s\n"
			"\n"
			"To work around this, you can download and install a "
			"working version from\n"
			"\n"
			"\thttps://github.com/git-lfs/git-lfs/releases/tag/"
			"v3.4.1\n",
			p, git_lfs);
	free(git_lfs);
}
