#ifndef TR2_TMR_H
#define TR2_TMR_H

#include "trace2.h"
#include "trace2/tr2_tgt.h"

/*
 * Define a mechanism to allow "stopwatch" timers.
 *
 * Timers can be used to measure "interesting" activity that does not
 * fit the "region" model, such as code called from many different
 * regions (like zlib) and/or where data for individual calls are not
 * interesting or are too numerous to be efficiently logged.
 *
 * Timer values are accumulated during program execution and emitted
 * to the Trace2 logs at program exit.
 *
 * To make this model efficient, we define a compile-time fixed set of
 * timers and timer ids using a "timer block" array in thread-local
 * storage.  This gives us constant time access to each timer within
 * each thread, since we want start/stop operations to be as fast as
 * possible.  This lets us avoid the complexities of dynamically
 * allocating a timer on the first use by a thread and/or possibly
 * sharing that timer definition with other concurrent threads.
 * However, this does require that we define time the set of timers at
 * compile time.
 *
 * Each thread uses the timer block in its thread-local storage to
 * compute partial sums for each timer (without locking).  When a
 * thread exits, those partial sums are (under lock) added to the
 * global final sum.
 *
 * Using this "timer block" model costs ~48 bytes per timer per thread
 * (we have about six uint64 fields per timer).  This does increase
 * the size of the thread-local storage block, but it is allocated (at
 * thread create time) and not on the thread stack, so I'm not worried
 * about the size.
 *
 * Partial sums for each timer are optionally emitted when a thread
 * exits.
 *
 * Final sums for each timer are emitted between the "exit" and
 * "atexit" events.
 *
 * A parallel "timer metadata" table contains the "category" and "name"
 * fields for each timer.  This eliminates the need to include those
 * args in the various timer APIs.
 */

/*
 * The definition of an individual timer and used by an individual
 * thread.
 */
struct tr2_timer {
	/*
	 * Total elapsed time for this timer in this thread in nanoseconds.
	 */
	uint64_t total_ns;

	/*
	 * The maximum and minimum interval values observed for this
	 * timer in this thread.
	 */
	uint64_t min_ns;
	uint64_t max_ns;

	/*
	 * The value of the clock when this timer was started in this
	 * thread.  (Undefined when the timer is not active in this
	 * thread.)
	 */
	uint64_t start_ns;

	/*
	 * Number of times that this timer has been started and stopped
	 * in this thread.  (Recursive starts are ignored.)
	 */
	uint64_t interval_count;

	/*
	 * Number of nested starts on the stack in this thread.  (We
	 * ignore recursive starts and use this to track the recursive
	 * calls.)
	 */
	unsigned int recursion_count;
};

/*
 * Metadata for a timer.
 */
struct tr2_timer_metadata {
	const char *category;
	const char *name;

	/*
	 * True if we should emit per-thread events for this timer
	 * when individual threads exit.
	 */
	unsigned int want_per_thread_events:1;
};

/*
 * A compile-time fixed-size block of timers to insert into
 * thread-local storage.  This wrapper is used to avoid quirks
 * of C and the usual need to pass an array size argument.
 */
struct tr2_timer_block {
	struct tr2_timer timer[TRACE2_NUMBER_OF_TIMERS];
};

/*
 * Private routines used by trace2.c to actually start/stop an
 * individual timer in the current thread.
 */
void tr2_start_timer(enum trace2_timer_id tid);
void tr2_stop_timer(enum trace2_timer_id tid);

/*
 * Add the current thread's timer data to the global totals.
 * This is called during thread-exit.
 *
 * Caller must be holding the tr2tls_mutex.
 */
void tr2_update_final_timers(void);

/*
 * Emit per-thread timer data for the current thread.
 * This is called during thread-exit.
 */
void tr2_emit_per_thread_timers(tr2_tgt_evt_timer_t *fn_apply);

/*
 * Emit global total timer values.
 * This is called during atexit handling.
 *
 * Caller must be holding the tr2tls_mutex.
 */
void tr2_emit_final_timers(tr2_tgt_evt_timer_t *fn_apply);

#endif /* TR2_TMR_H */
