#include "git-compat-util.h"
#include "fetch-retries.h"
#include "gettext.h"

/*
 * Parse a "--retries" option value.
 *
 * `arg` is the argument following "--retries=", or NULL for a bare
 * "--retries".  `negated` is non-zero for "--no-retries" (returning
 * FETCH_RETRY_NONE, distinguishable from "not given" via the "never"
 * / "none" strings which also yield FETCH_RETRY_NONE).
 *
 * Returns the retry count, dying on a malformed value.
 */
static int fetch_retries_parse(const char *arg, int negated)
{
	if (negated)
		return FETCH_RETRY_NONE;

	if (!arg)
		return FETCH_RETRY_DEFAULT;

	if (!strcmp(arg, "inf") || !strcmp(arg, "infinite") ||
	    !strcmp(arg, "forever"))
		return FETCH_RETRY_INFINITE;

	if (!strcmp(arg, "never") || !strcmp(arg, "none"))
		return FETCH_RETRY_NONE;

	{
		char *end;
		long n;

		errno = 0;
		n = strtol(arg, &end, 10);
		if (errno || end == arg || *end)
			die(_("invalid value for --retries: '%s'"), arg);
		if (n < 0)
			die(_("invalid value for --retries: '%s'"), arg);
		if (n > FETCH_RETRY_INFINITE)
			return FETCH_RETRY_INFINITE;
		return (int)n;
	}
}

int fetch_retries_set_opt(const struct option *opt, const char *arg, int unset)
{
	int *retries = opt->value;
	*retries = fetch_retries_parse(arg, unset);
	return 0;
}

/*
 * Read a retry count from environment variable `env_name`.
 *
 * Returns FETCH_RETRY_UNSET when the variable is not set (or empty),
 * otherwise the parsed (possibly infinite) count.  Dies on malformed
 * input.
 */
static int fetch_retries_env(const char *env_name)
{
	const char *val = getenv(env_name);

	if (!val || !*val)
		return FETCH_RETRY_UNSET;

	if (!strcmp(val, "inf") || !strcmp(val, "infinite") ||
	    !strcmp(val, "forever"))
		return FETCH_RETRY_INFINITE;

	if (!strcmp(val, "never") || !strcmp(val, "none"))
		return FETCH_RETRY_NONE;

	{
		char *end;
		long n;

		errno = 0;
		n = strtol(val, &end, 10);
		if (errno || end == val || *end)
			die(_("invalid value for %s: '%s'"), env_name, val);
		if (n < 0)
			die(_("invalid value for %s: '%s'"), env_name, val);
		if (n > FETCH_RETRY_INFINITE)
			return FETCH_RETRY_INFINITE;
		return (int)n;
	}
}

void fetch_retries_resolve(const char *env_name, int *retries)
{
	if (*retries != FETCH_RETRY_UNSET)
		return;
	*retries = fetch_retries_env(env_name);
	if (*retries == FETCH_RETRY_UNSET)
		*retries = FETCH_RETRY_NONE;
}

void fetch_retries_forward(struct strvec *argv, int retries)
{
	if (retries == FETCH_RETRY_UNSET || retries == FETCH_RETRY_NONE)
		return;
	if (retries == FETCH_RETRY_INFINITE)
		strvec_push(argv, "--retries=inf");
	else
		strvec_pushf(argv, "--retries=%d", retries);

	/*
	 * A bare "--retries" is consumed as FETCH_RETRY_DEFAULT by
	 * fetch_retries_parse(), so callers never see the bare form
	 * here; that value is already resolved.
	 */
}

void fetch_retries_sleep(void)
{
	sleep(FETCH_RETRY_DELAY);
}

int fetch_retries_next(int *retries)
{
	int left = *retries;

	switch (left) {
	case FETCH_RETRY_UNSET:
	case FETCH_RETRY_NONE:
		return 0;
	case FETCH_RETRY_INFINITE:
		return 1;
	default:
		if (left <= 0)
			return 0;
		*retries = left - 1;
		return 1;
	}
}

int fetch_retries_loop(int retries, fetch_retries_attempt_fn attempt_fn,
		       void *ctx)
{
	int result;

	for (;;) {
		result = attempt_fn(ctx);
		if (!result)
			break;
		if (!fetch_retries_next(&retries))
			break;
		fetch_retries_sleep();
	}
	return result;
}
