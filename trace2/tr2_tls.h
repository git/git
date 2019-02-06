#ifndef TR2_TLS_H
#define TR2_TLS_H

#include "strbuf.h"

/*
 * Arbitry limit for thread names for column alignment.
 */
#define TR2_MAX_THREAD_NAME (24)

struct tr2tls_thread_ctx {
	struct strbuf thread_name;
	uint64_t *array_us_start;
	int alloc;
	int nr_open_regions; /* plays role of "nr" in ALLOC_GROW */
	int thread_id;
};

/*
 * Create TLS data for the current thread.  This gives us a place to
 * put per-thread data, such as thread start time, function nesting
 * and a per-thread label for our messages.
 *
 * We assume the first thread is "main".  Other threads are given
 * non-zero thread-ids to help distinguish messages from concurrent
 * threads.
 *
 * Truncate the thread name if necessary to help with column alignment
 * in printf-style messages.
 *
 * In this and all following functions the term "self" refers to the
 * current thread.
 */
struct tr2tls_thread_ctx *tr2tls_create_self(const char *thread_name);

/*
 * Get our TLS data.
 */
struct tr2tls_thread_ctx *tr2tls_get_self(void);

/*
 * return true if the current thread is the main thread.
 */
int tr2tls_is_main_thread(void);

/*
 * Free our TLS data.
 */
void tr2tls_unset_self(void);

/*
 * Begin a new nested region and remember the start time.
 */
void tr2tls_push_self(uint64_t us_now);

/*
 * End the innermost nested region.
 */
void tr2tls_pop_self(void);

/*
 * Pop any extra (above the first) open regions on the current
 * thread and discard.  During a thread-exit, we should only
 * have region[0] that was pushed in trace2_thread_start() if
 * the thread exits normally.
 */
void tr2tls_pop_unwind_self(void);

/*
 * Compute the elapsed time since the innermost region in the
 * current thread started and the given time (usually now).
 */
uint64_t tr2tls_region_elasped_self(uint64_t us);

/*
 * Compute the elapsed time since the main thread started
 * and the given time (usually now).  This is assumed to
 * be the absolute run time of the process.
 */
uint64_t tr2tls_absolute_elapsed(uint64_t us);

/*
 * Initialize the tr2 TLS system.
 */
void tr2tls_init(void);

/*
 * Free all tr2 TLS resources.
 */
void tr2tls_release(void);

/*
 * Protected increment of an integer.
 */
int tr2tls_locked_increment(int *p);

#endif /* TR2_TLS_H */
