/*
 * Utilities for paths and pathnames
 */
#include "cache.h"
#include "repository.h"
#include "strbuf.h"
#include "string-list.h"
#include "dir.h"
#include "worktree.h"
#include "submodule-config.h"
#include "path.h"
#include "packfile.h"
#include "object-store.h"
#include "lockfile.h"
#include "exec-cmd.h"

static int get_st_mode_bits(const char *path, int *mode)
{
	struct stat st;
	if (lstat(path, &st) < 0)
		return -1;
	*mode = st.st_mode;
	return 0;
}

static char bad_path[] = "/bad-path/";

static struct strbuf *get_pathname(void)
{
	static struct strbuf pathname_array[4] = {
		STRBUF_INIT, STRBUF_INIT, STRBUF_INIT, STRBUF_INIT
	};
	static int index;
	struct strbuf *sb = &pathname_array[index];
	index = (index + 1) % ARRAY_SIZE(pathname_array);
	strbuf_reset(sb);
	return sb;
}

static const char *cleanup_path(const char *path)
{
	/* Clean it up */
	if (skip_prefix(path, "./", &path)) {
		while (*path == '/')
			path++;
	}
	return path;
}

static void strbuf_cleanup_path(struct strbuf *sb)
{
	const char *path = cleanup_path(sb->buf);
	if (path > sb->buf)
		strbuf_remove(sb, 0, path - sb->buf);
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
	return (char *)cleanup_path(buf);
}

static int dir_prefix(const char *buf, const char *dir)
{
	int len = strlen(dir);
	return !strncmp(buf, dir, len) &&
		(is_dir_sep(buf[len]) || buf[len] == '\0');
}

/* $buf =~ m|$dir/+$file| but without regex */
static int is_dir_file(const char *buf, const char *dir, const char *file)
{
	int len = strlen(dir);
	if (strncmp(buf, dir, len) || !is_dir_sep(buf[len]))
		return 0;
	while (is_dir_sep(buf[len]))
		len++;
	return !strcmp(buf + len, file);
}

static void replace_dir(struct strbuf *buf, int len, const char *newdir)
{
	int newlen = strlen(newdir);
	int need_sep = (buf->buf[len] && !is_dir_sep(buf->buf[len])) &&
		!is_dir_sep(newdir[newlen - 1]);
	if (need_sep)
		len--;	 /* keep one char, to be replaced with '/'  */
	strbuf_splice(buf, 0, len, newdir, newlen);
	if (need_sep)
		buf->buf[newlen] = '/';
}

struct common_dir {
	/* Not considered garbage for report_linked_checkout_garbage */
	unsigned ignore_garbage:1;
	unsigned is_dir:1;
	/* Belongs to the common dir, though it may contain paths that don't */
	unsigned is_common:1;
	const char *path;
};

static struct common_dir common_list[] = {
	{ 0, 1, 1, "branches" },
	{ 0, 1, 1, "common" },
	{ 0, 1, 1, "hooks" },
	{ 0, 1, 1, "info" },
	{ 0, 0, 0, "info/sparse-checkout" },
	{ 1, 1, 1, "logs" },
	{ 1, 0, 0, "logs/HEAD" },
	{ 0, 1, 0, "logs/refs/bisect" },
	{ 0, 1, 0, "logs/refs/rewritten" },
	{ 0, 1, 0, "logs/refs/worktree" },
	{ 0, 1, 1, "lost-found" },
	{ 0, 1, 1, "objects" },
	{ 0, 1, 1, "refs" },
	{ 0, 1, 0, "refs/bisect" },
	{ 0, 1, 0, "refs/rewritten" },
	{ 0, 1, 0, "refs/worktree" },
	{ 0, 1, 1, "remotes" },
	{ 0, 1, 1, "worktrees" },
	{ 0, 1, 1, "rr-cache" },
	{ 0, 1, 1, "svn" },
	{ 0, 0, 1, "config" },
	{ 1, 0, 1, "gc.pid" },
	{ 0, 0, 1, "packed-refs" },
	{ 0, 0, 1, "shallow" },
	{ 0, 0, 0, NULL }
};

/*
 * A compressed trie.  A trie node consists of zero or more characters that
 * are common to all elements with this prefix, optionally followed by some
 * children.  If value is not NULL, the trie node is a terminal node.
 *
 * For example, consider the following set of strings:
 * abc
 * def
 * definite
 * definition
 *
 * The trie would look like:
 * root: len = 0, children a and d non-NULL, value = NULL.
 *    a: len = 2, contents = bc, value = (data for "abc")
 *    d: len = 2, contents = ef, children i non-NULL, value = (data for "def")
 *       i: len = 3, contents = nit, children e and i non-NULL, value = NULL
 *           e: len = 0, children all NULL, value = (data for "definite")
 *           i: len = 2, contents = on, children all NULL,
 *              value = (data for "definition")
 */
struct trie {
	struct trie *children[256];
	int len;
	char *contents;
	void *value;
};

static struct trie *make_trie_node(const char *key, void *value)
{
	struct trie *new_node = xcalloc(1, sizeof(*new_node));
	new_node->len = strlen(key);
	if (new_node->len) {
		new_node->contents = xmalloc(new_node->len);
		memcpy(new_node->contents, key, new_node->len);
	}
	new_node->value = value;
	return new_node;
}

/*
 * Add a key/value pair to a trie.  The key is assumed to be \0-terminated.
 * If there was an existing value for this key, return it.
 */
static void *add_to_trie(struct trie *root, const char *key, void *value)
{
	struct trie *child;
	void *old;
	int i;

	if (!*key) {
		/* we have reached the end of the key */
		old = root->value;
		root->value = value;
		return old;
	}

	for (i = 0; i < root->len; i++) {
		if (root->contents[i] == key[i])
			continue;

		/*
		 * Split this node: child will contain this node's
		 * existing children.
		 */
		child = xmalloc(sizeof(*child));
		memcpy(child->children, root->children, sizeof(root->children));

		child->len = root->len - i - 1;
		if (child->len) {
			child->contents = xstrndup(root->contents + i + 1,
						   child->len);
		}
		child->value = root->value;
		root->value = NULL;
		root->len = i;

		memset(root->children, 0, sizeof(root->children));
		root->children[(unsigned char)root->contents[i]] = child;

		/* This is the newly-added child. */
		root->children[(unsigned char)key[i]] =
			make_trie_node(key + i + 1, value);
		return NULL;
	}

	/* We have matched the entire compressed section */
	if (key[i]) {
		child = root->children[(unsigned char)key[root->len]];
		if (child) {
			return add_to_trie(child, key + root->len + 1, value);
		} else {
			child = make_trie_node(key + root->len + 1, value);
			root->children[(unsigned char)key[root->len]] = child;
			return NULL;
		}
	}

	old = root->value;
	root->value = value;
	return old;
}

typedef int (*match_fn)(const char *unmatched, void *value, void *baton);

/*
 * Search a trie for some key.  Find the longest /-or-\0-terminated
 * prefix of the key for which the trie contains a value.  If there is
 * no such prefix, return -1.  Otherwise call fn with the unmatched
 * portion of the key and the found value.  If fn returns 0 or
 * positive, then return its return value.  If fn returns negative,
 * then call fn with the next-longest /-terminated prefix of the key
 * (i.e. a parent directory) for which the trie contains a value, and
 * handle its return value the same way.  If there is no shorter
 * /-terminated prefix with a value left, then return the negative
 * return value of the most recent fn invocation.
 *
 * The key is partially normalized: consecutive slashes are skipped.
 *
 * For example, consider the trie containing only [logs,
 * logs/refs/bisect], both with values, but not logs/refs.
 *
 * | key                | unmatched      | prefix to node   | return value |
 * |--------------------|----------------|------------------|--------------|
 * | a                  | not called     | n/a              | -1           |
 * | logstore           | not called     | n/a              | -1           |
 * | logs               | \0             | logs             | as per fn    |
 * | logs/              | /              | logs             | as per fn    |
 * | logs/refs          | /refs          | logs             | as per fn    |
 * | logs/refs/         | /refs/         | logs             | as per fn    |
 * | logs/refs/b        | /refs/b        | logs             | as per fn    |
 * | logs/refs/bisected | /refs/bisected | logs             | as per fn    |
 * | logs/refs/bisect   | \0             | logs/refs/bisect | as per fn    |
 * | logs/refs/bisect/  | /              | logs/refs/bisect | as per fn    |
 * | logs/refs/bisect/a | /a             | logs/refs/bisect | as per fn    |
 * | (If fn in the previous line returns -1, then fn is called once more:) |
 * | logs/refs/bisect/a | /refs/bisect/a | logs             | as per fn    |
 * |--------------------|----------------|------------------|--------------|
 */
static int trie_find(struct trie *root, const char *key, match_fn fn,
		     void *baton)
{
	int i;
	int result;
	struct trie *child;

	if (!*key) {
		/* we have reached the end of the key */
		if (root->value && !root->len)
			return fn(key, root->value, baton);
		else
			return -1;
	}

	for (i = 0; i < root->len; i++) {
		/* Partial path normalization: skip consecutive slashes. */
		if (key[i] == '/' && key[i+1] == '/') {
			key++;
			continue;
		}
		if (root->contents[i] != key[i])
			return -1;
	}

	/* Matched the entire compressed section */
	key += i;
	if (!*key) {
		/* End of key */
		if (root->value)
			return fn(key, root->value, baton);
		else
			return -1;
	}

	/* Partial path normalization: skip consecutive slashes */
	while (key[0] == '/' && key[1] == '/')
		key++;

	child = root->children[(unsigned char)*key];
	if (child)
		result = trie_find(child, key + 1, fn, baton);
	else
		result = -1;

	if (result >= 0 || (*key != '/' && *key != 0))
		return result;
	if (root->value)
		return fn(key, root->value, baton);
	else
		return -1;
}

static struct trie common_trie;
static int common_trie_done_setup;

static void init_common_trie(void)
{
	struct common_dir *p;

	if (common_trie_done_setup)
		return;

	for (p = common_list; p->path; p++)
		add_to_trie(&common_trie, p->path, p);

	common_trie_done_setup = 1;
}

/*
 * Helper function for update_common_dir: returns 1 if the dir
 * prefix is common.
 */
static int check_common(const char *unmatched, void *value, void *baton)
{
	struct common_dir *dir = value;

	if (dir->is_dir && (unmatched[0] == 0 || unmatched[0] == '/'))
		return dir->is_common;

	if (!dir->is_dir && unmatched[0] == 0)
		return dir->is_common;

	return 0;
}

static void update_common_dir(struct strbuf *buf, int git_dir_len,
			      const char *common_dir)
{
	char *base = buf->buf + git_dir_len;
	int has_lock_suffix = strbuf_strip_suffix(buf, LOCK_SUFFIX);

	init_common_trie();
	if (trie_find(&common_trie, base, check_common, NULL) > 0)
		replace_dir(buf, git_dir_len, common_dir);

	if (has_lock_suffix)
		strbuf_addstr(buf, LOCK_SUFFIX);
}

void report_linked_checkout_garbage(void)
{
	struct strbuf sb = STRBUF_INIT;
	const struct common_dir *p;
	int len;

	if (!the_repository->different_commondir)
		return;
	strbuf_addf(&sb, "%s/", get_git_dir());
	len = sb.len;
	for (p = common_list; p->path; p++) {
		const char *path = p->path;
		if (p->ignore_garbage)
			continue;
		strbuf_setlen(&sb, len);
		strbuf_addstr(&sb, path);
		if (file_exists(sb.buf))
			report_garbage(PACKDIR_FILE_GARBAGE, sb.buf);
	}
	strbuf_release(&sb);
}

static void adjust_git_path(const struct repository *repo,
			    struct strbuf *buf, int git_dir_len)
{
	const char *base = buf->buf + git_dir_len;
	if (is_dir_file(base, "info", "grafts"))
		strbuf_splice(buf, 0, buf->len,
			      repo->graft_file, strlen(repo->graft_file));
	else if (!strcmp(base, "index"))
		strbuf_splice(buf, 0, buf->len,
			      repo->index_file, strlen(repo->index_file));
	else if (dir_prefix(base, "objects"))
		replace_dir(buf, git_dir_len + 7, repo->objects->odb->path);
	else if (git_hooks_path && dir_prefix(base, "hooks"))
		replace_dir(buf, git_dir_len + 5, git_hooks_path);
	else if (repo->different_commondir)
		update_common_dir(buf, git_dir_len, repo->commondir);
}

static void strbuf_worktree_gitdir(struct strbuf *buf,
				   const struct repository *repo,
				   const struct worktree *wt)
{
	if (!wt)
		strbuf_addstr(buf, repo->gitdir);
	else if (!wt->id)
		strbuf_addstr(buf, repo->commondir);
	else
		strbuf_git_common_path(buf, repo, "worktrees/%s", wt->id);
}

static void do_git_path(const struct repository *repo,
			const struct worktree *wt, struct strbuf *buf,
			const char *fmt, va_list args)
{
	int gitdir_len;
	strbuf_worktree_gitdir(buf, repo, wt);
	if (buf->len && !is_dir_sep(buf->buf[buf->len - 1]))
		strbuf_addch(buf, '/');
	gitdir_len = buf->len;
	strbuf_vaddf(buf, fmt, args);
	if (!wt)
		adjust_git_path(repo, buf, gitdir_len);
	strbuf_cleanup_path(buf);
}

char *repo_git_path(const struct repository *repo,
		    const char *fmt, ...)
{
	struct strbuf path = STRBUF_INIT;
	va_list args;
	va_start(args, fmt);
	do_git_path(repo, NULL, &path, fmt, args);
	va_end(args);
	return strbuf_detach(&path, NULL);
}

void strbuf_repo_git_path(struct strbuf *sb,
			  const struct repository *repo,
			  const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_git_path(repo, NULL, sb, fmt, args);
	va_end(args);
}

char *git_path_buf(struct strbuf *buf, const char *fmt, ...)
{
	va_list args;
	strbuf_reset(buf);
	va_start(args, fmt);
	do_git_path(the_repository, NULL, buf, fmt, args);
	va_end(args);
	return buf->buf;
}

void strbuf_git_path(struct strbuf *sb, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_git_path(the_repository, NULL, sb, fmt, args);
	va_end(args);
}

const char *git_path(const char *fmt, ...)
{
	struct strbuf *pathname = get_pathname();
	va_list args;
	va_start(args, fmt);
	do_git_path(the_repository, NULL, pathname, fmt, args);
	va_end(args);
	return pathname->buf;
}

char *git_pathdup(const char *fmt, ...)
{
	struct strbuf path = STRBUF_INIT;
	va_list args;
	va_start(args, fmt);
	do_git_path(the_repository, NULL, &path, fmt, args);
	va_end(args);
	return strbuf_detach(&path, NULL);
}

char *mkpathdup(const char *fmt, ...)
{
	struct strbuf sb = STRBUF_INIT;
	va_list args;
	va_start(args, fmt);
	strbuf_vaddf(&sb, fmt, args);
	va_end(args);
	strbuf_cleanup_path(&sb);
	return strbuf_detach(&sb, NULL);
}

const char *mkpath(const char *fmt, ...)
{
	va_list args;
	struct strbuf *pathname = get_pathname();
	va_start(args, fmt);
	strbuf_vaddf(pathname, fmt, args);
	va_end(args);
	return cleanup_path(pathname->buf);
}

const char *worktree_git_path(const struct worktree *wt, const char *fmt, ...)
{
	struct strbuf *pathname = get_pathname();
	va_list args;
	va_start(args, fmt);
	do_git_path(the_repository, wt, pathname, fmt, args);
	va_end(args);
	return pathname->buf;
}

static void do_worktree_path(const struct repository *repo,
			     struct strbuf *buf,
			     const char *fmt, va_list args)
{
	strbuf_addstr(buf, repo->worktree);
	if(buf->len && !is_dir_sep(buf->buf[buf->len - 1]))
		strbuf_addch(buf, '/');

	strbuf_vaddf(buf, fmt, args);
	strbuf_cleanup_path(buf);
}

char *repo_worktree_path(const struct repository *repo, const char *fmt, ...)
{
	struct strbuf path = STRBUF_INIT;
	va_list args;

	if (!repo->worktree)
		return NULL;

	va_start(args, fmt);
	do_worktree_path(repo, &path, fmt, args);
	va_end(args);

	return strbuf_detach(&path, NULL);
}

void strbuf_repo_worktree_path(struct strbuf *sb,
			       const struct repository *repo,
			       const char *fmt, ...)
{
	va_list args;

	if (!repo->worktree)
		return;

	va_start(args, fmt);
	do_worktree_path(repo, sb, fmt, args);
	va_end(args);
}

/* Returns 0 on success, negative on failure. */
static int do_submodule_path(struct strbuf *buf, const char *path,
			     const char *fmt, va_list args)
{
	struct strbuf git_submodule_common_dir = STRBUF_INIT;
	struct strbuf git_submodule_dir = STRBUF_INIT;
	int ret;

	ret = submodule_to_gitdir(&git_submodule_dir, path);
	if (ret)
		goto cleanup;

	strbuf_complete(&git_submodule_dir, '/');
	strbuf_addbuf(buf, &git_submodule_dir);
	strbuf_vaddf(buf, fmt, args);

	if (get_common_dir_noenv(&git_submodule_common_dir, git_submodule_dir.buf))
		update_common_dir(buf, git_submodule_dir.len, git_submodule_common_dir.buf);

	strbuf_cleanup_path(buf);

cleanup:
	strbuf_release(&git_submodule_dir);
	strbuf_release(&git_submodule_common_dir);
	return ret;
}

char *git_pathdup_submodule(const char *path, const char *fmt, ...)
{
	int err;
	va_list args;
	struct strbuf buf = STRBUF_INIT;
	va_start(args, fmt);
	err = do_submodule_path(&buf, path, fmt, args);
	va_end(args);
	if (err) {
		strbuf_release(&buf);
		return NULL;
	}
	return strbuf_detach(&buf, NULL);
}

int strbuf_git_path_submodule(struct strbuf *buf, const char *path,
			      const char *fmt, ...)
{
	int err;
	va_list args;
	va_start(args, fmt);
	err = do_submodule_path(buf, path, fmt, args);
	va_end(args);

	return err;
}

static void do_git_common_path(const struct repository *repo,
			       struct strbuf *buf,
			       const char *fmt,
			       va_list args)
{
	strbuf_addstr(buf, repo->commondir);
	if (buf->len && !is_dir_sep(buf->buf[buf->len - 1]))
		strbuf_addch(buf, '/');
	strbuf_vaddf(buf, fmt, args);
	strbuf_cleanup_path(buf);
}

const char *git_common_path(const char *fmt, ...)
{
	struct strbuf *pathname = get_pathname();
	va_list args;
	va_start(args, fmt);
	do_git_common_path(the_repository, pathname, fmt, args);
	va_end(args);
	return pathname->buf;
}

void strbuf_git_common_path(struct strbuf *sb,
			    const struct repository *repo,
			    const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_git_common_path(repo, sb, fmt, args);
	va_end(args);
}

int validate_headref(const char *path)
{
	struct stat st;
	char buffer[256];
	const char *refname;
	struct object_id oid;
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

	if (len < 0)
		return -1;
	buffer[len] = '\0';

	/*
	 * Is it a symbolic ref?
	 */
	if (skip_prefix(buffer, "ref:", &refname)) {
		while (isspace(*refname))
			refname++;
		if (starts_with(refname, "refs/"))
			return 0;
	}

	/*
	 * Is this a detached HEAD?
	 */
	if (!get_oid_hex(buffer, &oid))
		return 0;

	return -1;
}

static struct passwd *getpw_str(const char *username, size_t len)
{
	struct passwd *pw;
	char *username_z = xmemdupz(username, len);
	pw = getpwnam(username_z);
	free(username_z);
	return pw;
}

/*
 * Return a string with ~ and ~user expanded via getpw*.  If buf != NULL,
 * then it is a newly allocated string. Returns NULL on getpw failure or
 * if path is NULL.
 *
 * If real_home is true, real_path($HOME) is used in the expansion.
 */
char *expand_user_path(const char *path, int real_home)
{
	struct strbuf user_path = STRBUF_INIT;
	const char *to_copy = path;

	if (path == NULL)
		goto return_null;
#ifdef __MINGW32__
	if (path[0] == '/')
		return system_path(path + 1);
#endif
	if (path[0] == '~') {
		const char *first_slash = strchrnul(path, '/');
		const char *username = path + 1;
		size_t username_len = first_slash - username;
		if (username_len == 0) {
			const char *home = getenv("HOME");
			if (!home)
				goto return_null;
			if (real_home)
				strbuf_add_real_path(&user_path, home);
			else
				strbuf_addstr(&user_path, home);
#ifdef GIT_WINDOWS_NATIVE
			convert_slashes(user_path.buf);
#endif
		} else {
			struct passwd *pw = getpw_str(username, username_len);
			if (!pw)
				goto return_null;
			strbuf_addstr(&user_path, pw->pw_dir);
		}
		to_copy = first_slash;
	}
	strbuf_addstr(&user_path, to_copy);
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
 * Unless "strict" is given, we check "%s/.git", "%s", "%s.git/.git", "%s.git"
 * in this order. We select the first one that is a valid git repository, and
 * chdir() to it. If none match, or we fail to chdir, we return NULL.
 *
 * If all goes well, we return the directory we used to chdir() (but
 * before ~user is expanded), avoiding getcwd() resolving symbolic
 * links.  User relative paths are also returned as they are given,
 * except DWIM suffixing.
 */
const char *enter_repo(const char *path, int strict)
{
	static struct strbuf validated_path = STRBUF_INIT;
	static struct strbuf used_path = STRBUF_INIT;

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

		/*
		 * We can handle arbitrary-sized buffers, but this remains as a
		 * sanity check on untrusted input.
		 */
		if (PATH_MAX <= len)
			return NULL;

		strbuf_reset(&used_path);
		strbuf_reset(&validated_path);
		strbuf_add(&used_path, path, len);
		strbuf_add(&validated_path, path, len);

		if (used_path.buf[0] == '~') {
			char *newpath = expand_user_path(used_path.buf, 0);
			if (!newpath)
				return NULL;
			strbuf_attach(&used_path, newpath, strlen(newpath),
				      strlen(newpath));
		}
		for (i = 0; suffix[i]; i++) {
			struct stat st;
			size_t baselen = used_path.len;
			strbuf_addstr(&used_path, suffix[i]);
			if (!stat(used_path.buf, &st) &&
			    (S_ISREG(st.st_mode) ||
			    (S_ISDIR(st.st_mode) && is_git_directory(used_path.buf)))) {
				strbuf_addstr(&validated_path, suffix[i]);
				break;
			}
			strbuf_setlen(&used_path, baselen);
		}
		if (!suffix[i])
			return NULL;
		gitfile = read_gitfile(used_path.buf);
		if (gitfile) {
			strbuf_reset(&used_path);
			strbuf_addstr(&used_path, gitfile);
		}
		if (chdir(used_path.buf))
			return NULL;
		path = validated_path.buf;
	}
	else {
		const char *gitfile = read_gitfile(path);
		if (gitfile)
			path = gitfile;
		if (chdir(path))
			return NULL;
	}

	if (is_git_directory(".")) {
		set_git_dir(".");
		check_repository_format();
		return path;
	}

	return NULL;
}

static int calc_shared_perm(int mode)
{
	int tweak;

	if (get_shared_repository() < 0)
		tweak = -get_shared_repository();
	else
		tweak = get_shared_repository();

	if (!(mode & S_IWUSR))
		tweak &= ~0222;
	if (mode & S_IXUSR)
		/* Copy read bits to execute bits */
		tweak |= (tweak & 0444) >> 2;
	if (get_shared_repository() < 0)
		mode = (mode & ~0777) | tweak;
	else
		mode |= tweak;

	return mode;
}


int adjust_shared_perm(const char *path)
{
	int old_mode, new_mode;

	if (!get_shared_repository())
		return 0;
	if (get_st_mode_bits(path, &old_mode) < 0)
		return -1;

	new_mode = calc_shared_perm(old_mode);
	if (S_ISDIR(old_mode)) {
		/* Copy read bits to execute bits */
		new_mode |= (new_mode & 0444) >> 2;
		new_mode |= FORCE_DIR_SET_GID;
	}

	if (((old_mode ^ new_mode) & ~S_IFMT) &&
			chmod(path, (new_mode & ~S_IFMT)) < 0)
		return -2;
	return 0;
}

void safe_create_dir(const char *dir, int share)
{
	if (mkdir(dir, 0777) < 0) {
		if (errno != EEXIST) {
			perror(dir);
			exit(1);
		}
	}
	else if (share && adjust_shared_perm(dir))
		die(_("Could not make %s writable by group"), dir);
}

static int have_same_root(const char *path1, const char *path2)
{
	int is_abs1, is_abs2;

	is_abs1 = is_absolute_path(path1);
	is_abs2 = is_absolute_path(path2);
	return (is_abs1 && is_abs2 && tolower(path1[0]) == tolower(path2[0])) ||
	       (!is_abs1 && !is_abs2);
}

/*
 * Give path as relative to prefix.
 *
 * The strbuf may or may not be used, so do not assume it contains the
 * returned path.
 */
const char *relative_path(const char *in, const char *prefix,
			  struct strbuf *sb)
{
	int in_len = in ? strlen(in) : 0;
	int prefix_len = prefix ? strlen(prefix) : 0;
	int in_off = 0;
	int prefix_off = 0;
	int i = 0, j = 0;

	if (!in_len)
		return "./";
	else if (!prefix_len)
		return in;

	if (have_same_root(in, prefix))
		/* bypass dos_drive, for "c:" is identical to "C:" */
		i = j = has_dos_drive_prefix(in);
	else {
		return in;
	}

	while (i < prefix_len && j < in_len && prefix[i] == in[j]) {
		if (is_dir_sep(prefix[i])) {
			while (is_dir_sep(prefix[i]))
				i++;
			while (is_dir_sep(in[j]))
				j++;
			prefix_off = i;
			in_off = j;
		} else {
			i++;
			j++;
		}
	}

	if (
	    /* "prefix" seems like prefix of "in" */
	    i >= prefix_len &&
	    /*
	     * but "/foo" is not a prefix of "/foobar"
	     * (i.e. prefix not end with '/')
	     */
	    prefix_off < prefix_len) {
		if (j >= in_len) {
			/* in="/a/b", prefix="/a/b" */
			in_off = in_len;
		} else if (is_dir_sep(in[j])) {
			/* in="/a/b/c", prefix="/a/b" */
			while (is_dir_sep(in[j]))
				j++;
			in_off = j;
		} else {
			/* in="/a/bbb/c", prefix="/a/b" */
			i = prefix_off;
		}
	} else if (
		   /* "in" is short than "prefix" */
		   j >= in_len &&
		   /* "in" not end with '/' */
		   in_off < in_len) {
		if (is_dir_sep(prefix[i])) {
			/* in="/a/b", prefix="/a/b/c/" */
			while (is_dir_sep(prefix[i]))
				i++;
			in_off = in_len;
		}
	}
	in += in_off;
	in_len -= in_off;

	if (i >= prefix_len) {
		if (!in_len)
			return "./";
		else
			return in;
	}

	strbuf_reset(sb);
	strbuf_grow(sb, in_len);

	while (i < prefix_len) {
		if (is_dir_sep(prefix[i])) {
			strbuf_addstr(sb, "../");
			while (is_dir_sep(prefix[i]))
				i++;
			continue;
		}
		i++;
	}
	if (!is_dir_sep(prefix[prefix_len - 1]))
		strbuf_addstr(sb, "../");

	strbuf_addstr(sb, in);

	return sb->buf;
}

/*
 * A simpler implementation of relative_path
 *
 * Get relative path by removing "prefix" from "in". This function
 * first appears in v1.5.6-1-g044bbbc, and makes git_dir shorter
 * to increase performance when traversing the path to work_tree.
 */
const char *remove_leading_path(const char *in, const char *prefix)
{
	static struct strbuf buf = STRBUF_INIT;
	int i = 0, j = 0;

	if (!prefix || !prefix[0])
		return in;
	while (prefix[i]) {
		if (is_dir_sep(prefix[i])) {
			if (!is_dir_sep(in[j]))
				return in;
			while (is_dir_sep(prefix[i]))
				i++;
			while (is_dir_sep(in[j]))
				j++;
			continue;
		} else if (in[j] != prefix[i]) {
			return in;
		}
		i++;
		j++;
	}
	if (
	    /* "/foo" is a prefix of "/foo" */
	    in[j] &&
	    /* "/foo" is not a prefix of "/foobar" */
	    !is_dir_sep(prefix[i-1]) && !is_dir_sep(in[j])
	   )
		return in;
	while (is_dir_sep(in[j]))
		j++;

	strbuf_reset(&buf);
	if (!in[j])
		strbuf_addstr(&buf, ".");
	else
		strbuf_addstr(&buf, in + j);
	return buf.buf;
}

/*
 * It is okay if dst == src, but they should not overlap otherwise.
 * The "dst" buffer must be at least as long as "src"; normalizing may shrink
 * the size of the path, but will never grow it.
 *
 * Performs the following normalizations on src, storing the result in dst:
 * - Ensures that components are separated by '/' (Windows only)
 * - Squashes sequences of '/' except "//server/share" on Windows
 * - Removes "." components.
 * - Removes ".." components, and the components the precede them.
 * Returns failure (non-zero) if a ".." component appears as first path
 * component anytime during the normalization. Otherwise, returns success (0).
 *
 * Note that this function is purely textual.  It does not follow symlinks,
 * verify the existence of the path, or make any system calls.
 *
 * prefix_len != NULL is for a specific case of prefix_pathspec():
 * assume that src == dst and src[0..prefix_len-1] is already
 * normalized, any time "../" eats up to the prefix_len part,
 * prefix_len is reduced. In the end prefix_len is the remaining
 * prefix that has not been overridden by user pathspec.
 *
 * NEEDSWORK: This function doesn't perform normalization w.r.t. trailing '/'.
 * For everything but the root folder itself, the normalized path should not
 * end with a '/', then the callers need to be fixed up accordingly.
 *
 */
int normalize_path_copy_len(char *dst, const char *src, int *prefix_len)
{
	char *dst0;
	const char *end;

	/*
	 * Copy initial part of absolute path: "/", "C:/", "//server/share/".
	 */
	end = src + offset_1st_component(src);
	while (src < end) {
		char c = *src++;
		if (is_dir_sep(c))
			c = '/';
		*dst++ = c;
	}
	dst0 = dst;

	while (is_dir_sep(*src))
		src++;

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
		if (prefix_len && *prefix_len > dst - dst0)
			*prefix_len = dst - dst0;
	}
	*dst = '\0';
	return 0;
}

int normalize_path_copy(char *dst, const char *src)
{
	return normalize_path_copy_len(dst, src, NULL);
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
 * If path ends with suffix (complete path components), returns the offset of
 * the last character in the path before the suffix (sans trailing directory
 * separators), and -1 otherwise.
 */
static ssize_t stripped_path_suffix_offset(const char *path, const char *suffix)
{
	int path_len = strlen(path), suffix_len = strlen(suffix);

	while (suffix_len) {
		if (!path_len)
			return -1;

		if (is_dir_sep(path[path_len - 1])) {
			if (!is_dir_sep(suffix[suffix_len - 1]))
				return -1;
			path_len = chomp_trailing_dir_sep(path, path_len);
			suffix_len = chomp_trailing_dir_sep(suffix, suffix_len);
		}
		else if (path[--path_len] != suffix[--suffix_len])
			return -1;
	}

	if (path_len && !is_dir_sep(path[path_len - 1]))
		return -1;
	return chomp_trailing_dir_sep(path, path_len);
}

/*
 * Returns true if the path ends with components, considering only complete path
 * components, and false otherwise.
 */
int ends_with_path_components(const char *path, const char *components)
{
	return stripped_path_suffix_offset(path, components) != -1;
}

/*
 * If path ends with suffix (complete path components), returns the
 * part before suffix (sans trailing directory separators).
 * Otherwise returns NULL.
 */
char *strip_path_suffix(const char *path, const char *suffix)
{
	ssize_t offset = stripped_path_suffix_offset(path, suffix);

	return offset == -1 ? NULL : xstrndup(path, offset);
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

/*
 * On NTFS, we need to be careful to disallow certain synonyms of the `.git/`
 * directory:
 *
 * - For historical reasons, file names that end in spaces or periods are
 *   automatically trimmed. Therefore, `.git . . ./` is a valid way to refer
 *   to `.git/`.
 *
 * - For other historical reasons, file names that do not conform to the 8.3
 *   format (up to eight characters for the basename, three for the file
 *   extension, certain characters not allowed such as `+`, etc) are associated
 *   with a so-called "short name", at least on the `C:` drive by default.
 *   Which means that `git~1/` is a valid way to refer to `.git/`.
 *
 *   Note: Technically, `.git/` could receive the short name `git~2` if the
 *   short name `git~1` were already used. In Git, however, we guarantee that
 *   `.git` is the first item in a directory, therefore it will be associated
 *   with the short name `git~1` (unless short names are disabled).
 *
 * - For yet other historical reasons, NTFS supports so-called "Alternate Data
 *   Streams", i.e. metadata associated with a given file, referred to via
 *   `<filename>:<stream-name>:<stream-type>`. There exists a default stream
 *   type for directories, allowing `.git/` to be accessed via
 *   `.git::$INDEX_ALLOCATION/`.
 *
 * When this function returns 1, it indicates that the specified file/directory
 * name refers to a `.git` file or directory, or to any of these synonyms, and
 * Git should therefore not track it.
 *
 * For performance reasons, _all_ Alternate Data Streams of `.git/` are
 * forbidden, not just `::$INDEX_ALLOCATION`.
 *
 * This function is intended to be used by `git fsck` even on platforms where
 * the backslash is a regular filename character, therefore it needs to handle
 * backlash characters in the provided `name` specially: they are interpreted
 * as directory separators.
 */
int is_ntfs_dotgit(const char *name)
{
	char c;

	/*
	 * Note that when we don't find `.git` or `git~1` we end up with `name`
	 * advanced partway through the string. That's okay, though, as we
	 * return immediately in those cases, without looking at `name` any
	 * further.
	 */
	c = *(name++);
	if (c == '.') {
		/* .git */
		if (((c = *(name++)) != 'g' && c != 'G') ||
		    ((c = *(name++)) != 'i' && c != 'I') ||
		    ((c = *(name++)) != 't' && c != 'T'))
			return 0;
	} else if (c == 'g' || c == 'G') {
		/* git ~1 */
		if (((c = *(name++)) != 'i' && c != 'I') ||
		    ((c = *(name++)) != 't' && c != 'T') ||
		    *(name++) != '~' ||
		    *(name++) != '1')
			return 0;
	} else
		return 0;

	for (;;) {
		c = *(name++);
		if (!c || c == '\\' || c == '/' || c == ':')
			return 1;
		if (c != '.' && c != ' ')
			return 0;
	}
}

static int is_ntfs_dot_generic(const char *name,
			       const char *dotgit_name,
			       size_t len,
			       const char *dotgit_ntfs_shortname_prefix)
{
	int saw_tilde;
	size_t i;

	if ((name[0] == '.' && !strncasecmp(name + 1, dotgit_name, len))) {
		i = len + 1;
only_spaces_and_periods:
		for (;;) {
			char c = name[i++];
			if (!c || c == ':')
				return 1;
			if (c != ' ' && c != '.')
				return 0;
		}
	}

	/*
	 * Is it a regular NTFS short name, i.e. shortened to 6 characters,
	 * followed by ~1, ... ~4?
	 */
	if (!strncasecmp(name, dotgit_name, 6) && name[6] == '~' &&
	    name[7] >= '1' && name[7] <= '4') {
		i = 8;
		goto only_spaces_and_periods;
	}

	/*
	 * Is it a fall-back NTFS short name (for details, see
	 * https://en.wikipedia.org/wiki/8.3_filename?
	 */
	for (i = 0, saw_tilde = 0; i < 8; i++)
		if (name[i] == '\0')
			return 0;
		else if (saw_tilde) {
			if (name[i] < '0' || name[i] > '9')
				return 0;
		} else if (name[i] == '~') {
			if (name[++i] < '1' || name[i] > '9')
				return 0;
			saw_tilde = 1;
		} else if (i >= 6)
			return 0;
		else if (name[i] & 0x80) {
			/*
			 * We know our needles contain only ASCII, so we clamp
			 * here to make the results of tolower() sane.
			 */
			return 0;
		} else if (tolower(name[i]) != dotgit_ntfs_shortname_prefix[i])
			return 0;

	goto only_spaces_and_periods;
}

/*
 * Inline helper to make sure compiler resolves strlen() on literals at
 * compile time.
 */
static inline int is_ntfs_dot_str(const char *name, const char *dotgit_name,
				  const char *dotgit_ntfs_shortname_prefix)
{
	return is_ntfs_dot_generic(name, dotgit_name, strlen(dotgit_name),
				   dotgit_ntfs_shortname_prefix);
}

int is_ntfs_dotgitmodules(const char *name)
{
	return is_ntfs_dot_str(name, "gitmodules", "gi7eba");
}

int is_ntfs_dotgitignore(const char *name)
{
	return is_ntfs_dot_str(name, "gitignore", "gi250a");
}

int is_ntfs_dotgitattributes(const char *name)
{
	return is_ntfs_dot_str(name, "gitattributes", "gi7d29");
}

int looks_like_command_line_option(const char *str)
{
	return str && str[0] == '-';
}

char *xdg_config_home(const char *filename)
{
	const char *home, *config_home;

	assert(filename);
	config_home = getenv("XDG_CONFIG_HOME");
	if (config_home && *config_home)
		return mkpathdup("%s/git/%s", config_home, filename);

	home = getenv("HOME");
	if (home)
		return mkpathdup("%s/.config/git/%s", home, filename);
	return NULL;
}

char *xdg_cache_home(const char *filename)
{
	const char *home, *cache_home;

	assert(filename);
	cache_home = getenv("XDG_CACHE_HOME");
	if (cache_home && *cache_home)
		return mkpathdup("%s/git/%s", cache_home, filename);

	home = getenv("HOME");
	if (home)
		return mkpathdup("%s/.cache/git/%s", home, filename);
	return NULL;
}

REPO_GIT_PATH_FUNC(cherry_pick_head, "CHERRY_PICK_HEAD")
REPO_GIT_PATH_FUNC(revert_head, "REVERT_HEAD")
REPO_GIT_PATH_FUNC(squash_msg, "SQUASH_MSG")
REPO_GIT_PATH_FUNC(merge_msg, "MERGE_MSG")
REPO_GIT_PATH_FUNC(merge_rr, "MERGE_RR")
REPO_GIT_PATH_FUNC(merge_mode, "MERGE_MODE")
REPO_GIT_PATH_FUNC(merge_head, "MERGE_HEAD")
REPO_GIT_PATH_FUNC(fetch_head, "FETCH_HEAD")
REPO_GIT_PATH_FUNC(shallow, "shallow")
