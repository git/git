#ifndef DIFF_PROVIDER_H
#define DIFF_PROVIDER_H

#include "xdiff-interface.h"

/*
 * The hunk provider interface sits between naming a pair of file
 * versions to diff and computing their changed line ranges.
 * Consumers that operate on hunk coordinates route their diff
 * through here, so that a provider can answer for the pair before
 * its content is loaded.
 *
 * A hunk provider answers a consumer's request from the pair's
 * identity, its blob object ids and the settings that determine the
 * diff, before any content is loaded; a request no provider answers
 * falls through to the consumer's own computation.  Two providers implement this
 * interface with different authority.  The diff-hunks store
 * (diff-hunks.h) is in-process and not authoritative: it may only
 * reproduce the builtin result, so it never asserts a pair
 * equivalent, and it stands aside wherever a process outranks it.  A
 * process configured in diff.<driver>.process (diff-process.c) is
 * authoritative for its paths: its answer may deliberately differ
 * from the builtin diff, including asserting a pair equivalent.  The
 * interface resolves that authority through a provider chain owned
 * by the repository, built on first consultation and released by
 * repo_clear(): chain order is the resolution, and the builtin
 * computation itself is the chain's terminal provider.  A consumer
 * never names a provider; it reads the outcome below.  Every answer a
 * provider serves from identity passes the shared coordinate check
 * (diff-provider-internal.h) before any consumer sees it.
 */

struct diff_options;
struct object_id;
struct repository;

/*
 * The result of a consultation: two dependent axes flattened into
 * their four valid points.  The first axis is the state of the
 * response: the pair was answered, no provider answered, or (from
 * diff_provider_emit_hunks() alone) the attempt failed.  The second
 * axis exists only in the unanswered state: whether what the caller
 * computes for this request may be recorded, the one rule the
 * interface imposes on an otherwise free caller.  The rule travels
 * in the outcome because the knowledge is a provider's while the
 * recording is the caller's, and it shares the enum with the state,
 * rather than riding a separate flag, so that no meaningless
 * combination is representable and -Wswitch forces every consumer
 * that switches to place the no-record arm.
 *
 * These values describe consultations, not providers: the set does
 * not grow when a provider is added; a new provider maps onto these
 * values inside the interface, so consumer code is written once.
 * Each entry point returns a subrange of the set (stated at its
 * declaration); a switch over this enum should list every value and
 * omit "default:" so -Wswitch keeps it exhaustive, and a caller for
 * whom only one value is actionable may compare against that value
 * alone.
 */
enum diff_provider_outcome {
	/*
	 * Loading or diffing the pair failed.  Returned only by
	 * diff_provider_emit_hunks(), whose compute leg is the only
	 * part of a consultation that can fail.
	 */
	DIFF_PROVIDER_ERROR = -1,

	/*
	 * The request is answered: every hunk of the pair has been
	 * emitted through the callback.  An authoritative provider
	 * that finds the pair equivalent answers with no hunks at
	 * all, so a callback that never fired is an answer, not an
	 * accident.
	 */
	DIFF_PROVIDER_ANSWERED = 0,

	/*
	 * No provider answered.  What happens next is the caller's
	 * business, typically computing the diff itself; a result it
	 * computes for this request may be recorded.
	 */
	DIFF_PROVIDER_UNANSWERED,

	/*
	 * No provider answered, and what the caller computes for
	 * this request must not be recorded: either an authoritative
	 * provider owns the pair and declined this request, or the
	 * request is shaped by parameters outside the recording key,
	 * the key a recorded result is later served by.
	 */
	DIFF_PROVIDER_UNANSWERED_NO_RECORD,
};

/*
 * A consultation request.  The interface consults providers from
 * these fields alone; no content is loaded before an answer.
 *
 * repo owns the provider chain the request walks.  old_oid/new_oid
 * name the blobs whose bytes are diffed; pass NULL for a side whose
 * bytes are not a stored blob (a working-tree file, textconv output,
 * a gitlink), so no provider answers from an id it cannot look up.
 * path names the file the pair is diffed as; a provider selected by
 * path applies only where it is set.  diffopt carries the diff
 * settings that live outside xpp; xpp carries the parameters the
 * diff runs with.  Each provider gates itself on the fields that
 * concern it.
 */
struct diff_provider_request {
	struct repository *repo;
	const struct object_id *old_oid;
	const struct object_id *new_oid;
	const char *path;
	struct diff_options *diffopt;
	const xpparam_t *xpp;
};

/*
 * Consult the providers for the request's pair without computing.
 * On DIFF_PROVIDER_ANSWERED the hunks were emitted through hunk_cb
 * (0-based emission coordinates, context 0) and were validated
 * before the first callback ran, so a consumer may accumulate
 * directly into its result.  Never returns DIFF_PROVIDER_ERROR.
 * The callback's return value is not consulted: emission of a
 * validated answer has no error leg, so the callback must return 0.
 */
enum diff_provider_outcome
diff_provider_consult(const struct diff_provider_request *req,
		      xdl_emit_hunk_consume_func_t hunk_cb, void *cb_data);

/*
 * Load the pair's content.  Called at most once per request, only
 * when the ranges are computed rather than provided.  The buffers
 * borrow storage owned by the callback's owner.
 */
typedef int (*diff_provider_fill_fn)(void *data, mmfile_t *old_file,
				     mmfile_t *new_file);

/*
 * Consult the providers and, when no identity answer serves the
 * request, load the pair's content through fill and compute its
 * exact changed ranges (context 0).  Emits to hunk_cb either way and
 * returns DIFF_PROVIDER_ANSWERED, or DIFF_PROVIDER_ERROR when fill
 * or the diff fails.  The unanswered outcomes are never returned: a
 * pair no provider answers is computed here instead of in the caller.
 */
enum diff_provider_outcome
diff_provider_emit_hunks(const struct diff_provider_request *req,
			 diff_provider_fill_fn fill, void *fill_data,
			 xdl_emit_hunk_consume_func_t hunk_cb,
			 void *cb_data);

/*
 * Release the repository's provider chain: stop any provider-owned
 * processes and free the providers.  Called by repo_clear(); the
 * chain builds again on the next consultation.
 */
void diff_providers_clear(struct repository *r);

#endif /* DIFF_PROVIDER_H */
