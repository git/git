#include "refs.h"
#include "cache.h"

#include <errno.h>

struct ref_list {
	struct ref_list *next;
	unsigned char flag; /* ISSYMREF? ISPACKED? */
	unsigned char sha1[20];
	char name[FLEX_ARRAY];
};

static const char *parse_ref_line(char *line, unsigned char *sha1)
{
	/*
	 * 42: the answer to everything.
	 *
	 * In this case, it happens to be the answer to
	 *  40 (length of sha1 hex representation)
	 *  +1 (space in between hex and name)
	 *  +1 (newline at the end of the line)
	 */
	int len = strlen(line) - 42;

	if (len <= 0)
		return NULL;
	if (get_sha1_hex(line, sha1) < 0)
		return NULL;
	if (!isspace(line[40]))
		return NULL;
	line += 41;
	if (isspace(*line))
		return NULL;
	if (line[len] != '\n')
		return NULL;
	line[len] = 0;
	return line;
}

static struct ref_list *add_ref(const char *name, const unsigned char *sha1,
				int flag, struct ref_list *list)
{
	int len;
	struct ref_list **p = &list, *entry;

	/* Find the place to insert the ref into.. */
	while ((entry = *p) != NULL) {
		int cmp = strcmp(entry->name, name);
		if (cmp > 0)
			break;

		/* Same as existing entry? */
		if (!cmp)
			return list;
		p = &entry->next;
	}

	/* Allocate it and add it in.. */
	len = strlen(name) + 1;
	entry = xmalloc(sizeof(struct ref_list) + len);
	hashcpy(entry->sha1, sha1);
	memcpy(entry->name, name, len);
	entry->flag = flag;
	entry->next = *p;
	*p = entry;
	return list;
}

/*
 * Future: need to be in "struct repository"
 * when doing a full libification.
 */
struct cached_refs {
	char did_loose;
	char did_packed;
	struct ref_list *loose;
	struct ref_list *packed;
} cached_refs;

static void free_ref_list(struct ref_list *list)
{
	struct ref_list *next;
	for ( ; list; list = next) {
		next = list->next;
		free(list);
	}
}

static void invalidate_cached_refs(void)
{
	struct cached_refs *ca = &cached_refs;

	if (ca->did_loose && ca->loose)
		free_ref_list(ca->loose);
	if (ca->did_packed && ca->packed)
		free_ref_list(ca->packed);
	ca->loose = ca->packed = NULL;
	ca->did_loose = ca->did_packed = 0;
}

static struct ref_list *get_packed_refs(void)
{
	if (!cached_refs.did_packed) {
		struct ref_list *refs = NULL;
		FILE *f = fopen(git_path("packed-refs"), "r");
		if (f) {
			struct ref_list *list = NULL;
			char refline[PATH_MAX];
			while (fgets(refline, sizeof(refline), f)) {
				unsigned char sha1[20];
				const char *name = parse_ref_line(refline, sha1);
				if (!name)
					continue;
				list = add_ref(name, sha1, REF_ISPACKED, list);
			}
			fclose(f);
			refs = list;
		}
		cached_refs.packed = refs;
		cached_refs.did_packed = 1;
	}
	return cached_refs.packed;
}

static struct ref_list *get_ref_dir(const char *base, struct ref_list *list)
{
	DIR *dir = opendir(git_path("%s", base));

	if (dir) {
		struct dirent *de;
		int baselen = strlen(base);
		char *ref = xmalloc(baselen + 257);

		memcpy(ref, base, baselen);
		if (baselen && base[baselen-1] != '/')
			ref[baselen++] = '/';

		while ((de = readdir(dir)) != NULL) {
			unsigned char sha1[20];
			struct stat st;
			int flag;
			int namelen;

			if (de->d_name[0] == '.')
				continue;
			namelen = strlen(de->d_name);
			if (namelen > 255)
				continue;
			if (has_extension(de->d_name, ".lock"))
				continue;
			memcpy(ref + baselen, de->d_name, namelen+1);
			if (stat(git_path("%s", ref), &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode)) {
				list = get_ref_dir(ref, list);
				continue;
			}
			if (!resolve_ref(ref, sha1, 1, &flag)) {
				error("%s points nowhere!", ref);
				continue;
			}
			list = add_ref(ref, sha1, flag, list);
		}
		free(ref);
		closedir(dir);
	}
	return list;
}

static struct ref_list *get_loose_refs(void)
{
	if (!cached_refs.did_loose) {
		cached_refs.loose = get_ref_dir("refs", NULL);
		cached_refs.did_loose = 1;
	}
	return cached_refs.loose;
}

/* We allow "recursive" symbolic refs. Only within reason, though */
#define MAXDEPTH 5

const char *resolve_ref(const char *ref, unsigned char *sha1, int reading, int *flag)
{
	int depth = MAXDEPTH, len;
	char buffer[256];
	static char ref_buffer[256];

	if (flag)
		*flag = 0;

	for (;;) {
		const char *path = git_path("%s", ref);
		struct stat st;
		char *buf;
		int fd;

		if (--depth < 0)
			return NULL;

		/* Special case: non-existing file.
		 * Not having the refs/heads/new-branch is OK
		 * if we are writing into it, so is .git/HEAD
		 * that points at refs/heads/master still to be
		 * born.  It is NOT OK if we are resolving for
		 * reading.
		 */
		if (lstat(path, &st) < 0) {
			struct ref_list *list = get_packed_refs();
			while (list) {
				if (!strcmp(ref, list->name)) {
					hashcpy(sha1, list->sha1);
					if (flag)
						*flag |= REF_ISPACKED;
					return ref;
				}
				list = list->next;
			}
			if (reading || errno != ENOENT)
				return NULL;
			hashclr(sha1);
			return ref;
		}

		/* Follow "normalized" - ie "refs/.." symlinks by hand */
		if (S_ISLNK(st.st_mode)) {
			len = readlink(path, buffer, sizeof(buffer)-1);
			if (len >= 5 && !memcmp("refs/", buffer, 5)) {
				buffer[len] = 0;
				strcpy(ref_buffer, buffer);
				ref = ref_buffer;
				if (flag)
					*flag |= REF_ISSYMREF;
				continue;
			}
		}

		/* Is it a directory? */
		if (S_ISDIR(st.st_mode)) {
			errno = EISDIR;
			return NULL;
		}

		/*
		 * Anything else, just open it and try to use it as
		 * a ref
		 */
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return NULL;
		len = read(fd, buffer, sizeof(buffer)-1);
		close(fd);

		/*
		 * Is it a symbolic ref?
		 */
		if (len < 4 || memcmp("ref:", buffer, 4))
			break;
		buf = buffer + 4;
		len -= 4;
		while (len && isspace(*buf))
			buf++, len--;
		while (len && isspace(buf[len-1]))
			len--;
		buf[len] = 0;
		memcpy(ref_buffer, buf, len + 1);
		ref = ref_buffer;
		if (flag)
			*flag |= REF_ISSYMREF;
	}
	if (len < 40 || get_sha1_hex(buffer, sha1))
		return NULL;
	return ref;
}

int create_symref(const char *ref_target, const char *refs_heads_master)
{
	const char *lockpath;
	char ref[1000];
	int fd, len, written;
	const char *git_HEAD = git_path("%s", ref_target);

#ifndef NO_SYMLINK_HEAD
	if (prefer_symlink_refs) {
		unlink(git_HEAD);
		if (!symlink(refs_heads_master, git_HEAD))
			return 0;
		fprintf(stderr, "no symlink - falling back to symbolic ref\n");
	}
#endif

	len = snprintf(ref, sizeof(ref), "ref: %s\n", refs_heads_master);
	if (sizeof(ref) <= len) {
		error("refname too long: %s", refs_heads_master);
		return -1;
	}
	lockpath = mkpath("%s.lock", git_HEAD);
	fd = open(lockpath, O_CREAT | O_EXCL | O_WRONLY, 0666);	
	written = write(fd, ref, len);
	close(fd);
	if (written != len) {
		unlink(lockpath);
		error("Unable to write to %s", lockpath);
		return -2;
	}
	if (rename(lockpath, git_HEAD) < 0) {
		unlink(lockpath);
		error("Unable to create %s", git_HEAD);
		return -3;
	}
	if (adjust_shared_perm(git_HEAD)) {
		unlink(lockpath);
		error("Unable to fix permissions on %s", lockpath);
		return -4;
	}
	return 0;
}

int read_ref(const char *ref, unsigned char *sha1)
{
	if (resolve_ref(ref, sha1, 1, NULL))
		return 0;
	return -1;
}

static int do_for_each_ref(const char *base, each_ref_fn fn, int trim,
			   void *cb_data)
{
	int retval;
	struct ref_list *packed = get_packed_refs();
	struct ref_list *loose = get_loose_refs();

	while (packed && loose) {
		struct ref_list *entry;
		int cmp = strcmp(packed->name, loose->name);
		if (!cmp) {
			packed = packed->next;
			continue;
		}
		if (cmp > 0) {
			entry = loose;
			loose = loose->next;
		} else {
			entry = packed;
			packed = packed->next;
		}
		if (strncmp(base, entry->name, trim))
			continue;
		if (is_null_sha1(entry->sha1))
			continue;
		if (!has_sha1_file(entry->sha1)) {
			error("%s does not point to a valid object!", entry->name);
			continue;
		}
		retval = fn(entry->name + trim, entry->sha1,
			    entry->flag, cb_data);
		if (retval)
			return retval;
	}

	packed = packed ? packed : loose;
	while (packed) {
		if (!strncmp(base, packed->name, trim)) {
			retval = fn(packed->name + trim, packed->sha1,
				    packed->flag, cb_data);
			if (retval)
				return retval;
		}
		packed = packed->next;
	}
	return 0;
}

int head_ref(each_ref_fn fn, void *cb_data)
{
	unsigned char sha1[20];
	int flag;

	if (resolve_ref("HEAD", sha1, 1, &flag))
		return fn("HEAD", sha1, flag, cb_data);
	return 0;
}

int for_each_ref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref("refs/", fn, 0, cb_data);
}

int for_each_tag_ref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref("refs/tags/", fn, 10, cb_data);
}

int for_each_branch_ref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref("refs/heads/", fn, 11, cb_data);
}

int for_each_remote_ref(each_ref_fn fn, void *cb_data)
{
	return do_for_each_ref("refs/remotes/", fn, 13, cb_data);
}

/* NEEDSWORK: This is only used by ssh-upload and it should go; the
 * caller should do resolve_ref or read_ref like everybody else.  Or
 * maybe everybody else should use get_ref_sha1() instead of doing
 * read_ref().
 */
int get_ref_sha1(const char *ref, unsigned char *sha1)
{
	if (check_ref_format(ref))
		return -1;
	return read_ref(mkpath("refs/%s", ref), sha1);
}

/*
 * Make sure "ref" is something reasonable to have under ".git/refs/";
 * We do not like it if:
 *
 * - any path component of it begins with ".", or
 * - it has double dots "..", or
 * - it has ASCII control character, "~", "^", ":" or SP, anywhere, or
 * - it ends with a "/".
 */

static inline int bad_ref_char(int ch)
{
	return (((unsigned) ch) <= ' ' ||
		ch == '~' || ch == '^' || ch == ':' ||
		/* 2.13 Pattern Matching Notation */
		ch == '?' || ch == '*' || ch == '[');
}

int check_ref_format(const char *ref)
{
	int ch, level;
	const char *cp = ref;

	level = 0;
	while (1) {
		while ((ch = *cp++) == '/')
			; /* tolerate duplicated slashes */
		if (!ch)
			return -1; /* should not end with slashes */

		/* we are at the beginning of the path component */
		if (ch == '.' || bad_ref_char(ch))
			return -1;

		/* scan the rest of the path component */
		while ((ch = *cp++) != 0) {
			if (bad_ref_char(ch))
				return -1;
			if (ch == '/')
				break;
			if (ch == '.' && *cp == '.')
				return -1;
		}
		level++;
		if (!ch) {
			if (level < 2)
				return -1; /* at least of form "heads/blah" */
			return 0;
		}
	}
}

static struct ref_lock *verify_lock(struct ref_lock *lock,
	const unsigned char *old_sha1, int mustexist)
{
	if (!resolve_ref(lock->ref_name, lock->old_sha1, mustexist, NULL)) {
		error("Can't verify ref %s", lock->ref_name);
		unlock_ref(lock);
		return NULL;
	}
	if (hashcmp(lock->old_sha1, old_sha1)) {
		error("Ref %s is at %s but expected %s", lock->ref_name,
			sha1_to_hex(lock->old_sha1), sha1_to_hex(old_sha1));
		unlock_ref(lock);
		return NULL;
	}
	return lock;
}

static int remove_empty_dir_recursive(char *path, int len)
{
	DIR *dir = opendir(path);
	struct dirent *e;
	int ret = 0;

	if (!dir)
		return -1;
	if (path[len-1] != '/')
		path[len++] = '/';
	while ((e = readdir(dir)) != NULL) {
		struct stat st;
		int namlen;
		if ((e->d_name[0] == '.') &&
		    ((e->d_name[1] == 0) ||
		     ((e->d_name[1] == '.') && e->d_name[2] == 0)))
			continue; /* "." and ".." */

		namlen = strlen(e->d_name);
		if ((len + namlen < PATH_MAX) &&
		    strcpy(path + len, e->d_name) &&
		    !lstat(path, &st) &&
		    S_ISDIR(st.st_mode) &&
		    !remove_empty_dir_recursive(path, len + namlen))
			continue; /* happy */

		/* path too long, stat fails, or non-directory still exists */
		ret = -1;
		break;
	}
	closedir(dir);
	if (!ret) {
		path[len] = 0;
		ret = rmdir(path);
	}
	return ret;
}

static int remove_empty_directories(char *file)
{
	/* we want to create a file but there is a directory there;
	 * if that is an empty directory (or a directory that contains
	 * only empty directories), remove them.
	 */
	char path[PATH_MAX];
	int len = strlen(file);

	if (len >= PATH_MAX) /* path too long ;-) */
		return -1;
	strcpy(path, file);
	return remove_empty_dir_recursive(path, len);
}

static struct ref_lock *lock_ref_sha1_basic(const char *ref, const unsigned char *old_sha1, int *flag)
{
	char *ref_file;
	const char *orig_ref = ref;
	struct ref_lock *lock;
	struct stat st;
	int last_errno = 0;
	int mustexist = (old_sha1 && !is_null_sha1(old_sha1));

	lock = xcalloc(1, sizeof(struct ref_lock));
	lock->lock_fd = -1;

	ref = resolve_ref(ref, lock->old_sha1, mustexist, flag);
	if (!ref && errno == EISDIR) {
		/* we are trying to lock foo but we used to
		 * have foo/bar which now does not exist;
		 * it is normal for the empty directory 'foo'
		 * to remain.
		 */
		ref_file = git_path("%s", orig_ref);
		if (remove_empty_directories(ref_file)) {
			last_errno = errno;
			error("there are still refs under '%s'", orig_ref);
			goto error_return;
		}
		ref = resolve_ref(orig_ref, lock->old_sha1, mustexist, flag);
	}
	if (!ref) {
		last_errno = errno;
		error("unable to resolve reference %s: %s",
			orig_ref, strerror(errno));
		goto error_return;
	}
	if (is_null_sha1(lock->old_sha1)) {
		/* The ref did not exist and we are creating it.
		 * Make sure there is no existing ref that is packed
		 * whose name begins with our refname, nor a ref whose
		 * name is a proper prefix of our refname.
		 */
		int namlen = strlen(ref); /* e.g. 'foo/bar' */
		struct ref_list *list = get_packed_refs();
		while (list) {
			/* list->name could be 'foo' or 'foo/bar/baz' */
			int len = strlen(list->name);
			int cmplen = (namlen < len) ? namlen : len;
			const char *lead = (namlen < len) ? list->name : ref;

			if (!strncmp(ref, list->name, cmplen) &&
			    lead[cmplen] == '/') {
				error("'%s' exists; cannot create '%s'",
				      list->name, ref);
				goto error_return;
			}
			list = list->next;
		}
	}

	lock->lk = xcalloc(1, sizeof(struct lock_file));

	lock->ref_name = xstrdup(ref);
	lock->log_file = xstrdup(git_path("logs/%s", ref));
	ref_file = git_path("%s", ref);
	lock->force_write = lstat(ref_file, &st) && errno == ENOENT;

	if (safe_create_leading_directories(ref_file)) {
		last_errno = errno;
		error("unable to create directory for %s", ref_file);
		goto error_return;
	}
	lock->lock_fd = hold_lock_file_for_update(lock->lk, ref_file, 1);

	return old_sha1 ? verify_lock(lock, old_sha1, mustexist) : lock;

 error_return:
	unlock_ref(lock);
	errno = last_errno;
	return NULL;
}

struct ref_lock *lock_ref_sha1(const char *ref, const unsigned char *old_sha1)
{
	char refpath[PATH_MAX];
	if (check_ref_format(ref))
		return NULL;
	strcpy(refpath, mkpath("refs/%s", ref));
	return lock_ref_sha1_basic(refpath, old_sha1, NULL);
}

struct ref_lock *lock_any_ref_for_update(const char *ref, const unsigned char *old_sha1)
{
	return lock_ref_sha1_basic(ref, old_sha1, NULL);
}

static struct lock_file packlock;

static int repack_without_ref(const char *refname)
{
	struct ref_list *list, *packed_ref_list;
	int fd;
	int found = 0;

	packed_ref_list = get_packed_refs();
	for (list = packed_ref_list; list; list = list->next) {
		if (!strcmp(refname, list->name)) {
			found = 1;
			break;
		}
	}
	if (!found)
		return 0;
	memset(&packlock, 0, sizeof(packlock));
	fd = hold_lock_file_for_update(&packlock, git_path("packed-refs"), 0);
	if (fd < 0)
		return error("cannot delete '%s' from packed refs", refname);

	for (list = packed_ref_list; list; list = list->next) {
		char line[PATH_MAX + 100];
		int len;

		if (!strcmp(refname, list->name))
			continue;
		len = snprintf(line, sizeof(line), "%s %s\n",
			       sha1_to_hex(list->sha1), list->name);
		/* this should not happen but just being defensive */
		if (len > sizeof(line))
			die("too long a refname '%s'", list->name);
		write_or_die(fd, line, len);
	}
	return commit_lock_file(&packlock);
}

int delete_ref(const char *refname, unsigned char *sha1)
{
	struct ref_lock *lock;
	int err, i, ret = 0, flag = 0;

	lock = lock_ref_sha1_basic(refname, sha1, &flag);
	if (!lock)
		return 1;
	if (!(flag & REF_ISPACKED)) {
		/* loose */
		i = strlen(lock->lk->filename) - 5; /* .lock */
		lock->lk->filename[i] = 0;
		err = unlink(lock->lk->filename);
		if (err) {
			ret = 1;
			error("unlink(%s) failed: %s",
			      lock->lk->filename, strerror(errno));
		}
		lock->lk->filename[i] = '.';
	}
	/* removing the loose one could have resurrected an earlier
	 * packed one.  Also, if it was not loose we need to repack
	 * without it.
	 */
	ret |= repack_without_ref(refname);

	err = unlink(lock->log_file);
	if (err && errno != ENOENT)
		fprintf(stderr, "warning: unlink(%s) failed: %s",
			lock->log_file, strerror(errno));
	invalidate_cached_refs();
	unlock_ref(lock);
	return ret;
}

void unlock_ref(struct ref_lock *lock)
{
	if (lock->lock_fd >= 0) {
		close(lock->lock_fd);
		/* Do not free lock->lk -- atexit() still looks at them */
		if (lock->lk)
			rollback_lock_file(lock->lk);
	}
	free(lock->ref_name);
	free(lock->log_file);
	free(lock);
}

static int log_ref_write(struct ref_lock *lock,
	const unsigned char *sha1, const char *msg)
{
	int logfd, written, oflags = O_APPEND | O_WRONLY;
	unsigned maxlen, len;
	char *logrec;
	const char *committer;

	if (log_all_ref_updates &&
	    !strncmp(lock->ref_name, "refs/heads/", 11)) {
		if (safe_create_leading_directories(lock->log_file) < 0)
			return error("unable to create directory for %s",
				lock->log_file);
		oflags |= O_CREAT;
	}

	logfd = open(lock->log_file, oflags, 0666);
	if (logfd < 0) {
		if (!(oflags & O_CREAT) && errno == ENOENT)
			return 0;
		return error("Unable to append to %s: %s",
			lock->log_file, strerror(errno));
	}

	committer = git_committer_info(1);
	if (msg) {
		maxlen = strlen(committer) + strlen(msg) + 2*40 + 5;
		logrec = xmalloc(maxlen);
		len = snprintf(logrec, maxlen, "%s %s %s\t%s\n",
			sha1_to_hex(lock->old_sha1),
			sha1_to_hex(sha1),
			committer,
			msg);
	}
	else {
		maxlen = strlen(committer) + 2*40 + 4;
		logrec = xmalloc(maxlen);
		len = snprintf(logrec, maxlen, "%s %s %s\n",
			sha1_to_hex(lock->old_sha1),
			sha1_to_hex(sha1),
			committer);
	}
	written = len <= maxlen ? write(logfd, logrec, len) : -1;
	free(logrec);
	close(logfd);
	if (written != len)
		return error("Unable to append to %s", lock->log_file);
	return 0;
}

int write_ref_sha1(struct ref_lock *lock,
	const unsigned char *sha1, const char *logmsg)
{
	static char term = '\n';

	if (!lock)
		return -1;
	if (!lock->force_write && !hashcmp(lock->old_sha1, sha1)) {
		unlock_ref(lock);
		return 0;
	}
	if (write(lock->lock_fd, sha1_to_hex(sha1), 40) != 40 ||
	    write(lock->lock_fd, &term, 1) != 1
		|| close(lock->lock_fd) < 0) {
		error("Couldn't write %s", lock->lk->filename);
		unlock_ref(lock);
		return -1;
	}
	invalidate_cached_refs();
	if (log_ref_write(lock, sha1, logmsg) < 0) {
		unlock_ref(lock);
		return -1;
	}
	if (commit_lock_file(lock->lk)) {
		error("Couldn't set %s", lock->ref_name);
		unlock_ref(lock);
		return -1;
	}
	lock->lock_fd = -1;
	unlock_ref(lock);
	return 0;
}

int read_ref_at(const char *ref, unsigned long at_time, unsigned char *sha1)
{
	const char *logfile, *logdata, *logend, *rec, *lastgt, *lastrec;
	char *tz_c;
	int logfd, tz;
	struct stat st;
	unsigned long date;
	unsigned char logged_sha1[20];

	logfile = git_path("logs/%s", ref);
	logfd = open(logfile, O_RDONLY, 0);
	if (logfd < 0)
		die("Unable to read log %s: %s", logfile, strerror(errno));
	fstat(logfd, &st);
	if (!st.st_size)
		die("Log %s is empty.", logfile);
	logdata = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, logfd, 0);
	close(logfd);

	lastrec = NULL;
	rec = logend = logdata + st.st_size;
	while (logdata < rec) {
		if (logdata < rec && *(rec-1) == '\n')
			rec--;
		lastgt = NULL;
		while (logdata < rec && *(rec-1) != '\n') {
			rec--;
			if (*rec == '>')
				lastgt = rec;
		}
		if (!lastgt)
			die("Log %s is corrupt.", logfile);
		date = strtoul(lastgt + 1, &tz_c, 10);
		if (date <= at_time) {
			if (lastrec) {
				if (get_sha1_hex(lastrec, logged_sha1))
					die("Log %s is corrupt.", logfile);
				if (get_sha1_hex(rec + 41, sha1))
					die("Log %s is corrupt.", logfile);
				if (hashcmp(logged_sha1, sha1)) {
					tz = strtoul(tz_c, NULL, 10);
					fprintf(stderr,
						"warning: Log %s has gap after %s.\n",
						logfile, show_rfc2822_date(date, tz));
				}
			}
			else if (date == at_time) {
				if (get_sha1_hex(rec + 41, sha1))
					die("Log %s is corrupt.", logfile);
			}
			else {
				if (get_sha1_hex(rec + 41, logged_sha1))
					die("Log %s is corrupt.", logfile);
				if (hashcmp(logged_sha1, sha1)) {
					tz = strtoul(tz_c, NULL, 10);
					fprintf(stderr,
						"warning: Log %s unexpectedly ended on %s.\n",
						logfile, show_rfc2822_date(date, tz));
				}
			}
			munmap((void*)logdata, st.st_size);
			return 0;
		}
		lastrec = rec;
	}

	rec = logdata;
	while (rec < logend && *rec != '>' && *rec != '\n')
		rec++;
	if (rec == logend || *rec == '\n')
		die("Log %s is corrupt.", logfile);
	date = strtoul(rec + 1, &tz_c, 10);
	tz = strtoul(tz_c, NULL, 10);
	if (get_sha1_hex(logdata, sha1))
		die("Log %s is corrupt.", logfile);
	munmap((void*)logdata, st.st_size);
	fprintf(stderr, "warning: Log %s only goes back to %s.\n",
		logfile, show_rfc2822_date(date, tz));
	return 0;
}
