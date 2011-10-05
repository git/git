#ifndef CONNECTED_H
#define CONNECTED_H

/*
 * Take callback data, and return next object name in the buffer.
 * When called after returning the name for the last object, return -1
 * to signal EOF, otherwise return 0.
 */
typedef int (*sha1_iterate_fn)(void *, unsigned char [20]);

/*
 * Make sure that our object store has all the commits necessary to
 * connect the ancestry chain to some of our existing refs, and all
 * the trees and blobs that these commits use.
 *
 * Return 0 if Ok, non zero otherwise (i.e. some missing objects)
 */
extern int check_everything_connected(sha1_iterate_fn, int quiet, void *cb_data);

#endif /* CONNECTED_H */
