/*
 * Converts filenames from decomposed unicode into precomposed unicode.
 * Used on MacOS X.
 */

#define PRECOMPOSE_UNICODE_C
#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "path.h"
#include "strbuf.h"
#include "utf8.h"
#include "precompose_utf8.h"

typedef char *iconv_ibp;
static const char *repo_encoding = "UTF-8";
static const char *path_encoding = "UTF-8-MAC";

static size_t has_non_ascii(const char *s, size_t maxlen, size_t *strlen_c)
{
	const uint8_t *ptr = (const uint8_t *)s;
	size_t strlen_chars = 0;
	size_t ret = 0;

	if (!ptr || !*ptr)
		return 0;

	while (*ptr && maxlen) {
		if (*ptr & 0x80)
			ret++;
		strlen_chars++;
		ptr++;
		maxlen--;
	}
	if (strlen_c)
		*strlen_c = strlen_chars;

	return ret;
}


void probe_utf8_pathname_composition(void)
{
	struct strbuf path = STRBUF_INIT;
	static const char *auml_nfc = "\xc3\xa4";
	static const char *auml_nfd = "\x61\xcc\x88";
	int output_fd;
	if (precomposed_unicode != -1)
		return; /* We found it defined in the global config, respect it */
	git_path_buf(&path, "%s", auml_nfc);
	output_fd = open(path.buf, O_CREAT|O_EXCL|O_RDWR, 0600);
	if (output_fd >= 0) {
		close(output_fd);
		git_path_buf(&path, "%s", auml_nfd);
		precomposed_unicode = access(path.buf, R_OK) ? 0 : 1;
		git_config_set("core.precomposeunicode",
			       precomposed_unicode ? "true" : "false");
		git_path_buf(&path, "%s", auml_nfc);
		if (unlink(path.buf))
			die_errno(_("failed to unlink '%s'"), path.buf);
	}
	strbuf_release(&path);
}

const char *precompose_string_if_needed(const char *in)
{
	size_t inlen;
	size_t outlen;
	if (!in)
		return NULL;
	if (has_non_ascii(in, (size_t)-1, &inlen)) {
		iconv_t ic_prec;
		char *out;
		if (precomposed_unicode < 0)
			git_config_get_bool("core.precomposeunicode", &precomposed_unicode);
		if (precomposed_unicode != 1)
			return in;
		ic_prec = iconv_open(repo_encoding, path_encoding);
		if (ic_prec == (iconv_t) -1)
			return in;

		out = reencode_string_iconv(in, inlen, ic_prec, 0, &outlen);
		if (out) {
			if (outlen == inlen && !memcmp(in, out, outlen))
				free(out); /* no need to return indentical */
			else
				in = out;
		}
		iconv_close(ic_prec);

	}
	return in;
}

const char *precompose_argv_prefix(int argc, const char **argv, const char *prefix)
{
	int i = 0;

	while (i < argc) {
		argv[i] = precompose_string_if_needed(argv[i]);
		i++;
	}
	return precompose_string_if_needed(prefix);
}


PREC_DIR *precompose_utf8_opendir(const char *dirname)
{
	PREC_DIR *prec_dir = xmalloc(sizeof(PREC_DIR));
	prec_dir->dirent_nfc = xmalloc(sizeof(dirent_prec_psx));
	prec_dir->dirent_nfc->max_name_len = sizeof(prec_dir->dirent_nfc->d_name);

	prec_dir->dirp = opendir(dirname);
	if (!prec_dir->dirp) {
		free(prec_dir->dirent_nfc);
		free(prec_dir);
		return NULL;
	} else {
		int ret_errno = errno;
		prec_dir->ic_precompose = iconv_open(repo_encoding, path_encoding);
		/* if iconv_open() fails, die() in readdir() if needed */
		errno = ret_errno;
	}

	return prec_dir;
}

struct dirent_prec_psx *precompose_utf8_readdir(PREC_DIR *prec_dir)
{
	struct dirent *res;
	res = readdir(prec_dir->dirp);
	if (res) {
		size_t namelenz = strlen(res->d_name) + 1; /* \0 */
		size_t new_maxlen = namelenz;

		int ret_errno = errno;

		if (new_maxlen > prec_dir->dirent_nfc->max_name_len) {
			size_t new_len = sizeof(dirent_prec_psx) + new_maxlen -
				sizeof(prec_dir->dirent_nfc->d_name);

			prec_dir->dirent_nfc = xrealloc(prec_dir->dirent_nfc, new_len);
			prec_dir->dirent_nfc->max_name_len = new_maxlen;
		}

		prec_dir->dirent_nfc->d_ino  = res->d_ino;
		prec_dir->dirent_nfc->d_type = res->d_type;

		if ((precomposed_unicode == 1) && has_non_ascii(res->d_name, (size_t)-1, NULL)) {
			if (prec_dir->ic_precompose == (iconv_t)-1) {
				die("iconv_open(%s,%s) failed, but needed:\n"
						"    precomposed unicode is not supported.\n"
						"    If you want to use decomposed unicode, run\n"
						"    \"git config core.precomposeunicode false\"\n",
						repo_encoding, path_encoding);
			} else {
				iconv_ibp	cp = (iconv_ibp)res->d_name;
				size_t inleft = namelenz;
				char *outpos = &prec_dir->dirent_nfc->d_name[0];
				size_t outsz = prec_dir->dirent_nfc->max_name_len;
				errno = 0;
				iconv(prec_dir->ic_precompose, &cp, &inleft, &outpos, &outsz);
				if (errno || inleft) {
					/*
					 * iconv() failed and errno could be E2BIG, EILSEQ, EINVAL, EBADF
					 * MacOS X avoids illegal byte sequences.
					 * If they occur on a mounted drive (e.g. NFS) it is not worth to
					 * die() for that, but rather let the user see the original name
					*/
					namelenz = 0; /* trigger strlcpy */
				}
			}
		} else
			namelenz = 0;

		if (!namelenz)
			strlcpy(prec_dir->dirent_nfc->d_name, res->d_name,
							prec_dir->dirent_nfc->max_name_len);

		errno = ret_errno;
		return prec_dir->dirent_nfc;
	}
	return NULL;
}


int precompose_utf8_closedir(PREC_DIR *prec_dir)
{
	int ret_value;
	int ret_errno;
	ret_value = closedir(prec_dir->dirp);
	ret_errno = errno;
	if (prec_dir->ic_precompose != (iconv_t)-1)
		iconv_close(prec_dir->ic_precompose);
	free(prec_dir->dirent_nfc);
	free(prec_dir);
	errno = ret_errno;
	return ret_value;
}
