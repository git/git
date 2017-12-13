/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 *
 * This handles basic git sha1 object files - packing, unpacking,
 * creation etc.
 */
#include "cache.h"
#include "config.h"
#include "string-list.h"
#include "lockfile.h"
#include "delta.h"
#include "pack.h"
#include "blob.h"
#include "commit.h"
#include "run-command.h"
#include "tag.h"
#include "tree.h"
#include "tree-walk.h"
#include "refs.h"
#include "pack-revindex.h"
#include "sha1-lookup.h"
#include "bulk-checkin.h"
#include "streaming.h"
#include "dir.h"
#include "mru.h"
#include "list.h"
#include "mergesort.h"
#include "quote.h"
#include "packfile.h"

const unsigned char null_sha1[GIT_MAX_RAWSZ];
const struct object_id null_oid;
const struct object_id empty_tree_oid = {
	EMPTY_TREE_SHA1_BIN_LITERAL
};
const struct object_id empty_blob_oid = {
	EMPTY_BLOB_SHA1_BIN_LITERAL
};

static void git_hash_sha1_init(void *ctx)
{
	git_SHA1_Init((git_SHA_CTX *)ctx);
}

static void git_hash_sha1_update(void *ctx, const void *data, size_t len)
{
	git_SHA1_Update((git_SHA_CTX *)ctx, data, len);
}

static void git_hash_sha1_final(unsigned char *hash, void *ctx)
{
	git_SHA1_Final(hash, (git_SHA_CTX *)ctx);
}

static void git_hash_unknown_init(void *ctx)
{
	die("trying to init unknown hash");
}

static void git_hash_unknown_update(void *ctx, const void *data, size_t len)
{
	die("trying to update unknown hash");
}

static void git_hash_unknown_final(unsigned char *hash, void *ctx)
{
	die("trying to finalize unknown hash");
}

const struct git_hash_algo hash_algos[GIT_HASH_NALGOS] = {
	{
		NULL,
		0x00000000,
		0,
		0,
		0,
		git_hash_unknown_init,
		git_hash_unknown_update,
		git_hash_unknown_final,
		NULL,
		NULL,
	},
	{
		"sha-1",
		/* "sha1", big-endian */
		0x73686131,
		sizeof(git_SHA_CTX),
		GIT_SHA1_RAWSZ,
		GIT_SHA1_HEXSZ,
		git_hash_sha1_init,
		git_hash_sha1_update,
		git_hash_sha1_final,
		&empty_tree_oid,
		&empty_blob_oid,
	},
};

/*
 * This is meant to hold a *small* number of objects that you would
 * want read_sha1_file() to be able to return, but yet you do not want
 * to write them into the object store (e.g. a browse-only
 * application).
 */
static struct cached_object {
	unsigned char sha1[20];
	enum object_type type;
	void *buf;
	unsigned long size;
} *cached_objects;
static int cached_object_nr, cached_object_alloc;

static struct cached_object empty_tree = {
	EMPTY_TREE_SHA1_BIN_LITERAL,
	OBJ_TREE,
	"",
	0
};

static struct cached_object *find_cached_object(const unsigned char *sha1)
{
	int i;
	struct cached_object *co = cached_objects;

	for (i = 0; i < cached_object_nr; i++, co++) {
		if (!hashcmp(co->sha1, sha1))
			return co;
	}
	if (!hashcmp(sha1, empty_tree.sha1))
		return &empty_tree;
	return NULL;
}


static enum safe_crlf get_safe_crlf(unsigned flags)
{
	if (flags & HASH_RENORMALIZE)
		return SAFE_CRLF_RENORMALIZE;
	else if (flags & HASH_WRITE_OBJECT)
		return safe_crlf;
	else
		return SAFE_CRLF_FALSE;
}


int mkdir_in_gitdir(const char *path)
{
	if (mkdir(path, 0777)) {
		int saved_errno = errno;
		struct stat st;
		struct strbuf sb = STRBUF_INIT;

		if (errno != EEXIST)
			return -1;
		/*
		 * Are we looking at a path in a symlinked worktree
		 * whose original repository does not yet have it?
		 * e.g. .git/rr-cache pointing at its original
		 * repository in which the user hasn't performed any
		 * conflict resolution yet?
		 */
		if (lstat(path, &st) || !S_ISLNK(st.st_mode) ||
		    strbuf_readlink(&sb, path, st.st_size) ||
		    !is_absolute_path(sb.buf) ||
		    mkdir(sb.buf, 0777)) {
			strbuf_release(&sb);
			errno = saved_errno;
			return -1;
		}
		strbuf_release(&sb);
	}
	return adjust_shared_perm(path);
}

enum scld_error safe_create_leading_directories(char *path)
{
	char *next_component = path + offset_1st_component(path);
	enum scld_error ret = SCLD_OK;

	while (ret == SCLD_OK && next_component) {
		struct stat st;
		char *slash = next_component, slash_character;

		while (*slash && !is_dir_sep(*slash))
			slash++;

		if (!*slash)
			break;

		next_component = slash + 1;
		while (is_dir_sep(*next_component))
			next_component++;
		if (!*next_component)
			break;

		slash_character = *slash;
		*slash = '\0';
		if (!stat(path, &st)) {
			/* path exists */
			if (!S_ISDIR(st.st_mode)) {
				errno = ENOTDIR;
				ret = SCLD_EXISTS;
			}
		} else if (mkdir(path, 0777)) {
			if (errno == EEXIST &&
			    !stat(path, &st) && S_ISDIR(st.st_mode))
				; /* somebody created it since we checked */
			else if (errno == ENOENT)
				/*
				 * Either mkdir() failed because
				 * somebody just pruned the containing
				 * directory, or stat() failed because
				 * the file that was in our way was
				 * just removed.  Either way, inform
				 * the caller that it might be worth
				 * trying again:
				 */
				ret = SCLD_VANISHED;
			else
				ret = SCLD_FAILED;
		} else if (adjust_shared_perm(path)) {
			ret = SCLD_PERMS;
		}
		*slash = slash_character;
	}
	return ret;
}

enum scld_error safe_create_leading_directories_const(const char *path)
{
	int save_errno;
	/* path points to cache entries, so xstrdup before messing with it */
	char *buf = xstrdup(path);
	enum scld_error result = safe_create_leading_directories(buf);

	save_errno = errno;
	free(buf);
	errno = save_errno;
	return result;
}

int raceproof_create_file(const char *path, create_file_fn fn, void *cb)
{
	/*
	 * The number of times we will try to remove empty directories
	 * in the way of path. This is only 1 because if another
	 * process is racily creating directories that conflict with
	 * us, we don't want to fight against them.
	 */
	int remove_directories_remaining = 1;

	/*
	 * The number of times that we will try to create the
	 * directories containing path. We are willing to attempt this
	 * more than once, because another process could be trying to
	 * clean up empty directories at the same time as we are
	 * trying to create them.
	 */
	int create_directories_remaining = 3;

	/* A scratch copy of path, filled lazily if we need it: */
	struct strbuf path_copy = STRBUF_INIT;

	int ret, save_errno;

	/* Sanity check: */
	assert(*path);

retry_fn:
	ret = fn(path, cb);
	save_errno = errno;
	if (!ret)
		goto out;

	if (errno == EISDIR && remove_directories_remaining-- > 0) {
		/*
		 * A directory is in the way. Maybe it is empty; try
		 * to remove it:
		 */
		if (!path_copy.len)
			strbuf_addstr(&path_copy, path);

		if (!remove_dir_recursively(&path_copy, REMOVE_DIR_EMPTY_ONLY))
			goto retry_fn;
	} else if (errno == ENOENT && create_directories_remaining-- > 0) {
		/*
		 * Maybe the containing directory didn't exist, or
		 * maybe it was just deleted by a process that is
		 * racing with us to clean up empty directories. Try
		 * to create it:
		 */
		enum scld_error scld_result;

		if (!path_copy.len)
			strbuf_addstr(&path_copy, path);

		do {
			scld_result = safe_create_leading_directories(path_copy.buf);
			if (scld_result == SCLD_OK)
				goto retry_fn;
		} while (scld_result == SCLD_VANISHED && create_directories_remaining-- > 0);
	}

out:
	strbuf_release(&path_copy);
	errno = save_errno;
	return ret;
}

static void fill_sha1_path(struct strbuf *buf, const unsigned char *sha1)
{
	int i;
	for (i = 0; i < 20; i++) {
		static char hex[] = "0123456789abcdef";
		unsigned int val = sha1[i];
		strbuf_addch(buf, hex[val >> 4]);
		strbuf_addch(buf, hex[val & 0xf]);
		if (!i)
			strbuf_addch(buf, '/');
	}
}

const char *sha1_file_name(const unsigned char *sha1)
{
	static struct strbuf buf = STRBUF_INIT;

	strbuf_reset(&buf);
	strbuf_addf(&buf, "%s/", get_object_directory());

	fill_sha1_path(&buf, sha1);
	return buf.buf;
}

struct strbuf *alt_scratch_buf(struct alternate_object_database *alt)
{
	strbuf_setlen(&alt->scratch, alt->base_len);
	return &alt->scratch;
}

static const char *alt_sha1_path(struct alternate_object_database *alt,
				 const unsigned char *sha1)
{
	struct strbuf *buf = alt_scratch_buf(alt);
	fill_sha1_path(buf, sha1);
	return buf->buf;
}

struct alternate_object_database *alt_odb_list;
static struct alternate_object_database **alt_odb_tail;

/*
 * Return non-zero iff the path is usable as an alternate object database.
 */
static int alt_odb_usable(struct strbuf *path, const char *normalized_objdir)
{
	struct alternate_object_database *alt;

	/* Detect cases where alternate disappeared */
	if (!is_directory(path->buf)) {
		error("object directory %s does not exist; "
		      "check .git/objects/info/alternates.",
		      path->buf);
		return 0;
	}

	/*
	 * Prevent the common mistake of listing the same
	 * thing twice, or object directory itself.
	 */
	for (alt = alt_odb_list; alt; alt = alt->next) {
		if (!fspathcmp(path->buf, alt->path))
			return 0;
	}
	if (!fspathcmp(path->buf, normalized_objdir))
		return 0;

	return 1;
}

/*
 * Prepare alternate object database registry.
 *
 * The variable alt_odb_list points at the list of struct
 * alternate_object_database.  The elements on this list come from
 * non-empty elements from colon separated ALTERNATE_DB_ENVIRONMENT
 * environment variable, and $GIT_OBJECT_DIRECTORY/info/alternates,
 * whose contents is similar to that environment variable but can be
 * LF separated.  Its base points at a statically allocated buffer that
 * contains "/the/directory/corresponding/to/.git/objects/...", while
 * its name points just after the slash at the end of ".git/objects/"
 * in the example above, and has enough space to hold 40-byte hex
 * SHA1, an extra slash for the first level indirection, and the
 * terminating NUL.
 */
static void read_info_alternates(const char * relative_base, int depth);
static int link_alt_odb_entry(const char *entry, const char *relative_base,
	int depth, const char *normalized_objdir)
{
	struct alternate_object_database *ent;
	struct strbuf pathbuf = STRBUF_INIT;

	if (!is_absolute_path(entry) && relative_base) {
		strbuf_realpath(&pathbuf, relative_base, 1);
		strbuf_addch(&pathbuf, '/');
	}
	strbuf_addstr(&pathbuf, entry);

	if (strbuf_normalize_path(&pathbuf) < 0 && relative_base) {
		error("unable to normalize alternate object path: %s",
		      pathbuf.buf);
		strbuf_release(&pathbuf);
		return -1;
	}

	/*
	 * The trailing slash after the directory name is given by
	 * this function at the end. Remove duplicates.
	 */
	while (pathbuf.len && pathbuf.buf[pathbuf.len - 1] == '/')
		strbuf_setlen(&pathbuf, pathbuf.len - 1);

	if (!alt_odb_usable(&pathbuf, normalized_objdir)) {
		strbuf_release(&pathbuf);
		return -1;
	}

	ent = alloc_alt_odb(pathbuf.buf);

	/* add the alternate entry */
	*alt_odb_tail = ent;
	alt_odb_tail = &(ent->next);
	ent->next = NULL;

	/* recursively add alternates */
	read_info_alternates(pathbuf.buf, depth + 1);

	strbuf_release(&pathbuf);
	return 0;
}

static const char *parse_alt_odb_entry(const char *string,
				       int sep,
				       struct strbuf *out)
{
	const char *end;

	strbuf_reset(out);

	if (*string == '#') {
		/* comment; consume up to next separator */
		end = strchrnul(string, sep);
	} else if (*string == '"' && !unquote_c_style(out, string, &end)) {
		/*
		 * quoted path; unquote_c_style has copied the
		 * data for us and set "end". Broken quoting (e.g.,
		 * an entry that doesn't end with a quote) falls
		 * back to the unquoted case below.
		 */
	} else {
		/* normal, unquoted path */
		end = strchrnul(string, sep);
		strbuf_add(out, string, end - string);
	}

	if (*end)
		end++;
	return end;
}

static void link_alt_odb_entries(const char *alt, int sep,
				 const char *relative_base, int depth)
{
	struct strbuf objdirbuf = STRBUF_INIT;
	struct strbuf entry = STRBUF_INIT;

	if (!alt || !*alt)
		return;

	if (depth > 5) {
		error("%s: ignoring alternate object stores, nesting too deep.",
				relative_base);
		return;
	}

	strbuf_add_absolute_path(&objdirbuf, get_object_directory());
	if (strbuf_normalize_path(&objdirbuf) < 0)
		die("unable to normalize object directory: %s",
		    objdirbuf.buf);

	while (*alt) {
		alt = parse_alt_odb_entry(alt, sep, &entry);
		if (!entry.len)
			continue;
		link_alt_odb_entry(entry.buf, relative_base, depth, objdirbuf.buf);
	}
	strbuf_release(&entry);
	strbuf_release(&objdirbuf);
}

static void read_info_alternates(const char * relative_base, int depth)
{
	char *path;
	struct strbuf buf = STRBUF_INIT;

	path = xstrfmt("%s/info/alternates", relative_base);
	if (strbuf_read_file(&buf, path, 1024) < 0) {
		warn_on_fopen_errors(path);
		free(path);
		return;
	}

	link_alt_odb_entries(buf.buf, '\n', relative_base, depth);
	strbuf_release(&buf);
	free(path);
}

struct alternate_object_database *alloc_alt_odb(const char *dir)
{
	struct alternate_object_database *ent;

	FLEX_ALLOC_STR(ent, path, dir);
	strbuf_init(&ent->scratch, 0);
	strbuf_addf(&ent->scratch, "%s/", dir);
	ent->base_len = ent->scratch.len;

	return ent;
}

void add_to_alternates_file(const char *reference)
{
	struct lock_file lock = LOCK_INIT;
	char *alts = git_pathdup("objects/info/alternates");
	FILE *in, *out;
	int found = 0;

	hold_lock_file_for_update(&lock, alts, LOCK_DIE_ON_ERROR);
	out = fdopen_lock_file(&lock, "w");
	if (!out)
		die_errno("unable to fdopen alternates lockfile");

	in = fopen(alts, "r");
	if (in) {
		struct strbuf line = STRBUF_INIT;

		while (strbuf_getline(&line, in) != EOF) {
			if (!strcmp(reference, line.buf)) {
				found = 1;
				break;
			}
			fprintf_or_die(out, "%s\n", line.buf);
		}

		strbuf_release(&line);
		fclose(in);
	}
	else if (errno != ENOENT)
		die_errno("unable to read alternates file");

	if (found) {
		rollback_lock_file(&lock);
	} else {
		fprintf_or_die(out, "%s\n", reference);
		if (commit_lock_file(&lock))
			die_errno("unable to move new alternates file into place");
		if (alt_odb_tail)
			link_alt_odb_entries(reference, '\n', NULL, 0);
	}
	free(alts);
}

void add_to_alternates_memory(const char *reference)
{
	/*
	 * Make sure alternates are initialized, or else our entry may be
	 * overwritten when they are.
	 */
	prepare_alt_odb();

	link_alt_odb_entries(reference, '\n', NULL, 0);
}

/*
 * Compute the exact path an alternate is at and returns it. In case of
 * error NULL is returned and the human readable error is added to `err`
 * `path` may be relative and should point to $GITDIR.
 * `err` must not be null.
 */
char *compute_alternate_path(const char *path, struct strbuf *err)
{
	char *ref_git = NULL;
	const char *repo, *ref_git_s;
	int seen_error = 0;

	ref_git_s = real_path_if_valid(path);
	if (!ref_git_s) {
		seen_error = 1;
		strbuf_addf(err, _("path '%s' does not exist"), path);
		goto out;
	} else
		/*
		 * Beware: read_gitfile(), real_path() and mkpath()
		 * return static buffer
		 */
		ref_git = xstrdup(ref_git_s);

	repo = read_gitfile(ref_git);
	if (!repo)
		repo = read_gitfile(mkpath("%s/.git", ref_git));
	if (repo) {
		free(ref_git);
		ref_git = xstrdup(repo);
	}

	if (!repo && is_directory(mkpath("%s/.git/objects", ref_git))) {
		char *ref_git_git = mkpathdup("%s/.git", ref_git);
		free(ref_git);
		ref_git = ref_git_git;
	} else if (!is_directory(mkpath("%s/objects", ref_git))) {
		struct strbuf sb = STRBUF_INIT;
		seen_error = 1;
		if (get_common_dir(&sb, ref_git)) {
			strbuf_addf(err,
				    _("reference repository '%s' as a linked "
				      "checkout is not supported yet."),
				    path);
			goto out;
		}

		strbuf_addf(err, _("reference repository '%s' is not a "
					"local repository."), path);
		goto out;
	}

	if (!access(mkpath("%s/shallow", ref_git), F_OK)) {
		strbuf_addf(err, _("reference repository '%s' is shallow"),
			    path);
		seen_error = 1;
		goto out;
	}

	if (!access(mkpath("%s/info/grafts", ref_git), F_OK)) {
		strbuf_addf(err,
			    _("reference repository '%s' is grafted"),
			    path);
		seen_error = 1;
		goto out;
	}

out:
	if (seen_error) {
		FREE_AND_NULL(ref_git);
	}

	return ref_git;
}

int foreach_alt_odb(alt_odb_fn fn, void *cb)
{
	struct alternate_object_database *ent;
	int r = 0;

	prepare_alt_odb();
	for (ent = alt_odb_list; ent; ent = ent->next) {
		r = fn(ent, cb);
		if (r)
			break;
	}
	return r;
}

void prepare_alt_odb(void)
{
	const char *alt;

	if (alt_odb_tail)
		return;

	alt = getenv(ALTERNATE_DB_ENVIRONMENT);

	alt_odb_tail = &alt_odb_list;
	link_alt_odb_entries(alt, PATH_SEP, NULL, 0);

	read_info_alternates(get_object_directory(), 0);
}

/* Returns 1 if we have successfully freshened the file, 0 otherwise. */
static int freshen_file(const char *fn)
{
	struct utimbuf t;
	t.actime = t.modtime = time(NULL);
	return !utime(fn, &t);
}

/*
 * All of the check_and_freshen functions return 1 if the file exists and was
 * freshened (if freshening was requested), 0 otherwise. If they return
 * 0, you should not assume that it is safe to skip a write of the object (it
 * either does not exist on disk, or has a stale mtime and may be subject to
 * pruning).
 */
int check_and_freshen_file(const char *fn, int freshen)
{
	if (access(fn, F_OK))
		return 0;
	if (freshen && !freshen_file(fn))
		return 0;
	return 1;
}

static int check_and_freshen_local(const unsigned char *sha1, int freshen)
{
	return check_and_freshen_file(sha1_file_name(sha1), freshen);
}

static int check_and_freshen_nonlocal(const unsigned char *sha1, int freshen)
{
	struct alternate_object_database *alt;
	prepare_alt_odb();
	for (alt = alt_odb_list; alt; alt = alt->next) {
		const char *path = alt_sha1_path(alt, sha1);
		if (check_and_freshen_file(path, freshen))
			return 1;
	}
	return 0;
}

static int check_and_freshen(const unsigned char *sha1, int freshen)
{
	return check_and_freshen_local(sha1, freshen) ||
	       check_and_freshen_nonlocal(sha1, freshen);
}

int has_loose_object_nonlocal(const unsigned char *sha1)
{
	return check_and_freshen_nonlocal(sha1, 0);
}

static int has_loose_object(const unsigned char *sha1)
{
	return check_and_freshen(sha1, 0);
}

static void mmap_limit_check(size_t length)
{
	static size_t limit = 0;
	if (!limit) {
		limit = git_env_ulong("GIT_MMAP_LIMIT", 0);
		if (!limit)
			limit = SIZE_MAX;
	}
	if (length > limit)
		die("attempting to mmap %"PRIuMAX" over limit %"PRIuMAX,
		    (uintmax_t)length, (uintmax_t)limit);
}

void *xmmap_gently(void *start, size_t length,
		  int prot, int flags, int fd, off_t offset)
{
	void *ret;

	mmap_limit_check(length);
	ret = mmap(start, length, prot, flags, fd, offset);
	if (ret == MAP_FAILED) {
		if (!length)
			return NULL;
		release_pack_memory(length);
		ret = mmap(start, length, prot, flags, fd, offset);
	}
	return ret;
}

void *xmmap(void *start, size_t length,
	int prot, int flags, int fd, off_t offset)
{
	void *ret = xmmap_gently(start, length, prot, flags, fd, offset);
	if (ret == MAP_FAILED)
		die_errno("mmap failed");
	return ret;
}

/*
 * With an in-core object data in "map", rehash it to make sure the
 * object name actually matches "sha1" to detect object corruption.
 * With "map" == NULL, try reading the object named with "sha1" using
 * the streaming interface and rehash it to do the same.
 */
int check_sha1_signature(const unsigned char *sha1, void *map,
			 unsigned long size, const char *type)
{
	unsigned char real_sha1[20];
	enum object_type obj_type;
	struct git_istream *st;
	git_SHA_CTX c;
	char hdr[32];
	int hdrlen;

	if (map) {
		hash_sha1_file(map, size, type, real_sha1);
		return hashcmp(sha1, real_sha1) ? -1 : 0;
	}

	st = open_istream(sha1, &obj_type, &size, NULL);
	if (!st)
		return -1;

	/* Generate the header */
	hdrlen = xsnprintf(hdr, sizeof(hdr), "%s %lu", typename(obj_type), size) + 1;

	/* Sha1.. */
	git_SHA1_Init(&c);
	git_SHA1_Update(&c, hdr, hdrlen);
	for (;;) {
		char buf[1024 * 16];
		ssize_t readlen = read_istream(st, buf, sizeof(buf));

		if (readlen < 0) {
			close_istream(st);
			return -1;
		}
		if (!readlen)
			break;
		git_SHA1_Update(&c, buf, readlen);
	}
	git_SHA1_Final(real_sha1, &c);
	close_istream(st);
	return hashcmp(sha1, real_sha1) ? -1 : 0;
}

int git_open_cloexec(const char *name, int flags)
{
	int fd;
	static int o_cloexec = O_CLOEXEC;

	fd = open(name, flags | o_cloexec);
	if ((o_cloexec & O_CLOEXEC) && fd < 0 && errno == EINVAL) {
		/* Try again w/o O_CLOEXEC: the kernel might not support it */
		o_cloexec &= ~O_CLOEXEC;
		fd = open(name, flags | o_cloexec);
	}

#if defined(F_GETFD) && defined(F_SETFD) && defined(FD_CLOEXEC)
	{
		static int fd_cloexec = FD_CLOEXEC;

		if (!o_cloexec && 0 <= fd && fd_cloexec) {
			/* Opened w/o O_CLOEXEC?  try with fcntl(2) to add it */
			int flags = fcntl(fd, F_GETFD);
			if (fcntl(fd, F_SETFD, flags | fd_cloexec))
				fd_cloexec = 0;
		}
	}
#endif
	return fd;
}

/*
 * Find "sha1" as a loose object in the local repository or in an alternate.
 * Returns 0 on success, negative on failure.
 *
 * The "path" out-parameter will give the path of the object we found (if any).
 * Note that it may point to static storage and is only valid until another
 * call to sha1_file_name(), etc.
 */
static int stat_sha1_file(const unsigned char *sha1, struct stat *st,
			  const char **path)
{
	struct alternate_object_database *alt;

	*path = sha1_file_name(sha1);
	if (!lstat(*path, st))
		return 0;

	prepare_alt_odb();
	errno = ENOENT;
	for (alt = alt_odb_list; alt; alt = alt->next) {
		*path = alt_sha1_path(alt, sha1);
		if (!lstat(*path, st))
			return 0;
	}

	return -1;
}

/*
 * Like stat_sha1_file(), but actually open the object and return the
 * descriptor. See the caveats on the "path" parameter above.
 */
static int open_sha1_file(const unsigned char *sha1, const char **path)
{
	int fd;
	struct alternate_object_database *alt;
	int most_interesting_errno;

	*path = sha1_file_name(sha1);
	fd = git_open(*path);
	if (fd >= 0)
		return fd;
	most_interesting_errno = errno;

	prepare_alt_odb();
	for (alt = alt_odb_list; alt; alt = alt->next) {
		*path = alt_sha1_path(alt, sha1);
		fd = git_open(*path);
		if (fd >= 0)
			return fd;
		if (most_interesting_errno == ENOENT)
			most_interesting_errno = errno;
	}
	errno = most_interesting_errno;
	return -1;
}

/*
 * Map the loose object at "path" if it is not NULL, or the path found by
 * searching for a loose object named "sha1".
 */
static void *map_sha1_file_1(const char *path,
			     const unsigned char *sha1,
			     unsigned long *size)
{
	void *map;
	int fd;

	if (path)
		fd = git_open(path);
	else
		fd = open_sha1_file(sha1, &path);
	map = NULL;
	if (fd >= 0) {
		struct stat st;

		if (!fstat(fd, &st)) {
			*size = xsize_t(st.st_size);
			if (!*size) {
				/* mmap() is forbidden on empty files */
				error("object file %s is empty", path);
				return NULL;
			}
			map = xmmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0);
		}
		close(fd);
	}
	return map;
}

void *map_sha1_file(const unsigned char *sha1, unsigned long *size)
{
	return map_sha1_file_1(NULL, sha1, size);
}

static int unpack_sha1_short_header(git_zstream *stream,
				    unsigned char *map, unsigned long mapsize,
				    void *buffer, unsigned long bufsiz)
{
	/* Get the data stream */
	memset(stream, 0, sizeof(*stream));
	stream->next_in = map;
	stream->avail_in = mapsize;
	stream->next_out = buffer;
	stream->avail_out = bufsiz;

	git_inflate_init(stream);
	return git_inflate(stream, 0);
}

int unpack_sha1_header(git_zstream *stream,
		       unsigned char *map, unsigned long mapsize,
		       void *buffer, unsigned long bufsiz)
{
	int status = unpack_sha1_short_header(stream, map, mapsize,
					      buffer, bufsiz);

	if (status < Z_OK)
		return status;

	/* Make sure we have the terminating NUL */
	if (!memchr(buffer, '\0', stream->next_out - (unsigned char *)buffer))
		return -1;
	return 0;
}

static int unpack_sha1_header_to_strbuf(git_zstream *stream, unsigned char *map,
					unsigned long mapsize, void *buffer,
					unsigned long bufsiz, struct strbuf *header)
{
	int status;

	status = unpack_sha1_short_header(stream, map, mapsize, buffer, bufsiz);
	if (status < Z_OK)
		return -1;

	/*
	 * Check if entire header is unpacked in the first iteration.
	 */
	if (memchr(buffer, '\0', stream->next_out - (unsigned char *)buffer))
		return 0;

	/*
	 * buffer[0..bufsiz] was not large enough.  Copy the partial
	 * result out to header, and then append the result of further
	 * reading the stream.
	 */
	strbuf_add(header, buffer, stream->next_out - (unsigned char *)buffer);
	stream->next_out = buffer;
	stream->avail_out = bufsiz;

	do {
		status = git_inflate(stream, 0);
		strbuf_add(header, buffer, stream->next_out - (unsigned char *)buffer);
		if (memchr(buffer, '\0', stream->next_out - (unsigned char *)buffer))
			return 0;
		stream->next_out = buffer;
		stream->avail_out = bufsiz;
	} while (status != Z_STREAM_END);
	return -1;
}

static void *unpack_sha1_rest(git_zstream *stream, void *buffer, unsigned long size, const unsigned char *sha1)
{
	int bytes = strlen(buffer) + 1;
	unsigned char *buf = xmallocz(size);
	unsigned long n;
	int status = Z_OK;

	n = stream->total_out - bytes;
	if (n > size)
		n = size;
	memcpy(buf, (char *) buffer + bytes, n);
	bytes = n;
	if (bytes <= size) {
		/*
		 * The above condition must be (bytes <= size), not
		 * (bytes < size).  In other words, even though we
		 * expect no more output and set avail_out to zero,
		 * the input zlib stream may have bytes that express
		 * "this concludes the stream", and we *do* want to
		 * eat that input.
		 *
		 * Otherwise we would not be able to test that we
		 * consumed all the input to reach the expected size;
		 * we also want to check that zlib tells us that all
		 * went well with status == Z_STREAM_END at the end.
		 */
		stream->next_out = buf + bytes;
		stream->avail_out = size - bytes;
		while (status == Z_OK)
			status = git_inflate(stream, Z_FINISH);
	}
	if (status == Z_STREAM_END && !stream->avail_in) {
		git_inflate_end(stream);
		return buf;
	}

	if (status < 0)
		error("corrupt loose object '%s'", sha1_to_hex(sha1));
	else if (stream->avail_in)
		error("garbage at end of loose object '%s'",
		      sha1_to_hex(sha1));
	free(buf);
	return NULL;
}

/*
 * We used to just use "sscanf()", but that's actually way
 * too permissive for what we want to check. So do an anal
 * object header parse by hand.
 */
static int parse_sha1_header_extended(const char *hdr, struct object_info *oi,
			       unsigned int flags)
{
	const char *type_buf = hdr;
	unsigned long size;
	int type, type_len = 0;

	/*
	 * The type can be of any size but is followed by
	 * a space.
	 */
	for (;;) {
		char c = *hdr++;
		if (!c)
			return -1;
		if (c == ' ')
			break;
		type_len++;
	}

	type = type_from_string_gently(type_buf, type_len, 1);
	if (oi->typename)
		strbuf_add(oi->typename, type_buf, type_len);
	/*
	 * Set type to 0 if its an unknown object and
	 * we're obtaining the type using '--allow-unknown-type'
	 * option.
	 */
	if ((flags & OBJECT_INFO_ALLOW_UNKNOWN_TYPE) && (type < 0))
		type = 0;
	else if (type < 0)
		die("invalid object type");
	if (oi->typep)
		*oi->typep = type;

	/*
	 * The length must follow immediately, and be in canonical
	 * decimal format (ie "010" is not valid).
	 */
	size = *hdr++ - '0';
	if (size > 9)
		return -1;
	if (size) {
		for (;;) {
			unsigned long c = *hdr - '0';
			if (c > 9)
				break;
			hdr++;
			size = size * 10 + c;
		}
	}

	if (oi->sizep)
		*oi->sizep = size;

	/*
	 * The length must be followed by a zero byte
	 */
	return *hdr ? -1 : type;
}

int parse_sha1_header(const char *hdr, unsigned long *sizep)
{
	struct object_info oi = OBJECT_INFO_INIT;

	oi.sizep = sizep;
	return parse_sha1_header_extended(hdr, &oi, 0);
}

static int sha1_loose_object_info(const unsigned char *sha1,
				  struct object_info *oi,
				  int flags)
{
	int status = 0;
	unsigned long mapsize;
	void *map;
	git_zstream stream;
	char hdr[32];
	struct strbuf hdrbuf = STRBUF_INIT;
	unsigned long size_scratch;

	if (oi->delta_base_sha1)
		hashclr(oi->delta_base_sha1);

	/*
	 * If we don't care about type or size, then we don't
	 * need to look inside the object at all. Note that we
	 * do not optimize out the stat call, even if the
	 * caller doesn't care about the disk-size, since our
	 * return value implicitly indicates whether the
	 * object even exists.
	 */
	if (!oi->typep && !oi->typename && !oi->sizep && !oi->contentp) {
		const char *path;
		struct stat st;
		if (stat_sha1_file(sha1, &st, &path) < 0)
			return -1;
		if (oi->disk_sizep)
			*oi->disk_sizep = st.st_size;
		return 0;
	}

	map = map_sha1_file(sha1, &mapsize);
	if (!map)
		return -1;

	if (!oi->sizep)
		oi->sizep = &size_scratch;

	if (oi->disk_sizep)
		*oi->disk_sizep = mapsize;
	if ((flags & OBJECT_INFO_ALLOW_UNKNOWN_TYPE)) {
		if (unpack_sha1_header_to_strbuf(&stream, map, mapsize, hdr, sizeof(hdr), &hdrbuf) < 0)
			status = error("unable to unpack %s header with --allow-unknown-type",
				       sha1_to_hex(sha1));
	} else if (unpack_sha1_header(&stream, map, mapsize, hdr, sizeof(hdr)) < 0)
		status = error("unable to unpack %s header",
			       sha1_to_hex(sha1));
	if (status < 0)
		; /* Do nothing */
	else if (hdrbuf.len) {
		if ((status = parse_sha1_header_extended(hdrbuf.buf, oi, flags)) < 0)
			status = error("unable to parse %s header with --allow-unknown-type",
				       sha1_to_hex(sha1));
	} else if ((status = parse_sha1_header_extended(hdr, oi, flags)) < 0)
		status = error("unable to parse %s header", sha1_to_hex(sha1));

	if (status >= 0 && oi->contentp) {
		*oi->contentp = unpack_sha1_rest(&stream, hdr,
						 *oi->sizep, sha1);
		if (!*oi->contentp) {
			git_inflate_end(&stream);
			status = -1;
		}
	} else
		git_inflate_end(&stream);

	munmap(map, mapsize);
	if (status && oi->typep)
		*oi->typep = status;
	if (oi->sizep == &size_scratch)
		oi->sizep = NULL;
	strbuf_release(&hdrbuf);
	oi->whence = OI_LOOSE;
	return (status < 0) ? status : 0;
}

int sha1_object_info_extended(const unsigned char *sha1, struct object_info *oi, unsigned flags)
{
	static struct object_info blank_oi = OBJECT_INFO_INIT;
	struct pack_entry e;
	int rtype;
	const unsigned char *real = (flags & OBJECT_INFO_LOOKUP_REPLACE) ?
				    lookup_replace_object(sha1) :
				    sha1;

	if (is_null_sha1(real))
		return -1;

	if (!oi)
		oi = &blank_oi;

	if (!(flags & OBJECT_INFO_SKIP_CACHED)) {
		struct cached_object *co = find_cached_object(real);
		if (co) {
			if (oi->typep)
				*(oi->typep) = co->type;
			if (oi->sizep)
				*(oi->sizep) = co->size;
			if (oi->disk_sizep)
				*(oi->disk_sizep) = 0;
			if (oi->delta_base_sha1)
				hashclr(oi->delta_base_sha1);
			if (oi->typename)
				strbuf_addstr(oi->typename, typename(co->type));
			if (oi->contentp)
				*oi->contentp = xmemdupz(co->buf, co->size);
			oi->whence = OI_CACHED;
			return 0;
		}
	}

	if (!find_pack_entry(real, &e)) {
		/* Most likely it's a loose object. */
		if (!sha1_loose_object_info(real, oi, flags))
			return 0;

		/* Not a loose object; someone else may have just packed it. */
		if (flags & OBJECT_INFO_QUICK) {
			return -1;
		} else {
			reprepare_packed_git();
			if (!find_pack_entry(real, &e))
				return -1;
		}
	}

	if (oi == &blank_oi)
		/*
		 * We know that the caller doesn't actually need the
		 * information below, so return early.
		 */
		return 0;

	rtype = packed_object_info(e.p, e.offset, oi);
	if (rtype < 0) {
		mark_bad_packed_object(e.p, real);
		return sha1_object_info_extended(real, oi, 0);
	} else if (oi->whence == OI_PACKED) {
		oi->u.packed.offset = e.offset;
		oi->u.packed.pack = e.p;
		oi->u.packed.is_delta = (rtype == OBJ_REF_DELTA ||
					 rtype == OBJ_OFS_DELTA);
	}

	return 0;
}

/* returns enum object_type or negative */
int sha1_object_info(const unsigned char *sha1, unsigned long *sizep)
{
	enum object_type type;
	struct object_info oi = OBJECT_INFO_INIT;

	oi.typep = &type;
	oi.sizep = sizep;
	if (sha1_object_info_extended(sha1, &oi,
				      OBJECT_INFO_LOOKUP_REPLACE) < 0)
		return -1;
	return type;
}

static void *read_object(const unsigned char *sha1, enum object_type *type,
			 unsigned long *size)
{
	struct object_info oi = OBJECT_INFO_INIT;
	void *content;
	oi.typep = type;
	oi.sizep = size;
	oi.contentp = &content;

	if (sha1_object_info_extended(sha1, &oi, 0) < 0)
		return NULL;
	return content;
}

int pretend_sha1_file(void *buf, unsigned long len, enum object_type type,
		      unsigned char *sha1)
{
	struct cached_object *co;

	hash_sha1_file(buf, len, typename(type), sha1);
	if (has_sha1_file(sha1) || find_cached_object(sha1))
		return 0;
	ALLOC_GROW(cached_objects, cached_object_nr + 1, cached_object_alloc);
	co = &cached_objects[cached_object_nr++];
	co->size = len;
	co->type = type;
	co->buf = xmalloc(len);
	memcpy(co->buf, buf, len);
	hashcpy(co->sha1, sha1);
	return 0;
}

/*
 * This function dies on corrupt objects; the callers who want to
 * deal with them should arrange to call read_object() and give error
 * messages themselves.
 */
void *read_sha1_file_extended(const unsigned char *sha1,
			      enum object_type *type,
			      unsigned long *size,
			      int lookup_replace)
{
	void *data;
	const struct packed_git *p;
	const char *path;
	struct stat st;
	const unsigned char *repl = lookup_replace ? lookup_replace_object(sha1)
						   : sha1;

	errno = 0;
	data = read_object(repl, type, size);
	if (data)
		return data;

	if (errno && errno != ENOENT)
		die_errno("failed to read object %s", sha1_to_hex(sha1));

	/* die if we replaced an object with one that does not exist */
	if (repl != sha1)
		die("replacement %s not found for %s",
		    sha1_to_hex(repl), sha1_to_hex(sha1));

	if (!stat_sha1_file(repl, &st, &path))
		die("loose object %s (stored in %s) is corrupt",
		    sha1_to_hex(repl), path);

	if ((p = has_packed_and_bad(repl)) != NULL)
		die("packed object %s (stored in %s) is corrupt",
		    sha1_to_hex(repl), p->pack_name);

	return NULL;
}

void *read_object_with_reference(const unsigned char *sha1,
				 const char *required_type_name,
				 unsigned long *size,
				 unsigned char *actual_sha1_return)
{
	enum object_type type, required_type;
	void *buffer;
	unsigned long isize;
	unsigned char actual_sha1[20];

	required_type = type_from_string(required_type_name);
	hashcpy(actual_sha1, sha1);
	while (1) {
		int ref_length = -1;
		const char *ref_type = NULL;

		buffer = read_sha1_file(actual_sha1, &type, &isize);
		if (!buffer)
			return NULL;
		if (type == required_type) {
			*size = isize;
			if (actual_sha1_return)
				hashcpy(actual_sha1_return, actual_sha1);
			return buffer;
		}
		/* Handle references */
		else if (type == OBJ_COMMIT)
			ref_type = "tree ";
		else if (type == OBJ_TAG)
			ref_type = "object ";
		else {
			free(buffer);
			return NULL;
		}
		ref_length = strlen(ref_type);

		if (ref_length + 40 > isize ||
		    memcmp(buffer, ref_type, ref_length) ||
		    get_sha1_hex((char *) buffer + ref_length, actual_sha1)) {
			free(buffer);
			return NULL;
		}
		free(buffer);
		/* Now we have the ID of the referred-to object in
		 * actual_sha1.  Check again. */
	}
}

static void write_sha1_file_prepare(const void *buf, unsigned long len,
                                    const char *type, unsigned char *sha1,
                                    char *hdr, int *hdrlen)
{
	git_SHA_CTX c;

	/* Generate the header */
	*hdrlen = xsnprintf(hdr, *hdrlen, "%s %lu", type, len)+1;

	/* Sha1.. */
	git_SHA1_Init(&c);
	git_SHA1_Update(&c, hdr, *hdrlen);
	git_SHA1_Update(&c, buf, len);
	git_SHA1_Final(sha1, &c);
}

/*
 * Move the just written object into its final resting place.
 */
int finalize_object_file(const char *tmpfile, const char *filename)
{
	int ret = 0;

	if (object_creation_mode == OBJECT_CREATION_USES_RENAMES)
		goto try_rename;
	else if (link(tmpfile, filename))
		ret = errno;

	/*
	 * Coda hack - coda doesn't like cross-directory links,
	 * so we fall back to a rename, which will mean that it
	 * won't be able to check collisions, but that's not a
	 * big deal.
	 *
	 * The same holds for FAT formatted media.
	 *
	 * When this succeeds, we just return.  We have nothing
	 * left to unlink.
	 */
	if (ret && ret != EEXIST) {
	try_rename:
		if (!rename(tmpfile, filename))
			goto out;
		ret = errno;
	}
	unlink_or_warn(tmpfile);
	if (ret) {
		if (ret != EEXIST) {
			return error_errno("unable to write sha1 filename %s", filename);
		}
		/* FIXME!!! Collision check here ? */
	}

out:
	if (adjust_shared_perm(filename))
		return error("unable to set permission to '%s'", filename);
	return 0;
}

static int write_buffer(int fd, const void *buf, size_t len)
{
	if (write_in_full(fd, buf, len) < 0)
		return error_errno("file write error");
	return 0;
}

int hash_sha1_file(const void *buf, unsigned long len, const char *type,
                   unsigned char *sha1)
{
	char hdr[32];
	int hdrlen = sizeof(hdr);
	write_sha1_file_prepare(buf, len, type, sha1, hdr, &hdrlen);
	return 0;
}

/* Finalize a file on disk, and close it. */
static void close_sha1_file(int fd)
{
	if (fsync_object_files)
		fsync_or_die(fd, "sha1 file");
	if (close(fd) != 0)
		die_errno("error when closing sha1 file");
}

/* Size of directory component, including the ending '/' */
static inline int directory_size(const char *filename)
{
	const char *s = strrchr(filename, '/');
	if (!s)
		return 0;
	return s - filename + 1;
}

/*
 * This creates a temporary file in the same directory as the final
 * 'filename'
 *
 * We want to avoid cross-directory filename renames, because those
 * can have problems on various filesystems (FAT, NFS, Coda).
 */
static int create_tmpfile(struct strbuf *tmp, const char *filename)
{
	int fd, dirlen = directory_size(filename);

	strbuf_reset(tmp);
	strbuf_add(tmp, filename, dirlen);
	strbuf_addstr(tmp, "tmp_obj_XXXXXX");
	fd = git_mkstemp_mode(tmp->buf, 0444);
	if (fd < 0 && dirlen && errno == ENOENT) {
		/*
		 * Make sure the directory exists; note that the contents
		 * of the buffer are undefined after mkstemp returns an
		 * error, so we have to rewrite the whole buffer from
		 * scratch.
		 */
		strbuf_reset(tmp);
		strbuf_add(tmp, filename, dirlen - 1);
		if (mkdir(tmp->buf, 0777) && errno != EEXIST)
			return -1;
		if (adjust_shared_perm(tmp->buf))
			return -1;

		/* Try again */
		strbuf_addstr(tmp, "/tmp_obj_XXXXXX");
		fd = git_mkstemp_mode(tmp->buf, 0444);
	}
	return fd;
}

static int write_loose_object(const unsigned char *sha1, char *hdr, int hdrlen,
			      const void *buf, unsigned long len, time_t mtime)
{
	int fd, ret;
	unsigned char compressed[4096];
	git_zstream stream;
	git_SHA_CTX c;
	unsigned char parano_sha1[20];
	static struct strbuf tmp_file = STRBUF_INIT;
	const char *filename = sha1_file_name(sha1);

	fd = create_tmpfile(&tmp_file, filename);
	if (fd < 0) {
		if (errno == EACCES)
			return error("insufficient permission for adding an object to repository database %s", get_object_directory());
		else
			return error_errno("unable to create temporary file");
	}

	/* Set it up */
	git_deflate_init(&stream, zlib_compression_level);
	stream.next_out = compressed;
	stream.avail_out = sizeof(compressed);
	git_SHA1_Init(&c);

	/* First header.. */
	stream.next_in = (unsigned char *)hdr;
	stream.avail_in = hdrlen;
	while (git_deflate(&stream, 0) == Z_OK)
		; /* nothing */
	git_SHA1_Update(&c, hdr, hdrlen);

	/* Then the data itself.. */
	stream.next_in = (void *)buf;
	stream.avail_in = len;
	do {
		unsigned char *in0 = stream.next_in;
		ret = git_deflate(&stream, Z_FINISH);
		git_SHA1_Update(&c, in0, stream.next_in - in0);
		if (write_buffer(fd, compressed, stream.next_out - compressed) < 0)
			die("unable to write sha1 file");
		stream.next_out = compressed;
		stream.avail_out = sizeof(compressed);
	} while (ret == Z_OK);

	if (ret != Z_STREAM_END)
		die("unable to deflate new object %s (%d)", sha1_to_hex(sha1), ret);
	ret = git_deflate_end_gently(&stream);
	if (ret != Z_OK)
		die("deflateEnd on object %s failed (%d)", sha1_to_hex(sha1), ret);
	git_SHA1_Final(parano_sha1, &c);
	if (hashcmp(sha1, parano_sha1) != 0)
		die("confused by unstable object source data for %s", sha1_to_hex(sha1));

	close_sha1_file(fd);

	if (mtime) {
		struct utimbuf utb;
		utb.actime = mtime;
		utb.modtime = mtime;
		if (utime(tmp_file.buf, &utb) < 0)
			warning_errno("failed utime() on %s", tmp_file.buf);
	}

	return finalize_object_file(tmp_file.buf, filename);
}

static int freshen_loose_object(const unsigned char *sha1)
{
	return check_and_freshen(sha1, 1);
}

static int freshen_packed_object(const unsigned char *sha1)
{
	struct pack_entry e;
	if (!find_pack_entry(sha1, &e))
		return 0;
	if (e.p->freshened)
		return 1;
	if (!freshen_file(e.p->pack_name))
		return 0;
	e.p->freshened = 1;
	return 1;
}

int write_sha1_file(const void *buf, unsigned long len, const char *type, unsigned char *sha1)
{
	char hdr[32];
	int hdrlen = sizeof(hdr);

	/* Normally if we have it in the pack then we do not bother writing
	 * it out into .git/objects/??/?{38} file.
	 */
	write_sha1_file_prepare(buf, len, type, sha1, hdr, &hdrlen);
	if (freshen_packed_object(sha1) || freshen_loose_object(sha1))
		return 0;
	return write_loose_object(sha1, hdr, hdrlen, buf, len, 0);
}

int hash_sha1_file_literally(const void *buf, unsigned long len, const char *type,
			     struct object_id *oid, unsigned flags)
{
	char *header;
	int hdrlen, status = 0;

	/* type string, SP, %lu of the length plus NUL must fit this */
	hdrlen = strlen(type) + 32;
	header = xmalloc(hdrlen);
	write_sha1_file_prepare(buf, len, type, oid->hash, header, &hdrlen);

	if (!(flags & HASH_WRITE_OBJECT))
		goto cleanup;
	if (freshen_packed_object(oid->hash) || freshen_loose_object(oid->hash))
		goto cleanup;
	status = write_loose_object(oid->hash, header, hdrlen, buf, len, 0);

cleanup:
	free(header);
	return status;
}

int force_object_loose(const unsigned char *sha1, time_t mtime)
{
	void *buf;
	unsigned long len;
	enum object_type type;
	char hdr[32];
	int hdrlen;
	int ret;

	if (has_loose_object(sha1))
		return 0;
	buf = read_object(sha1, &type, &len);
	if (!buf)
		return error("cannot read sha1_file for %s", sha1_to_hex(sha1));
	hdrlen = xsnprintf(hdr, sizeof(hdr), "%s %lu", typename(type), len) + 1;
	ret = write_loose_object(sha1, hdr, hdrlen, buf, len, mtime);
	free(buf);

	return ret;
}

int has_sha1_file_with_flags(const unsigned char *sha1, int flags)
{
	if (!startup_info->have_repository)
		return 0;
	return sha1_object_info_extended(sha1, NULL,
					 flags | OBJECT_INFO_SKIP_CACHED) >= 0;
}

int has_object_file(const struct object_id *oid)
{
	return has_sha1_file(oid->hash);
}

int has_object_file_with_flags(const struct object_id *oid, int flags)
{
	return has_sha1_file_with_flags(oid->hash, flags);
}

static void check_tree(const void *buf, size_t size)
{
	struct tree_desc desc;
	struct name_entry entry;

	init_tree_desc(&desc, buf, size);
	while (tree_entry(&desc, &entry))
		/* do nothing
		 * tree_entry() will die() on malformed entries */
		;
}

static void check_commit(const void *buf, size_t size)
{
	struct commit c;
	memset(&c, 0, sizeof(c));
	if (parse_commit_buffer(&c, buf, size))
		die("corrupt commit");
}

static void check_tag(const void *buf, size_t size)
{
	struct tag t;
	memset(&t, 0, sizeof(t));
	if (parse_tag_buffer(&t, buf, size))
		die("corrupt tag");
}

static int index_mem(struct object_id *oid, void *buf, size_t size,
		     enum object_type type,
		     const char *path, unsigned flags)
{
	int ret, re_allocated = 0;
	int write_object = flags & HASH_WRITE_OBJECT;

	if (!type)
		type = OBJ_BLOB;

	/*
	 * Convert blobs to git internal format
	 */
	if ((type == OBJ_BLOB) && path) {
		struct strbuf nbuf = STRBUF_INIT;
		if (convert_to_git(&the_index, path, buf, size, &nbuf,
				   get_safe_crlf(flags))) {
			buf = strbuf_detach(&nbuf, &size);
			re_allocated = 1;
		}
	}
	if (flags & HASH_FORMAT_CHECK) {
		if (type == OBJ_TREE)
			check_tree(buf, size);
		if (type == OBJ_COMMIT)
			check_commit(buf, size);
		if (type == OBJ_TAG)
			check_tag(buf, size);
	}

	if (write_object)
		ret = write_sha1_file(buf, size, typename(type), oid->hash);
	else
		ret = hash_sha1_file(buf, size, typename(type), oid->hash);
	if (re_allocated)
		free(buf);
	return ret;
}

static int index_stream_convert_blob(struct object_id *oid, int fd,
				     const char *path, unsigned flags)
{
	int ret;
	const int write_object = flags & HASH_WRITE_OBJECT;
	struct strbuf sbuf = STRBUF_INIT;

	assert(path);
	assert(would_convert_to_git_filter_fd(path));

	convert_to_git_filter_fd(&the_index, path, fd, &sbuf,
				 get_safe_crlf(flags));

	if (write_object)
		ret = write_sha1_file(sbuf.buf, sbuf.len, typename(OBJ_BLOB),
				      oid->hash);
	else
		ret = hash_sha1_file(sbuf.buf, sbuf.len, typename(OBJ_BLOB),
				     oid->hash);
	strbuf_release(&sbuf);
	return ret;
}

static int index_pipe(struct object_id *oid, int fd, enum object_type type,
		      const char *path, unsigned flags)
{
	struct strbuf sbuf = STRBUF_INIT;
	int ret;

	if (strbuf_read(&sbuf, fd, 4096) >= 0)
		ret = index_mem(oid, sbuf.buf, sbuf.len, type, path, flags);
	else
		ret = -1;
	strbuf_release(&sbuf);
	return ret;
}

#define SMALL_FILE_SIZE (32*1024)

static int index_core(struct object_id *oid, int fd, size_t size,
		      enum object_type type, const char *path,
		      unsigned flags)
{
	int ret;

	if (!size) {
		ret = index_mem(oid, "", size, type, path, flags);
	} else if (size <= SMALL_FILE_SIZE) {
		char *buf = xmalloc(size);
		ssize_t read_result = read_in_full(fd, buf, size);
		if (read_result < 0)
			ret = error_errno("read error while indexing %s",
					  path ? path : "<unknown>");
		else if (read_result != size)
			ret = error("short read while indexing %s",
				    path ? path : "<unknown>");
		else
			ret = index_mem(oid, buf, size, type, path, flags);
		free(buf);
	} else {
		void *buf = xmmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
		ret = index_mem(oid, buf, size, type, path, flags);
		munmap(buf, size);
	}
	return ret;
}

/*
 * This creates one packfile per large blob unless bulk-checkin
 * machinery is "plugged".
 *
 * This also bypasses the usual "convert-to-git" dance, and that is on
 * purpose. We could write a streaming version of the converting
 * functions and insert that before feeding the data to fast-import
 * (or equivalent in-core API described above). However, that is
 * somewhat complicated, as we do not know the size of the filter
 * result, which we need to know beforehand when writing a git object.
 * Since the primary motivation for trying to stream from the working
 * tree file and to avoid mmaping it in core is to deal with large
 * binary blobs, they generally do not want to get any conversion, and
 * callers should avoid this code path when filters are requested.
 */
static int index_stream(struct object_id *oid, int fd, size_t size,
			enum object_type type, const char *path,
			unsigned flags)
{
	return index_bulk_checkin(oid->hash, fd, size, type, path, flags);
}

int index_fd(struct object_id *oid, int fd, struct stat *st,
	     enum object_type type, const char *path, unsigned flags)
{
	int ret;

	/*
	 * Call xsize_t() only when needed to avoid potentially unnecessary
	 * die() for large files.
	 */
	if (type == OBJ_BLOB && path && would_convert_to_git_filter_fd(path))
		ret = index_stream_convert_blob(oid, fd, path, flags);
	else if (!S_ISREG(st->st_mode))
		ret = index_pipe(oid, fd, type, path, flags);
	else if (st->st_size <= big_file_threshold || type != OBJ_BLOB ||
		 (path && would_convert_to_git(&the_index, path)))
		ret = index_core(oid, fd, xsize_t(st->st_size), type, path,
				 flags);
	else
		ret = index_stream(oid, fd, xsize_t(st->st_size), type, path,
				   flags);
	close(fd);
	return ret;
}

int index_path(struct object_id *oid, const char *path, struct stat *st, unsigned flags)
{
	int fd;
	struct strbuf sb = STRBUF_INIT;
	int rc = 0;

	switch (st->st_mode & S_IFMT) {
	case S_IFREG:
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return error_errno("open(\"%s\")", path);
		if (index_fd(oid, fd, st, OBJ_BLOB, path, flags) < 0)
			return error("%s: failed to insert into database",
				     path);
		break;
	case S_IFLNK:
		if (strbuf_readlink(&sb, path, st->st_size))
			return error_errno("readlink(\"%s\")", path);
		if (!(flags & HASH_WRITE_OBJECT))
			hash_sha1_file(sb.buf, sb.len, blob_type, oid->hash);
		else if (write_sha1_file(sb.buf, sb.len, blob_type, oid->hash))
			rc = error("%s: failed to insert into database", path);
		strbuf_release(&sb);
		break;
	case S_IFDIR:
		return resolve_gitlink_ref(path, "HEAD", oid);
	default:
		return error("%s: unsupported file type", path);
	}
	return rc;
}

int read_pack_header(int fd, struct pack_header *header)
{
	if (read_in_full(fd, header, sizeof(*header)) != sizeof(*header))
		/* "eof before pack header was fully read" */
		return PH_ERROR_EOF;

	if (header->hdr_signature != htonl(PACK_SIGNATURE))
		/* "protocol error (pack signature mismatch detected)" */
		return PH_ERROR_PACK_SIGNATURE;
	if (!pack_version_ok(header->hdr_version))
		/* "protocol error (pack version unsupported)" */
		return PH_ERROR_PROTOCOL;
	return 0;
}

void assert_sha1_type(const unsigned char *sha1, enum object_type expect)
{
	enum object_type type = sha1_object_info(sha1, NULL);
	if (type < 0)
		die("%s is not a valid object", sha1_to_hex(sha1));
	if (type != expect)
		die("%s is not a valid '%s' object", sha1_to_hex(sha1),
		    typename(expect));
}

int for_each_file_in_obj_subdir(unsigned int subdir_nr,
				struct strbuf *path,
				each_loose_object_fn obj_cb,
				each_loose_cruft_fn cruft_cb,
				each_loose_subdir_fn subdir_cb,
				void *data)
{
	size_t origlen, baselen;
	DIR *dir;
	struct dirent *de;
	int r = 0;
	struct object_id oid;

	if (subdir_nr > 0xff)
		BUG("invalid loose object subdirectory: %x", subdir_nr);

	origlen = path->len;
	strbuf_complete(path, '/');
	strbuf_addf(path, "%02x", subdir_nr);
	baselen = path->len;

	dir = opendir(path->buf);
	if (!dir) {
		if (errno != ENOENT)
			r = error_errno("unable to open %s", path->buf);
		strbuf_setlen(path, origlen);
		return r;
	}

	oid.hash[0] = subdir_nr;

	while ((de = readdir(dir))) {
		if (is_dot_or_dotdot(de->d_name))
			continue;

		strbuf_setlen(path, baselen);
		strbuf_addf(path, "/%s", de->d_name);

		if (strlen(de->d_name) == GIT_SHA1_HEXSZ - 2 &&
		    !hex_to_bytes(oid.hash + 1, de->d_name,
				  GIT_SHA1_RAWSZ - 1)) {
			if (obj_cb) {
				r = obj_cb(&oid, path->buf, data);
				if (r)
					break;
			}
			continue;
		}

		if (cruft_cb) {
			r = cruft_cb(de->d_name, path->buf, data);
			if (r)
				break;
		}
	}
	closedir(dir);

	strbuf_setlen(path, baselen);
	if (!r && subdir_cb)
		r = subdir_cb(subdir_nr, path->buf, data);

	strbuf_setlen(path, origlen);

	return r;
}

int for_each_loose_file_in_objdir_buf(struct strbuf *path,
			    each_loose_object_fn obj_cb,
			    each_loose_cruft_fn cruft_cb,
			    each_loose_subdir_fn subdir_cb,
			    void *data)
{
	int r = 0;
	int i;

	for (i = 0; i < 256; i++) {
		r = for_each_file_in_obj_subdir(i, path, obj_cb, cruft_cb,
						subdir_cb, data);
		if (r)
			break;
	}

	return r;
}

int for_each_loose_file_in_objdir(const char *path,
				  each_loose_object_fn obj_cb,
				  each_loose_cruft_fn cruft_cb,
				  each_loose_subdir_fn subdir_cb,
				  void *data)
{
	struct strbuf buf = STRBUF_INIT;
	int r;

	strbuf_addstr(&buf, path);
	r = for_each_loose_file_in_objdir_buf(&buf, obj_cb, cruft_cb,
					      subdir_cb, data);
	strbuf_release(&buf);

	return r;
}

struct loose_alt_odb_data {
	each_loose_object_fn *cb;
	void *data;
};

static int loose_from_alt_odb(struct alternate_object_database *alt,
			      void *vdata)
{
	struct loose_alt_odb_data *data = vdata;
	struct strbuf buf = STRBUF_INIT;
	int r;

	strbuf_addstr(&buf, alt->path);
	r = for_each_loose_file_in_objdir_buf(&buf,
					      data->cb, NULL, NULL,
					      data->data);
	strbuf_release(&buf);
	return r;
}

int for_each_loose_object(each_loose_object_fn cb, void *data, unsigned flags)
{
	struct loose_alt_odb_data alt;
	int r;

	r = for_each_loose_file_in_objdir(get_object_directory(),
					  cb, NULL, NULL, data);
	if (r)
		return r;

	if (flags & FOR_EACH_OBJECT_LOCAL_ONLY)
		return 0;

	alt.cb = cb;
	alt.data = data;
	return foreach_alt_odb(loose_from_alt_odb, &alt);
}

static int check_stream_sha1(git_zstream *stream,
			     const char *hdr,
			     unsigned long size,
			     const char *path,
			     const unsigned char *expected_sha1)
{
	git_SHA_CTX c;
	unsigned char real_sha1[GIT_MAX_RAWSZ];
	unsigned char buf[4096];
	unsigned long total_read;
	int status = Z_OK;

	git_SHA1_Init(&c);
	git_SHA1_Update(&c, hdr, stream->total_out);

	/*
	 * We already read some bytes into hdr, but the ones up to the NUL
	 * do not count against the object's content size.
	 */
	total_read = stream->total_out - strlen(hdr) - 1;

	/*
	 * This size comparison must be "<=" to read the final zlib packets;
	 * see the comment in unpack_sha1_rest for details.
	 */
	while (total_read <= size &&
	       (status == Z_OK || status == Z_BUF_ERROR)) {
		stream->next_out = buf;
		stream->avail_out = sizeof(buf);
		if (size - total_read < stream->avail_out)
			stream->avail_out = size - total_read;
		status = git_inflate(stream, Z_FINISH);
		git_SHA1_Update(&c, buf, stream->next_out - buf);
		total_read += stream->next_out - buf;
	}
	git_inflate_end(stream);

	if (status != Z_STREAM_END) {
		error("corrupt loose object '%s'", sha1_to_hex(expected_sha1));
		return -1;
	}
	if (stream->avail_in) {
		error("garbage at end of loose object '%s'",
		      sha1_to_hex(expected_sha1));
		return -1;
	}

	git_SHA1_Final(real_sha1, &c);
	if (hashcmp(expected_sha1, real_sha1)) {
		error("sha1 mismatch for %s (expected %s)", path,
		      sha1_to_hex(expected_sha1));
		return -1;
	}

	return 0;
}

int read_loose_object(const char *path,
		      const unsigned char *expected_sha1,
		      enum object_type *type,
		      unsigned long *size,
		      void **contents)
{
	int ret = -1;
	void *map = NULL;
	unsigned long mapsize;
	git_zstream stream;
	char hdr[32];

	*contents = NULL;

	map = map_sha1_file_1(path, NULL, &mapsize);
	if (!map) {
		error_errno("unable to mmap %s", path);
		goto out;
	}

	if (unpack_sha1_header(&stream, map, mapsize, hdr, sizeof(hdr)) < 0) {
		error("unable to unpack header of %s", path);
		goto out;
	}

	*type = parse_sha1_header(hdr, size);
	if (*type < 0) {
		error("unable to parse header of %s", path);
		git_inflate_end(&stream);
		goto out;
	}

	if (*type == OBJ_BLOB) {
		if (check_stream_sha1(&stream, hdr, *size, path, expected_sha1) < 0)
			goto out;
	} else {
		*contents = unpack_sha1_rest(&stream, hdr, *size, expected_sha1);
		if (!*contents) {
			error("unable to unpack contents of %s", path);
			git_inflate_end(&stream);
			goto out;
		}
		if (check_sha1_signature(expected_sha1, *contents,
					 *size, typename(*type))) {
			error("sha1 mismatch for %s (expected %s)", path,
			      sha1_to_hex(expected_sha1));
			free(*contents);
			goto out;
		}
	}

	ret = 0; /* everything checks out */

out:
	if (map)
		munmap(map, mapsize);
	return ret;
}
