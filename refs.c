#include "refs.h"
#include "cache.h"

#include <errno.h>

/* We allow "recursive" symbolic refs. Only within reason, though */
#define MAXDEPTH 5

const char *resolve_ref(const char *path, unsigned char *sha1, int reading)
{
	int depth = MAXDEPTH, len;
	char buffer[256];

	for (;;) {
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
			if (reading || errno != ENOENT)
				return NULL;
			memset(sha1, 0, 20);
			return path;
		}

		/* Follow "normalized" - ie "refs/.." symlinks by hand */
		if (S_ISLNK(st.st_mode)) {
			len = readlink(path, buffer, sizeof(buffer)-1);
			if (len >= 5 && !memcmp("refs/", buffer, 5)) {
				path = git_path("%.*s", len, buffer);
				continue;
			}
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
			buf[--len] = 0;
		path = git_path("%.*s", len, buf);
	}
	if (len < 40 || get_sha1_hex(buffer, sha1))
		return NULL;
	return path;
}

int create_symref(const char *git_HEAD, const char *refs_heads_master)
{
	const char *lockpath;
	char ref[1000];
	int fd, len, written;

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

int read_ref(const char *filename, unsigned char *sha1)
{
	if (resolve_ref(filename, sha1, 1))
		return 0;
	return -1;
}

static int do_for_each_ref(const char *base, int (*fn)(const char *path, const unsigned char *sha1), int trim)
{
	int retval = 0;
	DIR *dir = opendir(git_path("%s", base));

	if (dir) {
		struct dirent *de;
		int baselen = strlen(base);
		char *path = xmalloc(baselen + 257);

		if (!strncmp(base, "./", 2)) {
			base += 2;
			baselen -= 2;
		}
		memcpy(path, base, baselen);
		if (baselen && base[baselen-1] != '/')
			path[baselen++] = '/';

		while ((de = readdir(dir)) != NULL) {
			unsigned char sha1[20];
			struct stat st;
			int namelen;

			if (de->d_name[0] == '.')
				continue;
			namelen = strlen(de->d_name);
			if (namelen > 255)
				continue;
			if (has_extension(de->d_name, namelen, ".lock"))
				continue;
			memcpy(path + baselen, de->d_name, namelen+1);
			if (stat(git_path("%s", path), &st) < 0)
				continue;
			if (S_ISDIR(st.st_mode)) {
				retval = do_for_each_ref(path, fn, trim);
				if (retval)
					break;
				continue;
			}
			if (read_ref(git_path("%s", path), sha1) < 0) {
				error("%s points nowhere!", path);
				continue;
			}
			if (!has_sha1_file(sha1)) {
				error("%s does not point to a valid "
				      "commit object!", path);
				continue;
			}
			retval = fn(path + trim, sha1);
			if (retval)
				break;
		}
		free(path);
		closedir(dir);
	}
	return retval;
}

int head_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	unsigned char sha1[20];
	if (!read_ref(git_path("HEAD"), sha1))
		return fn("HEAD", sha1);
	return 0;
}

int for_each_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	return do_for_each_ref("refs", fn, 0);
}

int for_each_tag_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	return do_for_each_ref("refs/tags", fn, 10);
}

int for_each_branch_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	return do_for_each_ref("refs/heads", fn, 11);
}

int for_each_remote_ref(int (*fn)(const char *path, const unsigned char *sha1))
{
	return do_for_each_ref("refs/remotes", fn, 13);
}

int get_ref_sha1(const char *ref, unsigned char *sha1)
{
	if (check_ref_format(ref))
		return -1;
	return read_ref(git_path("refs/%s", ref), sha1);
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
	char buf[40];
	int nr, fd = open(lock->ref_file, O_RDONLY);
	if (fd < 0 && (mustexist || errno != ENOENT)) {
		error("Can't verify ref %s", lock->ref_file);
		unlock_ref(lock);
		return NULL;
	}
	nr = read(fd, buf, 40);
	close(fd);
	if (nr != 40 || get_sha1_hex(buf, lock->old_sha1) < 0) {
		error("Can't verify ref %s", lock->ref_file);
		unlock_ref(lock);
		return NULL;
	}
	if (memcmp(lock->old_sha1, old_sha1, 20)) {
		error("Ref %s is at %s but expected %s", lock->ref_file,
			sha1_to_hex(lock->old_sha1), sha1_to_hex(old_sha1));
		unlock_ref(lock);
		return NULL;
	}
	return lock;
}

static struct ref_lock *lock_ref_sha1_basic(const char *path,
	int plen,
	const unsigned char *old_sha1, int mustexist)
{
	const char *orig_path = path;
	struct ref_lock *lock;
	struct stat st;

	lock = xcalloc(1, sizeof(struct ref_lock));
	lock->lock_fd = -1;

	plen = strlen(path) - plen;
	path = resolve_ref(path, lock->old_sha1, mustexist);
	if (!path) {
		int last_errno = errno;
		error("unable to resolve reference %s: %s",
			orig_path, strerror(errno));
		unlock_ref(lock);
		errno = last_errno;
		return NULL;
	}
	lock->lk = xcalloc(1, sizeof(struct lock_file));

	lock->ref_file = strdup(path);
	lock->log_file = strdup(git_path("logs/%s", lock->ref_file + plen));
	lock->force_write = lstat(lock->ref_file, &st) && errno == ENOENT;

	if (safe_create_leading_directories(lock->ref_file))
		die("unable to create directory for %s", lock->ref_file);
	lock->lock_fd = hold_lock_file_for_update(lock->lk, lock->ref_file);
	if (lock->lock_fd < 0) {
		error("Couldn't open lock file %s: %s",
		      lock->lk->filename, strerror(errno));
		unlock_ref(lock);
		return NULL;
	}

	return old_sha1 ? verify_lock(lock, old_sha1, mustexist) : lock;
}

struct ref_lock *lock_ref_sha1(const char *ref,
	const unsigned char *old_sha1, int mustexist)
{
	if (check_ref_format(ref))
		return NULL;
	return lock_ref_sha1_basic(git_path("refs/%s", ref),
		5 + strlen(ref), old_sha1, mustexist);
}

struct ref_lock *lock_any_ref_for_update(const char *ref,
	const unsigned char *old_sha1, int mustexist)
{
	return lock_ref_sha1_basic(git_path("%s", ref),
		strlen(ref), old_sha1, mustexist);
}

void unlock_ref(struct ref_lock *lock)
{
	if (lock->lock_fd >= 0) {
		close(lock->lock_fd);
		/* Do not free lock->lk -- atexit() still looks at them */
		if (lock->lk)
			rollback_lock_file(lock->lk);
	}
	if (lock->ref_file)
		free(lock->ref_file);
	if (lock->log_file)
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

	if (log_all_ref_updates) {
		if (safe_create_leading_directories(lock->log_file) < 0)
			return error("unable to create directory for %s",
				lock->log_file);
		oflags |= O_CREAT;
	}

	logfd = open(lock->log_file, oflags, 0666);
	if (logfd < 0) {
		if (!log_all_ref_updates && errno == ENOENT)
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
	if (!lock->force_write && !memcmp(lock->old_sha1, sha1, 20)) {
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
	if (log_ref_write(lock, sha1, logmsg) < 0) {
		unlock_ref(lock);
		return -1;
	}
	if (commit_lock_file(lock->lk)) {
		error("Couldn't set %s", lock->ref_file);
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
				if (memcmp(logged_sha1, sha1, 20)) {
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
				if (memcmp(logged_sha1, sha1, 20)) {
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
