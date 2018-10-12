#ifndef CONNECTED_H
#define CONNECTED_H

struct object_id;
struct transport;

/*
 * Take callback data, and return next object name in the buffer.
 * When called after returning the name for the last object, return -1
 * to signal EOF, otherwise return 0.
 */
typedef int (*oid_iterate_fn)(void *, struct object_id *oid);

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

	/*
	 * If non-zero, check the ancestry chain completely, not stopping at
	 * any existing ref. This is necessary when deepening existing refs
	 * during a fetch.
	 */
	unsigned is_deepening_fetch : 1;
};

#define CHECK_CONNECTED_INIT { 0 }

/*
 * Make sure that all given objects and all objects reachable from them
 * either exist in our object store or (if the repository is a partial
 * clone) are promised to be available.
 *
 * Return 0 if Ok, non zero otherwise (i.e. some missing objects)
 *
 * If "opt" is NULL, behaves as if CHECK_CONNECTED_INIT was passed.
 */
int check_connected(oid_iterate_fn fn, void *cb_data,
		    struct check_connected_options *opt);

#endif /* CONNECTED_H */
