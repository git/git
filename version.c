#include "git-compat-util.h"
#include "version.h"
#include "strbuf.h"
#include "sane-ctype.h"

#ifndef GIT_VERSION_H
# include "version-def.h"
#else
# include GIT_VERSION_H
#endif

const char git_version_string[] = GIT_VERSION;
const char git_built_from_commit_string[] = GIT_BUILT_FROM_COMMIT;

/*
 * Trim and replace each character with ascii code below 32 or above
 * 127 (included) using a dot '.' character.
 */
static void redact_non_printables(struct strbuf *buf)
{
	strbuf_trim(buf);
	for (size_t i = 0; i < buf->len; i++) {
		if (!isprint(buf->buf[i]) || buf->buf[i] == ' ')
			buf->buf[i] = '.';
	}
}

const char *git_user_agent(void)
{
	static const char *agent = NULL;

	if (!agent) {
		agent = getenv("GIT_USER_AGENT");
		if (!agent)
			agent = GIT_USER_AGENT;
	}

	return agent;
}

const char *git_user_agent_sanitized(void)
{
	static const char *agent = NULL;

	if (!agent) {
		struct strbuf buf = STRBUF_INIT;

		strbuf_addstr(&buf, git_user_agent());
		redact_non_printables(&buf);
		agent = strbuf_detach(&buf, NULL);
	}

	return agent;
}
