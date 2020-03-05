#ifndef WALKER_H
#define WALKER_H

#include "remote.h"

struct walker {
	void *data;
	int (*fetch_ref)(struct walker *, struct ref *ref);
	void (*prefetch)(struct walker *, unsigned char *sha1);
	int (*fetch)(struct walker *, unsigned char *sha1);
	void (*cleanup)(struct walker *);
	int get_verbosely;
	int get_progress;
	int get_recover;

	int corrupt_object_found;
};

/* Report what we got under get_verbosely */
__attribute__((format (printf, 2, 3)))
void walker_say(struct walker *walker, const char *fmt, ...);

/* Load pull targets from stdin */
int walker_targets_stdin(char ***target, const char ***write_ref);

/* Free up loaded targets */
void walker_targets_free(int targets, char **target, const char **write_ref);

/* If write_ref is set, the ref filename to write the target value to. */
/* If write_ref_log_details is set, additional text will appear in the ref log. */
int walker_fetch(struct walker *impl, int targets, char **target,
		 const char **write_ref, const char *write_ref_log_details);

void walker_free(struct walker *walker);

struct walker *get_http_walker(const char *url);

#endif /* WALKER_H */
