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

extern int pull(char *target);

#endif /* PULL_H */
