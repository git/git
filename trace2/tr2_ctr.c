#include "git-compat-util.h"
#include "thread-utils.h"
#include "trace2/tr2_tgt.h"
#include "trace2/tr2_tls.h"
#include "trace2/tr2_ctr.h"

/*
 * A global counter block to aggregrate values from the partial sums
 * from each thread.
 */
static struct tr2_counter_block final_counter_block; /* access under tr2tls_mutex */

/*
 * Define metadata for each global counter.
 *
 * This array must match the "enum trace2_counter_id" and the values
 * in "struct tr2_counter_block.counter[*]".
 */
static struct tr2_counter_metadata tr2_counter_metadata[TRACE2_NUMBER_OF_COUNTERS] = {
	[TRACE2_COUNTER_ID_TEST1] = {
		.category = "test",
		.name = "test1",
		.want_per_thread_events = 0,
	},
	[TRACE2_COUNTER_ID_TEST2] = {
		.category = "test",
		.name = "test2",
		.want_per_thread_events = 1,
	},

	/* Add additional metadata before here. */
};

void tr2_counter_increment(enum trace2_counter_id cid, uint64_t value)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	struct tr2_counter *c = &ctx->counter_block.counter[cid];

	c->value += value;

	ctx->used_any_counter = 1;
	if (tr2_counter_metadata[cid].want_per_thread_events)
		ctx->used_any_per_thread_counter = 1;
}

void tr2_update_final_counters(void)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	enum trace2_counter_id cid;

	if (!ctx->used_any_counter)
		return;

	/*
	 * Access `final_counter_block` requires holding `tr2tls_mutex`.
	 * We assume that our caller is holding the lock.
	 */

	for (cid = 0; cid < TRACE2_NUMBER_OF_COUNTERS; cid++) {
		struct tr2_counter *c_final = &final_counter_block.counter[cid];
		const struct tr2_counter *c = &ctx->counter_block.counter[cid];

		c_final->value += c->value;
	}
}

void tr2_emit_per_thread_counters(tr2_tgt_evt_counter_t *fn_apply)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();
	enum trace2_counter_id cid;

	if (!ctx->used_any_per_thread_counter)
		return;

	/*
	 * For each counter, if the counter wants per-thread events
	 * and this thread used it (the value is non-zero), emit it.
	 */
	for (cid = 0; cid < TRACE2_NUMBER_OF_COUNTERS; cid++)
		if (tr2_counter_metadata[cid].want_per_thread_events &&
		    ctx->counter_block.counter[cid].value)
			fn_apply(&tr2_counter_metadata[cid],
				 &ctx->counter_block.counter[cid],
				 0);
}

void tr2_emit_final_counters(tr2_tgt_evt_counter_t *fn_apply)
{
	enum trace2_counter_id cid;

	/*
	 * Access `final_counter_block` requires holding `tr2tls_mutex`.
	 * We assume that our caller is holding the lock.
	 */

	for (cid = 0; cid < TRACE2_NUMBER_OF_COUNTERS; cid++)
		if (final_counter_block.counter[cid].value)
			fn_apply(&tr2_counter_metadata[cid],
				 &final_counter_block.counter[cid],
				 1);
}
