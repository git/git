#ifndef PULL_H
#define PULL_H

/*
 * Fetch object given SHA1 from the remote, and store it locally under
 * GIT_OBJECT_DIRECTORY.  Return 0 on success, -1 on failure.  To be
 * provided by the particular implementation.
 */
extern int fetch(unsigned char *sha1);

/*
 * Fetch the specified object and store it locally; fetch() will be
 * called later to determine success. To be provided by the particular
 * implementation.
 */
extern void prefetch(unsigned char *sha1);

/*
 * Fetch ref (relative to $GIT_DIR/refs) from the remote, and store
 * the 20-byte SHA1 in sha1.  Return 0 on success, -1 on failure.  To
 * be provided by the particular implementation.
 */
extern int fetch_ref(char *ref, unsigned char *sha1);

/* Set to fetch the target tree. */
extern int get_tree;

/* Set to fetch the commit history. */
extern int get_history;

/* Set to fetch the trees in the commit history. */
extern int get_all;

/* Set to be verbose */
extern int get_verbosely;

/* Set to check on all reachable objects. */
extern int get_recover;

/* Report what we got under get_verbosely */
extern void pull_say(const char *, const char *);

/* If write_ref is set, the ref filename to write the target value to. */
/* If write_ref_log_details is set, additional text will appear in the ref log. */
extern int pull(int targets, char **target, const char **write_ref,
		const char *write_ref_log_details);

#endif /* PULL_H */
