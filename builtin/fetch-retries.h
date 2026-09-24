#ifndef FETCH_RETRIES_H
#define FETCH_RETRIES_H

#include "git-compat-util.h"
#include "parse-options.h"
#include "strvec.h"

/*
 * A retry budget for commands that fetch over the network: "git fetch",
 * "git clone", "git pull", "git submodule update", "git maintenance run"
 * (prefetch), "git remote update" and "git remote add -f".
 *
 * A non-zero budget means a failing fetch is retried up to that many
 * times, waiting FETCH_RETRY_DELAY seconds between attempts.
 *
 * The option can be written as:
 *
 *   --retries          ; use FETCH_RETRY_DEFAULT retries
 *   --retries=N        ; retry up to N times (N >= 0)
 *   --retries=inf      ; retry forever ("infinite", "forever")
 *   --retries=never    ; never retry ("none", "0")
 *   --no-retries       ; never retry
 *
 * When the option is not given, the environment variable given to
 * fetch_retries_resolve() is consulted instead.  When neither is given,
 * the historical default (no retry) applies.
 */

/* Retry count used for a bare "--retries". */
#define FETCH_RETRY_NONE 0
#define FETCH_RETRY_DEFAULT 10

/* Sentinel meaning "retry forever". */
#define FETCH_RETRY_INFINITE INT_MAX

/* Sentinel meaning "option/env did not supply a value". */
#define FETCH_RETRY_UNSET -1

/* Delay in seconds between retry attempts. */
#define FETCH_RETRY_DELAY 5

/*
 * OPT_CALLBACK_F callback for a "--retries" option.
 *
 * Parses the option value (or bare "--retries", or "--no-retries") and
 * writes the resulting count into the int pointed to by `opt->value`.
 */
int fetch_retries_set_opt(const struct option *opt, const char *arg, int unset);

/*
 * Run `attempt_fn(ctx)` and, on failure, retry up to the remaining
 * `retries` budget, sleeping FETCH_RETRY_DELAY seconds between tries.
 * An infinite budget retries forever; a zero (or unset/none) budget
 * performs a single attempt and never retries.  `retries` is consumed
 * by value, so each in-process consumer gets its own full budget.
 *
 * Returns the result of the last attempt.
 */
typedef int (*fetch_retries_attempt_fn)(void *ctx);
int fetch_retries_loop(int retries, fetch_retries_attempt_fn attempt_fn,
		       void *ctx);

/*
 * Give `*retries` one chance to produce another attempt.
 *
 * Returns 1 (and consumes one retry from a finite budget) when another
 * attempt should be made, 0 otherwise.  Infinite budgets always return
 * 1; unset/none/zero budgets always return 0.
 */
int fetch_retries_next(int *retries);

/*
 * Resolve the final retry budget: when `*retries` is FETCH_RETRY_UNSET
 * the environment variable `env_name` is consulted, and if that still
 * leaves FETCH_RETRY_UNSET the budget is FETCH_RETRY_NONE.
 *
 * This is the "CLI takes precedence, then env, then no retry" rule.
 */
void fetch_retries_resolve(const char *env_name, int *retries);

/*
 * Push "--retries=<count>" (or "--retries=inf") onto `argv` to forward the
 * budget to a subprocess fetch.  Does nothing for FETCH_RETRY_UNSET or
 * FETCH_RETRY_NONE.
 */
void fetch_retries_forward(struct strvec *argv, int retries);

/* Sleep for FETCH_RETRY_DELAY seconds. */
void fetch_retries_sleep(void);

#endif /* FETCH_RETRIES_H */
