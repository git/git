#ifndef AUTOCORRECT_H
#define AUTOCORRECT_H

enum autocorrect_mode {
	AUTOCORRECT_HINT,
	AUTOCORRECT_NEVER,
	AUTOCORRECT_PROMPT,
	AUTOCORRECT_IMMEDIATELY,
	AUTOCORRECT_DELAY,
};

/**
 * `mode` indicates which action will be performed by autocorrect_confirm().
 * `delay` is the timeout before autocorrect_confirm() returns, in tenths of a
 * second. Use it only with AUTOCORRECT_DELAY.
 */
struct autocorrect {
	enum autocorrect_mode mode;
	int delay;
};

/**
 * Resolve the autocorrect configuration into `conf`.
 */
void autocorrect_resolve(struct autocorrect *conf);

/**
 * Interact with the user in different ways depending on `conf->mode`.
 */
void autocorrect_confirm(struct autocorrect *conf, const char *assumed);

#endif /* AUTOCORRECT_H */
