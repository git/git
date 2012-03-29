#include "cache.h"

int advice_push_nonfastforward = 1;
int advice_push_non_ff_current = 1;
int advice_push_non_ff_default = 1;
int advice_push_non_ff_matching = 1;
int advice_status_hints = 1;
int advice_commit_before_merge = 1;
int advice_resolve_conflict = 1;
int advice_implicit_identity = 1;
int advice_detached_head = 1;

static struct {
	const char *name;
	int *preference;
} advice_config[] = {
	{ "pushnonfastforward", &advice_push_nonfastforward },
	{ "pushnonffcurrent", &advice_push_non_ff_current },
	{ "pushnonffdefault", &advice_push_non_ff_default },
	{ "pushnonffmatching", &advice_push_non_ff_matching },
	{ "statushints", &advice_status_hints },
	{ "commitbeforemerge", &advice_commit_before_merge },
	{ "resolveconflict", &advice_resolve_conflict },
	{ "implicitidentity", &advice_implicit_identity },
	{ "detachedhead", &advice_detached_head },
};

void advise(const char *advice, ...)
{
	struct strbuf buf = STRBUF_INIT;
	va_list params;
	const char *cp, *np;

	va_start(params, advice);
	strbuf_addf(&buf, advice, params);
	va_end(params);

	for (cp = buf.buf; *cp; cp = np) {
		np = strchrnul(cp, '\n');
		fprintf(stderr,	_("hint: %.*s\n"), (int)(np - cp), cp);
		if (*np)
			np++;
	}
	strbuf_release(&buf);
}

int git_default_advice_config(const char *var, const char *value)
{
	const char *k = skip_prefix(var, "advice.");
	int i;

	for (i = 0; i < ARRAY_SIZE(advice_config); i++) {
		if (strcmp(k, advice_config[i].name))
			continue;
		*advice_config[i].preference = git_config_bool(var, value);
		return 0;
	}

	return 0;
}

int error_resolve_conflict(const char *me)
{
	error("'%s' is not possible because you have unmerged files.", me);
	if (advice_resolve_conflict)
		/*
		 * Message used both when 'git commit' fails and when
		 * other commands doing a merge do.
		 */
		advise(_("Fix them up in the work tree,\n"
			 "and then use 'git add/rm <file>' as\n"
			 "appropriate to mark resolution and make a commit,\n"
			 "or use 'git commit -a'."));
	return -1;
}

void NORETURN die_resolve_conflict(const char *me)
{
	error_resolve_conflict(me);
	die("Exiting because of an unresolved conflict.");
}

void detach_advice(const char *new_name)
{
	const char fmt[] =
	"Note: checking out '%s'.\n\n"
	"You are in 'detached HEAD' state. You can look around, make experimental\n"
	"changes and commit them, and you can discard any commits you make in this\n"
	"state without impacting any branches by performing another checkout.\n\n"
	"If you want to create a new branch to retain commits you create, you may\n"
	"do so (now or later) by using -b with the checkout command again. Example:\n\n"
	"  git checkout -b new_branch_name\n\n";

	fprintf(stderr, fmt, new_name);
}
