#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "version.h"
#include "strbuf.h"
#include "gettext.h"

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

/*
  Retrieve, sanitize and cache operating system info for subsequent
  calls. Return a pointer to the sanitized operating system info
  string.
*/
static const char *os_info(void)
{
	static const char *os = NULL;

	if (!os) {
		struct strbuf buf = STRBUF_INIT;

		get_uname_info(&buf, 0);
		/* Sanitize the os information immediately */
		redact_non_printables(&buf);
		os = strbuf_detach(&buf, NULL);
	}

	return os;
}

const char *git_user_agent_sanitized(void)
{
	static const char *agent = NULL;

	if (!agent) {
		struct strbuf buf = STRBUF_INIT;

		strbuf_addstr(&buf, git_user_agent());

		if (!getenv("GIT_USER_AGENT")) {
			strbuf_addch(&buf, '-');
			strbuf_addstr(&buf, os_info());
		}
		redact_non_printables(&buf);
		agent = strbuf_detach(&buf, NULL);
	}

	return agent;
}

int get_uname_info(struct strbuf *buf, unsigned int full)
{
	struct utsname uname_info;

	if (uname(&uname_info)) {
		strbuf_addf(buf, _("uname() failed with error '%s' (%d)\n"),
			    strerror(errno),
			    errno);
		return -1;
	}
	if (full)
		strbuf_addf(buf, "%s %s %s %s\n",
			    uname_info.sysname,
			    uname_info.release,
			    uname_info.version,
			    uname_info.machine);
	else
	     strbuf_addf(buf, "%s\n", uname_info.sysname);
	return 0;
}
