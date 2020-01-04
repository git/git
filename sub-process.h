#ifndef SUBPROCESS_H
#define SUBPROCESS_H

#include "git-compat-util.h"
#include "hashmap.h"
#include "run-command.h"

/*
 * The sub-process API makes it possible to run background sub-processes
 * for the entire lifetime of a Git invocation. If Git needs to communicate
 * with an external process multiple times, then this can reduces the process
 * invocation overhead. Git and the sub-process communicate through stdin and
 * stdout.
 *
 * The sub-processes are kept in a hashmap by command name and looked up
 * via the subprocess_find_entry function.  If an existing instance can not
 * be found then a new process should be created and started.  When the
 * parent git command terminates, all sub-processes are also terminated.
 *
 * This API is based on the run-command API.
 */

 /* data structures */

/* Members should not be accessed directly. */
struct subprocess_entry {
	struct hashmap_entry ent;
	const char *cmd;
	struct child_process process;
};

struct subprocess_capability {
	const char *name;

	/*
	 * subprocess_handshake will "|=" this value to supported_capabilities
	 * if the server reports that it supports this capability.
	 */
	unsigned int flag;
};

/* subprocess functions */

/* Function to test two subprocess hashmap entries for equality. */
int cmd2process_cmp(const void *unused_cmp_data,
		    const struct hashmap_entry *e,
		    const struct hashmap_entry *entry_or_key,
		    const void *unused_keydata);

/*
 * User-supplied function to initialize the sub-process.  This is
 * typically used to negotiate the interface version and capabilities.
 */
typedef int(*subprocess_start_fn)(struct subprocess_entry *entry);

/* Start a subprocess and add it to the subprocess hashmap. */
int subprocess_start(struct hashmap *hashmap, struct subprocess_entry *entry, const char *cmd,
		subprocess_start_fn startfn);

/* Kill a subprocess and remove it from the subprocess hashmap. */
void subprocess_stop(struct hashmap *hashmap, struct subprocess_entry *entry);

/* Find a subprocess in the subprocess hashmap. */
struct subprocess_entry *subprocess_find_entry(struct hashmap *hashmap, const char *cmd);

/* subprocess helper functions */

/* Get the underlying `struct child_process` from a subprocess. */
static inline struct child_process *subprocess_get_child_process(
		struct subprocess_entry *entry)
{
	return &entry->process;
}

/*
 * Perform the version and capability negotiation as described in the
 * "Handshake" section of long-running-process-protocol.txt using the
 * given requested versions and capabilities. The "versions" and "capabilities"
 * parameters are arrays terminated by a 0 or blank struct.
 *
 * This function is typically called when a subprocess is started (as part of
 * the "startfn" passed to subprocess_start).
 */
int subprocess_handshake(struct subprocess_entry *entry,
			 const char *welcome_prefix,
			 int *versions,
			 int *chosen_version,
			 struct subprocess_capability *capabilities,
			 unsigned int *supported_capabilities);

/*
 * Helper function that will read packets looking for "status=<foo>"
 * key/value pairs and return the value from the last "status" packet
 */

int subprocess_read_status(int fd, struct strbuf *status);

#endif
