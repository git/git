#ifndef AUTOCORRECT_H
#define AUTOCORRECT_H

struct config_context;

enum autocorr_mode {
	AUTOCORRECT_HINTONLY,
	AUTOCORRECT_NEVER,
	AUTOCORRECT_PROMPT,
	AUTOCORRECT_IMMEDIATELY,
	AUTOCORRECT_DELAY,
};

struct autocorr {
	enum autocorr_mode mode;
	int delay;
};

void autocorr_resolve_config(const char *var, const char *value,
			     const struct config_context *ctx, void *data);

void autocorr_confirm(struct autocorr *conf, const char *assumed);

#endif /* AUTOCORRECT_H */
