#ifndef SUBPROCESS_H
#define SUBPROCESS_H

#include "git-compat-util.h"
#include "hashmap.h"
#include "run-command.h"

/*
 * Generic implementation of background process infrastructure.
 * See Documentation/technical/api-background-process.txt.
 */

 /* data structures */

struct subprocess_entry {
	struct hashmap_entry ent; /* must be the first member! */
	const char *cmd;
	struct child_process process;
};

/* subprocess functions */

int cmd2process_cmp(const struct subprocess_entry *e1,
	const struct subprocess_entry *e2, const void *unused);

typedef int(*subprocess_start_fn)(struct subprocess_entry *entry);
int subprocess_start(struct hashmap *hashmap, struct subprocess_entry *entry, const char *cmd,
		subprocess_start_fn startfn);

void subprocess_stop(struct hashmap *hashmap, struct subprocess_entry *entry);

struct subprocess_entry *subprocess_find_entry(struct hashmap *hashmap, const char *cmd);

/* subprocess helper functions */

static inline struct child_process *subprocess_get_child_process(
		struct subprocess_entry *entry)
{
	return &entry->process;
}

/*
 * Helper function that will read packets looking for "status=<foo>"
 * key/value pairs and return the value from the last "status" packet
 */

int subprocess_read_status(int fd, struct strbuf *status);

#endif
