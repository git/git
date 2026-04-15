#ifndef TR2_CTR_H
#define TR2_CTR_H

#include "trace2.h"
#include "trace2/tr2_tgt.h"

/*
 * Define a mechanism to allow global "counters".
 *
 * Counters can be used count interesting activity that does not fit
 * the "region and data" model, such as code called from many
 * different regions and/or where you want to count a number of items,
 * but don't have control of when the last item will be processed,
 * such as counter the number of calls to `lstat()`.
 *
 * Counters differ from Trace2 "data" events.  Data events are emitted
 * immediately and are appropriate for documenting loop counters at
 * the end of a region, for example.  Counter values are accumulated
 * during the program and final counter values are emitted at program
 * exit.
 *
 * To make this model efficient, we define a compile-time fixed set of
 * counters and counter ids using a fixed size "counter block" array
 * in thread-local storage.  This gives us constant time, lock-free
 * access to each counter within each thread.  This lets us avoid the
 * complexities of dynamically allocating a counter and sharing that
 * definition with other threads.
 *
 * Each thread uses the counter block in its thread-local storage to
 * increment partial sums for each counter (without locking).  When a
 * thread exits, those partial sums are (under lock) added to the
 * global final sum.
 *
 * Partial sums for each counter are optionally emitted when a thread
 * exits.
 *
 * Final sums for each counter are emitted between the "exit" and
 * "atexit" events.
 *
 * A parallel "counter metadata" table contains the "category" and
 * "name" fields for each counter.  This eliminates the need to
 * include those args in the various counter APIs.
 */

/*
 * The definition of an individual counter as used by an individual
 * thread (and later in aggregation).
 */
struct tr2_counter {
	uint64_t value;
};

/*
 * Metadata for a counter.
 */
struct tr2_counter_metadata {
	const char *category;
	const char *name;

	/*
	 * True if we should emit per-thread events for this counter
	 * when individual threads exit.
	 */
	unsigned int want_per_thread_events:1;
};

/*
 * A compile-time fixed block of counters to insert into thread-local
 * storage.  This wrapper is used to avoid quirks of C and the usual
 * need to pass an array size argument.
 */
struct tr2_counter_block {
	struct tr2_counter counter[TRACE2_NUMBER_OF_COUNTERS];
};

/*
 * Private routines used by trace2.c to increment a counter for the
 * current thread.
 */
void tr2_counter_increment(enum trace2_counter_id cid, uint64_t value);

/*
 * Add the current thread's counter data to the global totals.
 * This is called during thread-exit.
 *
 * Caller must be holding the tr2tls_mutex.
 */
void tr2_update_final_counters(void);

/*
 * Emit per-thread counter data for the current thread.
 * This is called during thread-exit.
 */
void tr2_emit_per_thread_counters(tr2_tgt_evt_counter_t *fn_apply);

/*
 * Emit global counter values.
 * This is called during atexit handling.
 *
 * Caller must be holding the tr2tls_mutex.
 */
void tr2_emit_final_counters(tr2_tgt_evt_counter_t *fn_apply);

#endif /* TR2_CTR_H */
