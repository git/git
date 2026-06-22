#include "git-compat-util.h"
#include "rebase.h"
#include "parse.h"
#include "gettext.h"

/*
 * Parses textual value for pull.rebase, branch.<name>.rebase, etc.
 * Unrecognised value yields REBASE_INVALID, which traditionally is
 * treated the same way as REBASE_FALSE.
 *
 * The callers that care if (any) rebase is requested should say
 *   if (REBASE_TRUE <= rebase_parse_value(string))
 *
 * The callers that want to differentiate an unrecognised value and
 * false can do so by treating _INVALID and _FALSE differently.
 */
enum rebase_type rebase_parse_value(const char *value)
{
	int v = git_parse_maybe_bool(value);

	if (!v)
		return REBASE_FALSE;
	else if (v > 0)
		return REBASE_TRUE;
	else if (!strcmp(value, "merges") || !strcmp(value, "m"))
		return REBASE_MERGES;
	else if (!strcmp(value, "interactive") || !strcmp(value, "i"))
		return REBASE_INTERACTIVE;
	else if (!strcmp(value, "preserve") || !strcmp(value, "p"))
		error(_("%s: 'preserve' superseded by 'merges'"), value);
	/*
	 * Please update _git_config() in git-completion.bash when you
	 * add new rebase modes.
	 */

	return REBASE_INVALID;
}
