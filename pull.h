#ifndef PULL_H
#define PULL_H

/** To be provided by the particular implementation. **/
extern int fetch(unsigned char *sha1);

/** Set to fetch the target tree. */
extern int get_tree;

/** Set to fetch the commit history. */
extern int get_history;

/** Set to fetch the trees in the commit history. **/
extern int get_all;

/* Set to be verbose */
extern int get_verbosely;

/* Report what we got under get_verbosely */
extern void pull_say(const char *, const char *);

extern int pull(char *target);

#endif /* PULL_H */
