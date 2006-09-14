#ifndef REFS_H
#define REFS_H

struct ref_lock {
	char *ref_name;
	char *log_file;
	struct lock_file *lk;
	unsigned char old_sha1[20];
	int lock_fd;
	int force_write;
};

/*
 * Calls the specified function for each ref file until it returns nonzero,
 * and returns the value
 */
extern int head_ref(int (*fn)(const char *path, const unsigned char *sha1));
extern int for_each_ref(int (*fn)(const char *path, const unsigned char *sha1));
extern int for_each_tag_ref(int (*fn)(const char *path, const unsigned char *sha1));
extern int for_each_branch_ref(int (*fn)(const char *path, const unsigned char *sha1));
extern int for_each_remote_ref(int (*fn)(const char *path, const unsigned char *sha1));

/** Reads the refs file specified into sha1 **/
extern int get_ref_sha1(const char *ref, unsigned char *sha1);

/** Locks a "refs/" ref returning the lock on success and NULL on failure. **/
extern struct ref_lock *lock_ref_sha1(const char *ref, const unsigned char *old_sha1, int mustexist);

/** Locks any ref (for 'HEAD' type refs). */
extern struct ref_lock *lock_any_ref_for_update(const char *ref, const unsigned char *old_sha1, int mustexist);

/** Release any lock taken but not written. **/
extern void unlock_ref(struct ref_lock *lock);

/** Writes sha1 into the ref specified by the lock. **/
extern int write_ref_sha1(struct ref_lock *lock, const unsigned char *sha1, const char *msg);

/** Reads log for the value of ref during at_time. **/
extern int read_ref_at(const char *ref, unsigned long at_time, unsigned char *sha1);

/** Returns 0 if target has the right format for a ref. **/
extern int check_ref_format(const char *target);

#endif /* REFS_H */
