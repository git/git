#ifndef AUTOCORRECT_H
#define AUTOCORRECT_H

struct config_context;

enum autocorrect_mode {
	AUTOCORRECT_SHOW,
	AUTOCORRECT_NEVER,
	AUTOCORRECT_PROMPT,
	AUTOCORRECT_IMMEDIATELY,
	AUTOCORRECT_DELAY,
};

struct autocorrect {
	enum autocorrect_mode mode;
	int delay;
};

void autocorrect_resolve_config(const char *var, const char *value,
				const struct config_context *ctx, void *data);

void autocorrect_confirm(struct autocorrect *conf, const char *assumed);

#endif /* AUTOCORRECT_H */
