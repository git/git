#ifndef LOCKFILE_H
#define LOCKFILE_H

/*
 * File write-locks as used by Git.
 *
 * For an overview of how to use the lockfile API, please see
 *
 *     Documentation/technical/api-lockfile.txt
 *
 * This module keeps track of all locked files in lock_file_list for
 * use at cleanup. This list and the lock_file objects that comprise
 * it must be kept in self-consistent states at all time, because the
 * program can be interrupted any time by a signal, in which case the
 * signal handler will walk through the list attempting to clean up
 * any open lock files.
 *
 * A lockfile is owned by the process that created it. The lock_file
 * object has an "owner" field that records its owner. This field is
 * used to prevent a forked process from closing a lockfile created by
 * its parent.
 *
 * The possible states of a lock_file object are as follows:
 *
 * - Uninitialized.  In this state the object's on_list field must be
 *   zero but the rest of its contents need not be initialized.  As
 *   soon as the object is used in any way, it is irrevocably
 *   registered in the lock_file_list, and on_list is set.
 *
 * - Locked, lockfile open (after hold_lock_file_for_update(),
 *   hold_lock_file_for_append(), or reopen_lock_file()). In this
 *   state:
 *   - the lockfile exists
 *   - active is set
 *   - filename holds the filename of the lockfile
 *   - fd holds a file descriptor open for writing to the lockfile
 *   - fp holds a pointer to an open FILE object if and only if
 *     fdopen_lock_file() has been called on the object
 *   - owner holds the PID of the process that locked the file
 *
 * - Locked, lockfile closed (after successful close_lock_file()).
 *   Same as the previous state, except that the lockfile is closed
 *   and fd is -1.
 *
 * - Unlocked (after commit_lock_file(), commit_lock_file_to(),
 *   rollback_lock_file(), a failed attempt to lock, or a failed
 *   close_lock_file()).  In this state:
 *   - active is unset
 *   - filename is empty (usually, though there are transitory
 *     states in which this condition doesn't hold). Client code should
 *     *not* rely on the filename being empty in this state.
 *   - fd is -1
 *   - the object is left registered in the lock_file_list, and
 *     on_list is set.
 */

struct lock_file {
	struct lock_file *volatile next;
	volatile sig_atomic_t active;
	volatile int fd;
	FILE *volatile fp;
	volatile pid_t owner;
	char on_list;
	struct strbuf filename;
};

/* String appended to a filename to derive the lockfile name: */
#define LOCK_SUFFIX ".lock"
#define LOCK_SUFFIX_LEN 5

#define LOCK_DIE_ON_ERROR 1
#define LOCK_NO_DEREF 2

extern void unable_to_lock_message(const char *path, int err,
				   struct strbuf *buf);
extern NORETURN void unable_to_lock_die(const char *path, int err);
extern int hold_lock_file_for_update(struct lock_file *, const char *path, int);
extern int hold_lock_file_for_append(struct lock_file *, const char *path, int);
extern FILE *fdopen_lock_file(struct lock_file *, const char *mode);
extern char *get_locked_file_path(struct lock_file *);
extern int commit_lock_file_to(struct lock_file *, const char *path);
extern int commit_lock_file(struct lock_file *);
extern int reopen_lock_file(struct lock_file *);
extern int close_lock_file(struct lock_file *);
extern void rollback_lock_file(struct lock_file *);

#endif /* LOCKFILE_H */
