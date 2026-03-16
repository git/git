#ifndef AUTOCORRECT_H
#define AUTOCORRECT_H

#define AUTOCORRECT_SHOW (-4)
#define AUTOCORRECT_PROMPT (-3)
#define AUTOCORRECT_NEVER (-2)
#define AUTOCORRECT_IMMEDIATELY (-1)

struct config_context;

void autocorrect_resolve_config(const char *var, const char *value,
				const struct config_context *ctx, void *data);

void autocorrect_confirm(int autocorrect, const char *assumed);

#endif /* AUTOCORRECT_H */
