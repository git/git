/*
 * I'm tired of doing "vsnprintf()" etc just to open a
 * file, so here's a "return static buffer with printf"
 * interface for paths.
 *
 * It's obviously not thread-safe. Sue me. But it's quite
 * useful for doing things like
 *
 *   f = open(mkpath("%s/%s.git", base, name), O_RDONLY);
 *
 * which is what it's designed for.
 */
#include "cache.h"
#include "strbuf.h"
#include "string-list.h"

static char bad_path[] = "/bad-path/";

static char *get_pathname(void)
{
	static char pathname_array[4][PATH_MAX];
	static int index;
	return pathname_array[3 & ++index];
}

static char *cleanup_path(char *path)
{
	/* Clean it up */
	if (!memcmp(path, "./", 2)) {
		path += 2;
		while (*path == '/')
			path++;
	}
	return path;
}

char *mksnpath(char *buf, size_t n, const char *fmt, ...)
{
	va_list args;
	unsigned len;

	va_start(args, fmt);
	len = vsnprintf(buf, n, fmt, args);
	va_end(args);
	if (len >= n) {
		strlcpy(buf, bad_path, n);
		return buf;
	}
	return cleanup_path(buf);
}

static char *vsnpath(char *buf, size_t n, const char *fmt, va_list args)
{
	const char *git_dir = get_git_dir();
	size_t len;

	len = strlen(git_dir);
	if (n < len + 1)
		goto bad;
	memcpy(buf, git_dir, len);
	if (len && !is_dir_sep(git_dir[len-1]))
		buf[len++] = '/';
	len += vsnprintf(buf + len, n - len, fmt, args);
	if (len >= n)
		goto bad;
	return cleanup_path(buf);
bad:
	strlcpy(buf, bad_path, n);
	return buf;
}

char *git_snpath(char *buf, size_t n, const char *fmt, ...)
{
	char *ret;
	va_list args;
	va_start(args, fmt);
	ret = vsnpath(buf, n, fmt, args);
	va_end(args);
	return ret;
}

char *git_pathdup(const char *fmt, ...)
{
	char path[PATH_MAX], *ret;
	va_list args;
	va_start(args, fmt);
	ret = vsnpath(path, sizeof(path), fmt, args);
	va_end(args);
	return xstrdup(ret);
}

char *mkpathdup(const char *fmt, ...)
{
	char *path;
	struct strbuf sb = STRBUF_INIT;
	va_list args;

	va_start(args, fmt);
	strbuf_vaddf(&sb, fmt, args);
	va_end(args);
	path = xstrdup(cleanup_path(sb.buf));

	strbuf_release(&sb);
	return path;
}

char *mkpath(const char *fmt, ...)
{
	va_list args;
	unsigned len;
	char *pathname = get_pathname();

	va_start(args, fmt);
	len = vsnprintf(pathname, PATH_MAX, fmt, args);
	va_end(args);
	if (len >= PATH_MAX)
		return bad_path;
	return cleanup_path(pathname);
}

char *git_path(const char *fmt, ...)
{
	char *pathname = get_pathname();
	va_list args;
	char *ret;

	va_start(args, fmt);
	ret = vsnpath(pathname, PATH_MAX, fmt, args);
	va_end(args);
	return ret;
}

void home_config_paths(char **global, char **xdg, char *file)
{
	char *xdg_home = getenv("XDG_CONFIG_HOME");
	char *home = getenv("HOME");
	char *to_free = NULL;

	if (!home) {
		if (global)
			*global = NULL;
	} else {
		if (!xdg_home) {
			to_free = mkpathdup("%s/.config", home);
			xdg_home = to_free;
		}
		if (global)
			*global = mkpathdup("%s/.gitconfig", home);
	}

	if (!xdg_home)
		*xdg = NULL;
	else
		*xdg = mkpathdup("%s/git/%s", xdg_home, file);

	free(to_free);
}

char *git_path_submodule(const char *path, const char *fmt, ...)
{
	char *pathname = get_pathname();
	struct strbuf buf = STRBUF_INIT;
	const char *git_dir;
	va_list args;
	unsigned len;

	len = strlen(path);
	if (len > PATH_MAX-100)
		return bad_path;

	strbuf_addstr(&buf, path);
	if (len && path[len-1] != '/')
		strbuf_addch(&buf, '/');
	strbuf_addstr(&buf, ".git");

	git_dir = read_gitfile(buf.buf);
	if (git_dir) {
		strbuf_reset(&buf);
		strbuf_addstr(&buf, git_dir);
	}
	strbuf_addch(&buf, '/');

	if (buf.len >= PATH_MAX)
		return bad_path;
	memcpy(pathname, buf.buf, buf.len + 1);

	strbuf_release(&buf);
	len = strlen(pathname);

	va_start(args, fmt);
	len += vsnprintf(pathname + len, PATH_MAX - len, fmt, args);
	va_end(args);
	if (len >= PATH_MAX)
		return bad_path;
	return cleanup_path(pathname);
}

int validate_headref(const char *path)
{
	struct stat st;
	char *buf, buffer[256];
	unsigned char sha1[20];
	int fd;
	ssize_t len;

	if (lstat(path, &st) < 0)
		return -1;

	/* Make sure it is a "refs/.." symlink */
	if (S_ISLNK(st.st_mode)) {
		len = readlink(path, buffer, sizeof(buffer)-1);
		if (len >= 5 && !memcmp("refs/", buffer, 5))
			return 0;
		return -1;
	}

	/*
	 * Anything else, just open it and try to see if it is a symbolic ref.
	 */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	len = read_in_full(fd, buffer, sizeof(buffer)-1);
	close(fd);

	/*
	 * Is it a symbolic ref?
	 */
	if (len < 4)
		return -1;
	if (!memcmp("ref:", buffer, 4)) {
		buf = buffer + 4;
		len -= 4;
		while (len && isspace(*buf))
			buf++, len--;
		if (len >= 5 && !memcmp("refs/", buf, 5))
			return 0;
	}

	/*
	 * Is this a detached HEAD?
	 */
	if (!get_sha1_hex(buffer, sha1))
		return 0;

	return -1;
}

static struct passwd *getpw_str(const char *username, size_t len)
{
	struct passwd *pw;
	char *username_z = xmalloc(len + 1);
	memcpy(username_z, username, len);
	username_z[len] = '\0';
	pw = getpwnam(username_z);
	free(username_z);
	return pw;
}

/*
 * Return a string with ~ and ~user expanded via getpw*.  If buf != NULL,
 * then it is a newly allocated string. Returns NULL on getpw failure or
 * if path is NULL.
 */
char *expand_user_path(const char *path)
{
	struct strbuf user_path = STRBUF_INIT;
	const char *first_slash = strchrnul(path, '/');
	const char *to_copy = path;

	if (path == NULL)
		goto return_null;
	if (path[0] == '~') {
		const char *username = path + 1;
		size_t username_len = first_slash - username;
		if (username_len == 0) {
			const char *home = getenv("HOME");
			if (!home)
				goto return_null;
			strbuf_add(&user_path, home, strlen(home));
		} else {
			struct passwd *pw = getpw_str(username, username_len);
			if (!pw)
				goto return_null;
			strbuf_add(&user_path, pw->pw_dir, strlen(pw->pw_dir));
		}
		to_copy = first_slash;
	}
	strbuf_add(&user_path, to_copy, strlen(to_copy));
	return strbuf_detach(&user_path, NULL);
return_null:
	strbuf_release(&user_path);
	return NULL;
}

/*
 * First, one directory to try is determined by the following algorithm.
 *
 * (0) If "strict" is given, the path is used as given and no DWIM is
 *     done. Otherwise:
 * (1) "~/path" to mean path under the running user's home directory;
 * (2) "~user/path" to mean path under named user's home directory;
 * (3) "relative/path" to mean cwd relative directory; or
 * (4) "/absolute/path" to mean absolute directory.
 *
 * Unless "strict" is given, we try access() for existence of "%s.git/.git",
 * "%s/.git", "%s.git", "%s" in this order.  The first one that exists is
 * what we try.
 *
 * Second, we try chdir() to that.  Upon failure, we return NULL.
 *
 * Then, we try if the current directory is a valid git repository.
 * Upon failure, we return NULL.
 *
 * If all goes well, we return the directory we used to chdir() (but
 * before ~user is expanded), avoiding getcwd() resolving symbolic
 * links.  User relative paths are also returned as they are given,
 * except DWIM suffixing.
 */
const char *enter_repo(const char *path, int strict)
{
	static char used_path[PATH_MAX];
	static char validated_path[PATH_MAX];

	if (!path)
		return NULL;

	if (!strict) {
		static const char *suffix[] = {
			"/.git", "", ".git/.git", ".git", NULL,
		};
		const char *gitfile;
		int len = strlen(path);
		int i;
		while ((1 < len) && (path[len-1] == '/'))
			len--;

		if (PATH_MAX <= len)
			return NULL;
		strncpy(used_path, path, len); used_path[len] = 0 ;
		strcpy(validated_path, used_path);

		if (used_path[0] == '~') {
			char *newpath = expand_user_path(used_path);
			if (!newpath || (PATH_MAX - 10 < strlen(newpath))) {
				free(newpath);
				return NULL;
			}
			/*
			 * Copy back into the static buffer. A pity
			 * since newpath was not bounded, but other
			 * branches of the if are limited by PATH_MAX
			 * anyway.
			 */
			strcpy(used_path, newpath); free(newpath);
		}
		else if (PATH_MAX - 10 < len)
			return NULL;
		len = strlen(used_path);
		for (i = 0; suffix[i]; i++) {
			struct stat st;
			strcpy(used_path + len, suffix[i]);
			if (!stat(used_path, &st) &&
			    (S_ISREG(st.st_mode) ||
			    (S_ISDIR(st.st_mode) && is_git_directory(used_path)))) {
				strcat(validated_path, suffix[i]);
				break;
			}
		}
		if (!suffix[i])
			return NULL;
		gitfile = read_gitfile(used_path) ;
		if (gitfile)
			strcpy(used_path, gitfile);
		if (chdir(used_path))
			return NULL;
		path = validated_path;
	}
	else if (chdir(path))
		return NULL;

	if (access("objects", X_OK) == 0 && access("refs", X_OK) == 0 &&
	    validate_headref("HEAD") == 0) {
		set_git_dir(".");
		check_repository_format();
		return path;
	}

	return NULL;
}

int set_shared_perm(const char *path, int mode)
{
	struct stat st;
	int tweak, shared, orig_mode;

	if (!shared_repository) {
		if (mode)
			return chmod(path, mode & ~S_IFMT);
		return 0;
	}
	if (!mode) {
		if (lstat(path, &st) < 0)
			return -1;
		mode = st.st_mode;
		orig_mode = mode;
	} else
		orig_mode = 0;
	if (shared_repository < 0)
		shared = -shared_repository;
	else
		shared = shared_repository;
	tweak = shared;

	if (!(mode & S_IWUSR))
		tweak &= ~0222;
	if (mode & S_IXUSR)
		/* Copy read bits to execute bits */
		tweak |= (tweak & 0444) >> 2;
	if (shared_repository < 0)
		mode = (mode & ~0777) | tweak;
	else
		mode |= tweak;

	if (S_ISDIR(mode)) {
		/* Copy read bits to execute bits */
		mode |= (shared & 0444) >> 2;
		mode |= FORCE_DIR_SET_GID;
	}

	if (((shared_repository < 0
	      ? (orig_mode & (FORCE_DIR_SET_GID | 0777))
	      : (orig_mode & mode)) != mode) &&
	    chmod(path, (mode & ~S_IFMT)) < 0)
		return -2;
	return 0;
}

const char *relative_path(const char *abs, const char *base)
{
	static char buf[PATH_MAX + 1];
	int i = 0, j = 0;

	if (!base || !base[0])
		return abs;
	while (base[i]) {
		if (is_dir_sep(base[i])) {
			if (!is_dir_sep(abs[j]))
				return abs;
			while (is_dir_sep(base[i]))
				i++;
			while (is_dir_sep(abs[j]))
				j++;
			continue;
		} else if (abs[j] != base[i]) {
			return abs;
		}
		i++;
		j++;
	}
	if (
	    /* "/foo" is a prefix of "/foo" */
	    abs[j] &&
	    /* "/foo" is not a prefix of "/foobar" */
	    !is_dir_sep(base[i-1]) && !is_dir_sep(abs[j])
	   )
		return abs;
	while (is_dir_sep(abs[j]))
		j++;
	if (!abs[j])
		strcpy(buf, ".");
	else
		strcpy(buf, abs + j);
	return buf;
}

/*
 * It is okay if dst == src, but they should not overlap otherwise.
 *
 * Performs the following normalizations on src, storing the result in dst:
 * - Ensures that components are separated by '/' (Windows only)
 * - Squashes sequences of '/'.
 * - Removes "." components.
 * - Removes ".." components, and the components the precede them.
 * Returns failure (non-zero) if a ".." component appears as first path
 * component anytime during the normalization. Otherwise, returns success (0).
 *
 * Note that this function is purely textual.  It does not follow symlinks,
 * verify the existence of the path, or make any system calls.
 */
int normalize_path_copy(char *dst, const char *src)
{
	char *dst0;

	if (has_dos_drive_prefix(src)) {
		*dst++ = *src++;
		*dst++ = *src++;
	}
	dst0 = dst;

	if (is_dir_sep(*src)) {
		*dst++ = '/';
		while (is_dir_sep(*src))
			src++;
	}

	for (;;) {
		char c = *src;

		/*
		 * A path component that begins with . could be
		 * special:
		 * (1) "." and ends   -- ignore and terminate.
		 * (2) "./"           -- ignore them, eat slash and continue.
		 * (3) ".." and ends  -- strip one and terminate.
		 * (4) "../"          -- strip one, eat slash and continue.
		 */
		if (c == '.') {
			if (!src[1]) {
				/* (1) */
				src++;
			} else if (is_dir_sep(src[1])) {
				/* (2) */
				src += 2;
				while (is_dir_sep(*src))
					src++;
				continue;
			} else if (src[1] == '.') {
				if (!src[2]) {
					/* (3) */
					src += 2;
					goto up_one;
				} else if (is_dir_sep(src[2])) {
					/* (4) */
					src += 3;
					while (is_dir_sep(*src))
						src++;
					goto up_one;
				}
			}
		}

		/* copy up to the next '/', and eat all '/' */
		while ((c = *src++) != '\0' && !is_dir_sep(c))
			*dst++ = c;
		if (is_dir_sep(c)) {
			*dst++ = '/';
			while (is_dir_sep(c))
				c = *src++;
			src--;
		} else if (!c)
			break;
		continue;

	up_one:
		/*
		 * dst0..dst is prefix portion, and dst[-1] is '/';
		 * go up one level.
		 */
		dst--;	/* go to trailing '/' */
		if (dst <= dst0)
			return -1;
		/* Windows: dst[-1] cannot be backslash anymore */
		while (dst0 < dst && dst[-1] != '/')
			dst--;
	}
	*dst = '\0';
	return 0;
}

/*
 * path = Canonical absolute path
 * prefixes = string_list containing normalized, absolute paths without
 * trailing slashes (except for the root directory, which is denoted by "/").
 *
 * Determines, for each path in prefixes, whether the "prefix"
 * is an ancestor directory of path.  Returns the length of the longest
 * ancestor directory, excluding any trailing slashes, or -1 if no prefix
 * is an ancestor.  (Note that this means 0 is returned if prefixes is
 * ["/"].) "/foo" is not considered an ancestor of "/foobar".  Directories
 * are not considered to be their own ancestors.  path must be in a
 * canonical form: empty components, or "." or ".." components are not
 * allowed.
 */
int longest_ancestor_length(const char *path, struct string_list *prefixes)
{
	int i, max_len = -1;

	if (!strcmp(path, "/"))
		return -1;

	for (i = 0; i < prefixes->nr; i++) {
		const char *ceil = prefixes->items[i].string;
		int len = strlen(ceil);

		if (len == 1 && ceil[0] == '/')
			len = 0; /* root matches anything, with length 0 */
		else if (!strncmp(path, ceil, len) && path[len] == '/')
			; /* match of length len */
		else
			continue; /* no match */

		if (len > max_len)
			max_len = len;
	}

	return max_len;
}

/* strip arbitrary amount of directory separators at end of path */
static inline int chomp_trailing_dir_sep(const char *path, int len)
{
	while (len && is_dir_sep(path[len - 1]))
		len--;
	return len;
}

/*
 * If path ends with suffix (complete path components), returns the
 * part before suffix (sans trailing directory separators).
 * Otherwise returns NULL.
 */
char *strip_path_suffix(const char *path, const char *suffix)
{
	int path_len = strlen(path), suffix_len = strlen(suffix);

	while (suffix_len) {
		if (!path_len)
			return NULL;

		if (is_dir_sep(path[path_len - 1])) {
			if (!is_dir_sep(suffix[suffix_len - 1]))
				return NULL;
			path_len = chomp_trailing_dir_sep(path, path_len);
			suffix_len = chomp_trailing_dir_sep(suffix, suffix_len);
		}
		else if (path[--path_len] != suffix[--suffix_len])
			return NULL;
	}

	if (path_len && !is_dir_sep(path[path_len - 1]))
		return NULL;
	return xstrndup(path, chomp_trailing_dir_sep(path, path_len));
}

int daemon_avoid_alias(const char *p)
{
	int sl, ndot;

	/*
	 * This resurrects the belts and suspenders paranoia check by HPA
	 * done in <435560F7.4080006@zytor.com> thread, now enter_repo()
	 * does not do getcwd() based path canonicalization.
	 *
	 * sl becomes true immediately after seeing '/' and continues to
	 * be true as long as dots continue after that without intervening
	 * non-dot character.
	 */
	if (!p || (*p != '/' && *p != '~'))
		return -1;
	sl = 1; ndot = 0;
	p++;

	while (1) {
		char ch = *p++;
		if (sl) {
			if (ch == '.')
				ndot++;
			else if (ch == '/') {
				if (ndot < 3)
					/* reject //, /./ and /../ */
					return -1;
				ndot = 0;
			}
			else if (ch == 0) {
				if (0 < ndot && ndot < 3)
					/* reject /.$ and /..$ */
					return -1;
				return 0;
			}
			else
				sl = ndot = 0;
		}
		else if (ch == 0)
			return 0;
		else if (ch == '/') {
			sl = 1;
			ndot = 0;
		}
	}
}

int offset_1st_component(const char *path)
{
	if (has_dos_drive_prefix(path))
		return 2 + is_dir_sep(path[2]);
	return is_dir_sep(path[0]);
}
