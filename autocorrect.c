#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "autocorrect.h"
#include "config.h"
#include "parse.h"
#include "strbuf.h"
#include "prompt.h"
#include "gettext.h"

static enum autocorr_mode parse_autocorrect(const char *value)
{
	switch (git_parse_maybe_bool_text(value)) {
		case 1:
			return AUTOCORRECT_IMMEDIATELY;
		case 0:
			return AUTOCORRECT_HINTONLY;
		default: /* other random text */
			break;
	}

	if (!strcmp(value, "prompt"))
		return AUTOCORRECT_PROMPT;
	else if (!strcmp(value, "never"))
		return AUTOCORRECT_NEVER;
	else if (!strcmp(value, "immediate"))
		return AUTOCORRECT_IMMEDIATELY;
	else if (!strcmp(value, "show"))
		return AUTOCORRECT_HINTONLY;
	else
		return AUTOCORRECT_DELAY;
}

static int resolve_autocorr(const char *var, const char *value,
			    const struct config_context *ctx, void *data)
{
	struct autocorr *conf = data;

	if (strcmp(var, "help.autocorrect"))
		return 0;

	conf->mode = parse_autocorrect(value);

	/*
	 * Disable autocorrection prompt in a non-interactive session.
	 */
	if (conf->mode == AUTOCORRECT_PROMPT && (!isatty(0) || !isatty(2)))
		conf->mode = AUTOCORRECT_NEVER;

	if (conf->mode == AUTOCORRECT_DELAY) {
		conf->delay = git_config_int(var, value, ctx->kvi);

		if (!conf->delay)
			conf->mode = AUTOCORRECT_HINTONLY;
		else if (conf->delay <= 1)
			conf->mode = AUTOCORRECT_IMMEDIATELY;
	}

	return 0;
}

void autocorr_resolve(struct autocorr *conf)
{
	read_early_config(the_repository, resolve_autocorr, conf);
}

void autocorr_confirm(struct autocorr *conf, const char *assumed)
{
	if (conf->mode == AUTOCORRECT_IMMEDIATELY) {
		fprintf_ln(stderr,
			   _("Continuing under the assumption that you meant '%s'."),
			   assumed);
	} else if (conf->mode == AUTOCORRECT_PROMPT) {
		char *answer;
		struct strbuf msg = STRBUF_INIT;

		strbuf_addf(&msg, _("Run '%s' instead [y/N]? "), assumed);
		answer = git_prompt(msg.buf, PROMPT_ECHO);
		strbuf_release(&msg);

		if (!(starts_with(answer, "y") || starts_with(answer, "Y")))
			exit(1);
	} else if (conf->mode == AUTOCORRECT_DELAY) {
		fprintf_ln(stderr,
			   _("Continuing in %0.1f seconds, assuming that you meant '%s'."),
			   conf->delay / 10.0, assumed);
		sleep_millisec(conf->delay * 100);
	}
}
