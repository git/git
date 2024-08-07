#include "git-compat-util.h"
#include "strbuf.h"
#include "trace2/tr2_cmd_name.h"

#define TR2_ENVVAR_PARENT_NAME "GIT_TRACE2_PARENT_NAME"

static struct strbuf tr2cmdname_hierarchy = STRBUF_INIT;

void tr2_cmd_name_append_hierarchy(const char *name)
{
	const char *parent_name = getenv(TR2_ENVVAR_PARENT_NAME);

	strbuf_reset(&tr2cmdname_hierarchy);
	if (parent_name && *parent_name) {
		strbuf_addstr(&tr2cmdname_hierarchy, parent_name);
		strbuf_addch(&tr2cmdname_hierarchy, '/');
	}
	strbuf_addstr(&tr2cmdname_hierarchy, name);

	setenv(TR2_ENVVAR_PARENT_NAME, tr2cmdname_hierarchy.buf, 1);
}

const char *tr2_cmd_name_get_hierarchy(void)
{
	return tr2cmdname_hierarchy.buf;
}

void tr2_cmd_name_release(void)
{
	strbuf_release(&tr2cmdname_hierarchy);
}
