#ifndef PARALLEL_CHECKOUT_H
#define PARALLEL_CHECKOUT_H

struct cache_entry;
struct checkout;
struct conv_attrs;

enum pc_status {
	PC_UNINITIALIZED = 0,
	PC_ACCEPTING_ENTRIES,
	PC_RUNNING,
};

enum pc_status parallel_checkout_status(void);

/*
 * Put parallel checkout into the PC_ACCEPTING_ENTRIES state. Should be used
 * only when in the PC_UNINITIALIZED state.
 */
void init_parallel_checkout(void);

/*
 * Return -1 if parallel checkout is currently not accepting entries or if the
 * entry is not eligible for parallel checkout. Otherwise, enqueue the entry
 * for later write and return 0.
 */
int enqueue_checkout(struct cache_entry *ce, struct conv_attrs *ca);

/* Write all the queued entries, returning 0 on success.*/
int run_parallel_checkout(struct checkout *state);

#endif /* PARALLEL_CHECKOUT_H */
