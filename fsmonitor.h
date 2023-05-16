#ifndef FSMONITOR_H
#define FSMONITOR_H

#include "cache.h"
#include "dir.h"
#include "fsmonitor-settings.h"
#include "trace.h"

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
 * Check if refresh_fsmonitor has been called at least once.
 * refresh_fsmonitor is idempotent. Returns true if fsmonitor is
 * not enabled (since the state will be "fresh" w/ CE_FSMONITOR_VALID unset)
 * This version is useful for assertions
 */
static inline int is_fsmonitor_refreshed(const struct index_state *istate)
{
	enum fsmonitor_mode fsm_mode = fsm_settings__get_mode(istate->repo);

	return fsm_mode <= FSMONITOR_MODE_DISABLED ||
		istate->fsmonitor_has_run_once;
}

/*
 * Set the given cache entries CE_FSMONITOR_VALID bit. This should be
 * called any time the cache entry has been updated to reflect the
 * current state of the file on disk.
 *
 * However, never mark submodules as valid.  When commands like "git
 * status" run they might need to recurse into the submodule (using a
 * child process) to get a summary of the submodule state.  We don't
 * have (and don't want to create) the facility to translate every
 * FS event that we receive and that happens to be deep inside of a
 * submodule back to the submodule root, so we cannot correctly keep
 * track of this bit on the gitlink directory.  Therefore, we never
 * set it on submodules.
 */
static inline void mark_fsmonitor_valid(struct index_state *istate, struct cache_entry *ce)
{
	enum fsmonitor_mode fsm_mode = fsm_settings__get_mode(istate->repo);

	if (fsm_mode > FSMONITOR_MODE_DISABLED &&
	    !(ce->ce_flags & CE_FSMONITOR_VALID)) {
		if (S_ISGITLINK(ce->ce_mode))
			return;
		istate->cache_changed |= FSMONITOR_CHANGED;
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
	enum fsmonitor_mode fsm_mode = fsm_settings__get_mode(istate->repo);

	if (fsm_mode > FSMONITOR_MODE_DISABLED) {
		ce->ce_flags &= ~CE_FSMONITOR_VALID;
		untracked_cache_invalidate_path(istate, ce->name, 1);
		trace_printf_key(&trace_fsmonitor, "mark_fsmonitor_invalid '%s'", ce->name);
	}
}

#endif
