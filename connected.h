#ifndef CONNECTED_H
#define CONNECTED_H

struct transport;

/*
 * Take callback data, and return next object name in the buffer.
 * When called after returning the name for the last object, return -1
 * to signal EOF, otherwise return 0.
 */
typedef int (*sha1_iterate_fn)(void *, unsigned char [20]);

/*
 * Named-arguments struct for check_connected. All arguments are
 * optional, and can be left to defaults as set by CHECK_CONNECTED_INIT.
 */
struct check_connected_options {
	/* Avoid printing any errors to stderr. */
	int quiet;

	/* --shallow-file to pass to rev-list sub-process */
	const char *shallow_file;

	/* Transport whose objects we are checking, if available. */
	struct transport *transport;

	/*
	 * If non-zero, send error messages to this descriptor rather
	 * than stderr. The descriptor is closed before check_connected
	 * returns.
	 */
	int err_fd;

	/* If non-zero, show progress as we traverse the objects. */
	int progress;

	/*
	 * Insert these variables into the environment of the child process.
	 */
	const char **env;
};

#define CHECK_CONNECTED_INIT { 0 }

/*
 * Make sure that our object store has all the commits necessary to
 * connect the ancestry chain to some of our existing refs, and all
 * the trees and blobs that these commits use.
 *
 * Return 0 if Ok, non zero otherwise (i.e. some missing objects)
 *
 * If "opt" is NULL, behaves as if CHECK_CONNECTED_INIT was passed.
 */
int check_connected(sha1_iterate_fn fn, void *cb_data,
		    struct check_connected_options *opt);

#endif /* CONNECTED_H */
