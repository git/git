#ifndef TR2_TLS_H
#define TR2_TLS_H

#include "trace2/tr2_ctr.h"
#include "trace2/tr2_tmr.h"

/*
 * Notice: the term "TLS" refers to "thread-local storage" in the
 * Trace2 source files.  This usage is borrowed from GCC and Windows.
 * There is NO relation to "transport layer security".
 */

/*
 * Arbitry limit for thread names for column alignment.
 */
#define TR2_MAX_THREAD_NAME (24)

struct tr2tls_thread_ctx {
	const char *thread_name;
	uint64_t *array_us_start;
	size_t alloc;
	size_t nr_open_regions; /* plays role of "nr" in ALLOC_GROW */
	int thread_id;
	struct tr2_timer_block timer_block;
	struct tr2_counter_block counter_block;
	unsigned int used_any_timer:1;
	unsigned int used_any_per_thread_timer:1;
	unsigned int used_any_counter:1;
	unsigned int used_any_per_thread_counter:1;
};

/*
 * Create thread-local storage for the current thread.
 *
 * The first thread in the process will have:
 *     { .thread_id=0, .thread_name="main" }
 * Subsequent threads are given a non-zero thread_id and a thread_name
 * constructed from the id and a thread base name (which is usually just
 * the name of the thread-proc function).  For example:
 *     { .thread_id=10, .thread_name="th10:fsm-listen" }
 * This helps to identify and distinguish messages from concurrent threads.
 * The ctx.thread_name field is truncated if necessary to help with column
 * alignment in printf-style messages.
 *
 * In this and all following functions the term "self" refers to the
 * current thread.
 */
struct tr2tls_thread_ctx *tr2tls_create_self(const char *thread_base_name,
					     uint64_t us_thread_start);

/*
 * Get the thread-local storage pointer of the current thread.
 */
struct tr2tls_thread_ctx *tr2tls_get_self(void);

/*
 * return true if the current thread is the main thread.
 */
int tr2tls_is_main_thread(void);

/*
 * Free the current thread's thread-local storage.
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
 * Initialize thread-local storage for Trace2.
 */
void tr2tls_init(void);

/*
 * Free all Trace2 thread-local storage resources.
 */
void tr2tls_release(void);

/*
 * Protected increment of an integer.
 */
int tr2tls_locked_increment(int *p);

/*
 * Capture the process start time and do nothing else.
 */
void tr2tls_start_process_clock(void);

/*
 * Explicitly lock/unlock our mutex.
 */
void tr2tls_lock(void);
void tr2tls_unlock(void);

#endif /* TR2_TLS_H */
