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

static char *git_vsnpath(char *buf, size_t n, const char *fmt, va_list args)
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
	va_list args;
	va_start(args, fmt);
	(void)git_vsnpath(buf, n, fmt, args);
	va_end(args);
	return buf;
}

char *git_pathdup(const char *fmt, ...)
{
	char path[PATH_MAX];
	va_list args;
	va_start(args, fmt);
	(void)git_vsnpath(path, sizeof(path), fmt, args);
	va_end(args);
	return xstrdup(path);
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
	const char *git_dir = get_git_dir();
	char *pathname = get_pathname();
	va_list args;
	unsigned len;

	len = strlen(git_dir);
	if (len > PATH_MAX-100)
		return bad_path;
	memcpy(pathname, git_dir, len);
	if (len && git_dir[len-1] != '/')
		pathname[len++] = '/';
	va_start(args, fmt);
	len += vsnprintf(pathname + len, PATH_MAX - len, fmt, args);
	va_end(args);
	if (len >= PATH_MAX)
		return bad_path;
	return cleanup_path(pathname);
}


/* git_mkstemp() - create tmp file honoring TMPDIR variable */
int git_mkstemp(char *path, size_t len, const char *template)
{
	const char *tmp;
	size_t n;

	tmp = getenv("TMPDIR");
	if (!tmp)
		tmp = "/tmp";
	n = snprintf(path, len, "%s/%s", tmp, template);
	if (len <= n) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return mkstemp(path);
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

static char *user_path(char *buf, char *path, int sz)
{
	struct passwd *pw;
	char *slash;
	int len, baselen;

	if (!path || path[0] != '~')
		return NULL;
	path++;
	slash = strchr(path, '/');
	if (path[0] == '/' || !path[0]) {
		pw = getpwuid(getuid());
	}
	else {
		if (slash) {
			*slash = 0;
			pw = getpwnam(path);
			*slash = '/';
		}
		else
			pw = getpwnam(path);
	}
	if (!pw || !pw->pw_dir || sz <= strlen(pw->pw_dir))
		return NULL;
	baselen = strlen(pw->pw_dir);
	memcpy(buf, pw->pw_dir, baselen);
	while ((1 < baselen) && (buf[baselen-1] == '/')) {
		buf[baselen-1] = 0;
		baselen--;
	}
	if (slash && slash[1]) {
		len = strlen(slash);
		if (sz <= baselen + len)
			return NULL;
		memcpy(buf + baselen, slash, len + 1);
	}
	return buf;
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
char *enter_repo(char *path, int strict)
{
	static char used_path[PATH_MAX];
	static char validated_path[PATH_MAX];

	if (!path)
		return NULL;

	if (!strict) {
		static const char *suffix[] = {
			".git/.git", "/.git", ".git", "", NULL,
		};
		int len = strlen(path);
		int i;
		while ((1 < len) && (path[len-1] == '/')) {
			path[len-1] = 0;
			len--;
		}
		if (PATH_MAX <= len)
			return NULL;
		if (path[0] == '~') {
			if (!user_path(used_path, path, PATH_MAX))
				return NULL;
			strcpy(validated_path, path);
			path = used_path;
		}
		else if (PATH_MAX - 10 < len)
			return NULL;
		else {
			path = strcpy(used_path, path);
			strcpy(validated_path, path);
		}
		len = strlen(path);
		for (i = 0; suffix[i]; i++) {
			strcpy(path + len, suffix[i]);
			if (!access(path, F_OK)) {
				strcat(validated_path, suffix[i]);
				break;
			}
		}
		if (!suffix[i] || chdir(path))
			return NULL;
		path = validated_path;
	}
	else if (chdir(path))
		return NULL;

	if (access("objects", X_OK) == 0 && access("refs", X_OK) == 0 &&
	    validate_headref("HEAD") == 0) {
		setenv(GIT_DIR_ENVIRONMENT, ".", 1);
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

const char *make_relative_path(const char *abs, const char *base)
{
	static char buf[PATH_MAX + 1];
	int baselen;
	if (!base)
		return abs;
	baselen = strlen(base);
	if (prefixcmp(abs, base))
		return abs;
	if (abs[baselen] == '/')
		baselen++;
	else if (base[baselen - 1] != '/')
		return abs;
	strcpy(buf, abs + baselen);
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
 * prefix_list = Colon-separated list of absolute paths
 *
 * Determines, for each path in prefix_list, whether the "prefix" really
 * is an ancestor directory of path.  Returns the length of the longest
 * ancestor directory, excluding any trailing slashes, or -1 if no prefix
 * is an ancestor.  (Note that this means 0 is returned if prefix_list is
 * "/".) "/foo" is not considered an ancestor of "/foobar".  Directories
 * are not considered to be their own ancestors.  path must be in a
 * canonical form: empty components, or "." or ".." components are not
 * allowed.  prefix_list may be null, which is like "".
 */
int longest_ancestor_length(const char *path, const char *prefix_list)
{
	char buf[PATH_MAX+1];
	const char *ceil, *colon;
	int len, max_len = -1;

	if (prefix_list == NULL || !strcmp(path, "/"))
		return -1;

	for (colon = ceil = prefix_list; *colon; ceil = colon+1) {
		for (colon = ceil; *colon && *colon != PATH_SEP; colon++);
		len = colon - ceil;
		if (len == 0 || len > PATH_MAX || !is_absolute_path(ceil))
			continue;
		strlcpy(buf, ceil, len+1);
		if (normalize_path_copy(buf, buf) < 0)
			continue;
		len = strlen(buf);
		if (len > 0 && buf[len-1] == '/')
			buf[--len] = '\0';

		if (!strncmp(path, buf, len) &&
		    path[len] == '/' &&
		    len > max_len) {
			max_len = len;
		}
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
