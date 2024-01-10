#include "git-compat-util.h"
#include "trace2/tr2_tgt.h"
#include "trace2/tr2_tls.h"
#include "trace2/tr2_tmr.h"
#include "trace.h"

#define MY_MAX(a, b) ((a) > (b) ? (a) : (b))
#define MY_MIN(a, b) ((a) < (b) ? (a) : (b))

/*
 * A global timer block to aggregate values from the partial sums from
 * each thread.
 */
static struct tr2_timer_block final_timer_block; /* access under tr2tls_mutex */

/*
 * Define metadata for each stopwatch timer.
 *
 * This array must match "enum trace2_timer_id" and the values
 * in "struct tr2_timer_block.timer[*]".
 */
static struct tr2_timer_metadata tr2_timer_metadata[TRACE2_NUMBER_OF_TIMERS] = {
	[TRACE2_TIMER_ID_TEST1] = {
		.category = "test",
		.name = "test1",
		.want_per_thread_events = 0,
	},
	[TRACE2_TIMER_ID_TEST2] = {
		.category = "test",
		.name = "test2",
		.want_per_thread_events = 1,
	},

	/* Add additional metadata before here. */
};

void tr2_start_timer(enum trace2_timer_id tid)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct tr2_timer *t = &ctx->timer_block.timer[tid];

	t->recursion_count++;
	if (t->recursion_count > 1)
		return; /* ignore recursive starts */

	t->start_ns = getnanotime();
}

void tr2_stop_timer(enum trace2_timer_id tid)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct tr2_timer *t = &ctx->timer_block.timer[tid];
	uint64_t ns_now;
	uint64_t ns_interval;

	assert(t->recursion_count > 0);

	t->recursion_count--;
	if (t->recursion_count)
		return; /* still in recursive call(s) */

	ns_now = getnanotime();
	ns_interval = ns_now - t->start_ns;

	t->total_ns += ns_interval;

	/*
	 * min_ns was initialized to zero (in the xcalloc()) rather
	 * than UINT_MAX when the block of timers was allocated,
	 * so we should always set both the min_ns and max_ns values
	 * the first time that the timer is used.
	 */
	if (!t->interval_count) {
		t->min_ns = ns_interval;
		t->max_ns = ns_interval;
	} else {
		t->min_ns = MY_MIN(ns_interval, t->min_ns);
		t->max_ns = MY_MAX(ns_interval, t->max_ns);
	}

	t->interval_count++;

	ctx->used_any_timer = 1;
	if (tr2_timer_metadata[tid].want_per_thread_events)
		ctx->used_any_per_thread_timer = 1;
}

void tr2_update_final_timers(void)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	enum trace2_timer_id tid;

	if (!ctx->used_any_timer)
		return;

	/*
	 * Accessing `final_timer_block` requires holding `tr2tls_mutex`.
	 * We assume that our caller is holding the lock.
	 */

	for (tid = 0; tid < TRACE2_NUMBER_OF_TIMERS; tid++) {
		struct tr2_timer *t_final = &final_timer_block.timer[tid];
		struct tr2_timer *t = &ctx->timer_block.timer[tid];

		if (t->recursion_count) {
			/*
			 * The current thread is exiting with
			 * timer[tid] still running.
			 *
			 * Technically, this is a bug, but I'm going
			 * to ignore it.
			 *
			 * I don't think it is worth calling die()
			 * for.  I don't think it is worth killing the
			 * process for this bookkeeping error.  We
			 * might want to call warning(), but I'm going
			 * to wait on that.
			 *
			 * The downside here is that total_ns won't
			 * include the current open interval (now -
			 * start_ns).  I can live with that.
			 */
		}

		if (!t->interval_count)
			continue; /* this timer was not used by this thread */

		t_final->total_ns += t->total_ns;

		/*
		 * final_timer_block.timer[tid].min_ns was initialized to
		 * was initialized to zero rather than UINT_MAX, so we should
		 * always set both the min_ns and max_ns values the first time
		 * that we add a partial sum into it.
		 */
		if (!t_final->interval_count) {
			t_final->min_ns = t->min_ns;
			t_final->max_ns = t->max_ns;
		} else {
			t_final->min_ns = MY_MIN(t_final->min_ns, t->min_ns);
			t_final->max_ns = MY_MAX(t_final->max_ns, t->max_ns);
		}

		t_final->interval_count += t->interval_count;
	}
}

void tr2_emit_per_thread_timers(tr2_tgt_evt_timer_t *fn_apply)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	enum trace2_timer_id tid;

	if (!ctx->used_any_per_thread_timer)
		return;

	/*
	 * For each timer, if the timer wants per-thread events and
	 * this thread used it, emit it.
	 */
	for (tid = 0; tid < TRACE2_NUMBER_OF_TIMERS; tid++)
		if (tr2_timer_metadata[tid].want_per_thread_events &&
		    ctx->timer_block.timer[tid].interval_count)
			fn_apply(&tr2_timer_metadata[tid],
				 &ctx->timer_block.timer[tid],
				 0);
}

void tr2_emit_final_timers(tr2_tgt_evt_timer_t *fn_apply)
{
	enum trace2_timer_id tid;

	/*
	 * Accessing `final_timer_block` requires holding `tr2tls_mutex`.
	 * We assume that our caller is holding the lock.
	 */

	for (tid = 0; tid < TRACE2_NUMBER_OF_TIMERS; tid++)
		if (final_timer_block.timer[tid].interval_count)
			fn_apply(&tr2_timer_metadata[tid],
				 &final_timer_block.timer[tid],
				 1);
}
