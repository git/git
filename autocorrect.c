#include "git-compat-util.h"
#include "autocorrect.h"
#include "config.h"
#include "parse.h"
#include "strbuf.h"
#include "prompt.h"
#include "gettext.h"

static int parse_autocorrect(const char *value)
{
	switch (git_parse_maybe_bool_text(value)) {
	case 1:
		return AUTOCORRECT_IMMEDIATELY;
	case 0:
		return AUTOCORRECT_SHOW;
	default: /* other random text */
		break;
	}

	if (!strcmp(value, "prompt"))
		return AUTOCORRECT_PROMPT;
	if (!strcmp(value, "never"))
		return AUTOCORRECT_NEVER;
	if (!strcmp(value, "immediate"))
		return AUTOCORRECT_IMMEDIATELY;
	if (!strcmp(value, "show"))
		return AUTOCORRECT_SHOW;

	return 0;
}

void autocorrect_resolve_config(const char *var, const char *value,
				const struct config_context *ctx, void *data)
{
	int *out = data;
	int parsed;

	if (strcmp(var, "help.autocorrect"))
		return;

	parsed = parse_autocorrect(value);

	/*
	 * Disable autocorrection prompt in a non-interactive session
	 */
	if (parsed == AUTOCORRECT_PROMPT && (!isatty(0) || !isatty(2)))
		parsed = AUTOCORRECT_NEVER;

	if (!parsed) {
		parsed = git_config_int(var, value, ctx->kvi);
		if (parsed < 0 || parsed == 1)
			parsed = AUTOCORRECT_IMMEDIATELY;
	}

	*out = parsed;
}

void autocorrect_confirm(int autocorrect, const char *assumed)
{
	if (autocorrect == AUTOCORRECT_IMMEDIATELY) {
		fprintf_ln(stderr,
			   _("Continuing under the assumption that you meant '%s'."),
			   assumed);
	} else if (autocorrect == AUTOCORRECT_PROMPT) {
		char *answer;
		struct strbuf msg = STRBUF_INIT;

		strbuf_addf(&msg, _("Run '%s' instead [y/N]? "), assumed);
		answer = git_prompt(msg.buf, PROMPT_ECHO);
		strbuf_release(&msg);

		if (!(starts_with(answer, "y") || starts_with(answer, "Y")))
			exit(1);
	} else {
		fprintf_ln(stderr,
			   _("Continuing in %0.1f seconds, assuming that you meant '%s'."),
			   (float)autocorrect / 10.0, assumed);
		sleep_millisec(autocorrect * 100);
	}
}
