#ifndef ENTRY_H
#define ENTRY_H

#include "cache.h"
#include "convert.h"

struct checkout {
	struct index_state *istate;
	const char *base_dir;
	int base_dir_len;
	struct delayed_checkout *delayed_checkout;
	struct checkout_metadata meta;
	unsigned force:1,
		 quiet:1,
		 not_new:1,
		 clone:1,
		 refresh_cache:1;
};
#define CHECKOUT_INIT { NULL, "" }

#define TEMPORARY_FILENAME_LENGTH 25
/*
 * Write the contents from ce out to the working tree.
 *
 * When topath[] is not NULL, instead of writing to the working tree
 * file named by ce, a temporary file is created by this function and
 * its name is returned in topath[], which must be able to hold at
 * least TEMPORARY_FILENAME_LENGTH bytes long.
 *
 * With checkout_entry_ca(), callers can optionally pass a preloaded
 * conv_attrs struct (to avoid reloading it), when ce refers to a
 * regular file. If ca is NULL, the attributes will be loaded
 * internally when (and if) needed.
 */
int checkout_entry_ca(struct cache_entry *ce, struct conv_attrs *ca,
		      const struct checkout *state, char *topath,
		      int *nr_checkouts);
static inline int checkout_entry(struct cache_entry *ce,
				 const struct checkout *state, char *topath,
				 int *nr_checkouts)
{
	return checkout_entry_ca(ce, NULL, state, topath, nr_checkouts);
}

void enable_delayed_checkout(struct checkout *state);
int finish_delayed_checkout(struct checkout *state, int *nr_checkouts,
			    int show_progress);

/*
 * Unlink the last component and schedule the leading directories for
 * removal, such that empty directories get removed.
 */
void unlink_entry(const struct cache_entry *ce);

void *read_blob_entry(const struct cache_entry *ce, unsigned long *size);
int fstat_checkout_output(int fd, const struct checkout *state, struct stat *st);
void update_ce_after_write(const struct checkout *state, struct cache_entry *ce,
			   struct stat *st);

#endif /* ENTRY_H */
