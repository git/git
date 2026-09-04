#ifndef DIFF_PROVIDER_INTERNAL_H
#define DIFF_PROVIDER_INTERNAL_H

#include "diff-provider.h"

/*
 * The implementor-facing half of the hunk provider interface: the
 * provider chain a repository owns, and the rules a provider applies
 * to its own answer before any consumer sees it.  Provider
 * implementations include this header; consumers of the interface
 * use only diff-provider.h.
 */

/*
 * A provider's verdict on one request.  Only the chain walk
 * (diff-provider.c) sees these; it maps the dispositions of a whole
 * walk onto the public outcome set.
 */
enum diff_provider_disposition {
	/*
	 * The provider failed to produce the answer it owns.  Only
	 * the computing provider returns this: its compute leg is
	 * the one part of a consultation that can fail, and the walk
	 * ends with the public error outcome.
	 */
	DIFF_PROVIDER_DISP_ERROR = -1,

	/*
	 * Answered: every hunk of the pair has been emitted through
	 * the consumer's callback.
	 */
	DIFF_PROVIDER_DISP_ANSWERED = 0,

	/* Not this provider's request: the walk consults the next one. */
	DIFF_PROVIDER_DISP_PASS,

	/*
	 * The pair must not be answered from identity nor recorded:
	 * the request is shaped by parameters the provider's
	 * recording key cannot express, so a recorded answer would
	 * not match this request, and this request's result must not
	 * be recorded under that key.  The walk goes on, but consults
	 * only the computing provider, and its fall-through outcome
	 * tells the consumer not to record.
	 */
	DIFF_PROVIDER_DISP_STOP_NO_RECORD,
};

/*
 * One provider in a repository's chain (repository.h).  The chain is
 * assembled in diff-provider.c with a fixed composition; whether a
 * provider applies to a request is decided by nobody but the
 * provider, whose consult gates itself and passes.  Chain position
 * carries the authority resolution: an earlier provider's answer or
 * refusal outranks every provider after it.
 */
struct diff_provider {
	/*
	 * Consult this provider for one request.  fill is NULL on a
	 * consult-only walk; only the computing provider reads it,
	 * and it must pass when fill is NULL.
	 */
	enum diff_provider_disposition
		(*consult)(struct diff_provider *provider,
			   const struct diff_provider_request *req,
			   diff_provider_fill_fn fill, void *fill_data,
			   xdl_emit_hunk_consume_func_t hunk_cb,
			   void *cb_data);

	/*
	 * Tear down the provider's state, or NULL when it owns none.
	 * Runs when the owning repository is cleared; the chain frees
	 * the provider itself afterwards.
	 */
	void (*release)(struct diff_provider *provider);

	void *state;

	/*
	 * Set on the provider that loads content and computes rather
	 * than answering from the request's identity.  It alone is
	 * still consulted after a stop-no-record: an identity answer
	 * may no longer be served, but the computation must still
	 * run.
	 */
	unsigned computes:1;

	struct diff_provider *next;
};

/*
 * The providers Git ships, besides the builtin computation that
 * diff-provider.c holds itself.  Each call returns a fresh provider
 * for one repository's chain.
 */
struct diff_provider *diff_process_provider_new(void);
struct diff_provider *diff_hunks_store_provider_new(void);

/*
 * Incremental well-formedness check for a provider-supplied hunk
 * sequence, shared by every provider.  Each coordinate, and each
 * hunk's end (its start plus count), must fit int32 (a consumer may
 * truncate to int, and a provider may serialize as such); hunks must
 * be in order and must not overlap; and the unchanged run between
 * hunks must be the same length on both sides, or a consumer that
 * walks the two files in lockstep desynchronizes.  Every rule
 * constrains differences between coordinates, so the check applies
 * to 0-based and 1-based sequences alike.
 *
 * Feed the hunks in order to a zero-initialized struct; the first
 * nonzero return names the violated rule, and the whole sequence must
 * then be discarded unemitted.
 */
struct diff_provider_hunks_check {
	int64_t prev_old_end, prev_new_end;
};

enum diff_provider_hunks_error {
	DIFF_PROVIDER_HUNKS_OK = 0,
	DIFF_PROVIDER_HUNKS_RANGE,      /* negative or beyond int32 */
	DIFF_PROVIDER_HUNKS_OVERLAP,    /* out of order or overlapping */
	DIFF_PROVIDER_HUNKS_MISALIGNED, /* unchanged runs differ in length */
};

enum diff_provider_hunks_error
diff_provider_check_hunk(struct diff_provider_hunks_check *c,
			  long old_start, long old_count,
			  long new_start, long new_count);

#endif /* DIFF_PROVIDER_INTERNAL_H */
