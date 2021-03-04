#ifndef FSMONITOR_H
#define FSMONITOR_H

#include "cache.h"
#include "dir.h"

extern struct trace_key trace_fsmonitor;

/*
 * Read the fsmonitor index extension and (if configured) restore the
 * CE_FSMONITOR_VALID state.
 */
int read_fsmonitor_extension(struct index_state *istate, const void *data, unsigned long sz);

/*
 * Fill the fsmonitor_dirty ewah bits with their state from the index,
 * before it is split during writing.
 */
void fill_fsmonitor_bitmap(struct index_state *istate);

/*
 * Write the CE_FSMONITOR_VALID state into the fsmonitor index
 * extension.  Reads from the fsmonitor_dirty ewah in the index.
 */
void write_fsmonitor_extension(struct strbuf *sb, struct index_state *istate);

/*
 * Add/remove the fsmonitor index extension
 */
void add_fsmonitor(struct index_state *istate);
void remove_fsmonitor(struct index_state *istate);

/*
 * Add/remove the fsmonitor index extension as necessary based on the current
 * core.fsmonitor setting.
 */
void tweak_fsmonitor(struct index_state *istate);

/*
 * Run the configured fsmonitor integration script and clear the
 * CE_FSMONITOR_VALID bit for any files returned as dirty.  Also invalidate
 * any corresponding untracked cache directory structures. Optimized to only
 * run the first time it is called.
 */
void refresh_fsmonitor(struct index_state *istate);

/*
 * Does the received result contain the "trivial" response?
 */
int fsmonitor_is_trivial_response(const struct strbuf *query_result);

/*
 * Set the given cache entries CE_FSMONITOR_VALID bit. This should be
 * called any time the cache entry has been updated to reflect the
 * current state of the file on disk.
 */
static inline void mark_fsmonitor_valid(struct index_state *istate, struct cache_entry *ce)
{
	if (core_fsmonitor && !(ce->ce_flags & CE_FSMONITOR_VALID)) {
		istate->cache_changed = 1;
		ce->ce_flags |= CE_FSMONITOR_VALID;
		trace_printf_key(&trace_fsmonitor, "mark_fsmonitor_clean '%s'", ce->name);
	}
}

/*
 * Clear the given cache entry's CE_FSMONITOR_VALID bit and invalidate
 * any corresponding untracked cache directory structures. This should
 * be called any time git creates or modifies a file that should
 * trigger an lstat() or invalidate the untracked cache for the
 * corresponding directory
 */
static inline void mark_fsmonitor_invalid(struct index_state *istate, struct cache_entry *ce)
{
	if (core_fsmonitor) {
		ce->ce_flags &= ~CE_FSMONITOR_VALID;
		untracked_cache_invalidate_path(istate, ce->name, 1);
		trace_printf_key(&trace_fsmonitor, "mark_fsmonitor_invalid '%s'", ce->name);
	}
}

#endif
