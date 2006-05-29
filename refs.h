#ifndef REFS_H
#define REFS_H

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

/** Locks ref and returns the fd to give to write_ref_sha1() if the ref
 * has the given value currently; otherwise, returns -1.
 **/
extern int lock_ref_sha1(const char *ref, const unsigned char *old_sha1);

/** Writes sha1 into the refs file specified, locked with the given fd. **/
extern int write_ref_sha1(const char *ref, int fd, const unsigned char *sha1);

/** Writes sha1 into the refs file specified. **/
extern int write_ref_sha1_unlocked(const char *ref, const unsigned char *sha1);

/** Returns 0 if target has the right format for a ref. **/
extern int check_ref_format(const char *target);

#endif /* REFS_H */
