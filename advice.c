#include "cache.h"
#include "config.h"
#include "color.h"

int advice_push_update_rejected = 1;
int advice_push_non_ff_current = 1;
int advice_push_non_ff_matching = 1;
int advice_push_already_exists = 1;
int advice_push_fetch_first = 1;
int advice_push_needs_force = 1;
int advice_status_hints = 1;
int advice_status_u_option = 1;
int advice_commit_before_merge = 1;
int advice_resolve_conflict = 1;
int advice_implicit_identity = 1;
int advice_detached_head = 1;
int advice_set_upstream_failure = 1;
int advice_object_name_warning = 1;
int advice_rm_hints = 1;
int advice_add_embedded_repo = 1;
int advice_ignored_hook = 1;
int advice_waiting_for_editor = 1;

static int advice_use_color = -1;
static char advice_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_YELLOW,	/* HINT */
};

enum color_advice {
	ADVICE_COLOR_RESET = 0,
	ADVICE_COLOR_HINT = 1,
};

static int parse_advise_color_slot(const char *slot)
{
	if (!strcasecmp(slot, "reset"))
		return ADVICE_COLOR_RESET;
	if (!strcasecmp(slot, "hint"))
		return ADVICE_COLOR_HINT;
	return -1;
}

static const char *advise_get_color(enum color_advice ix)
{
	if (want_color_stderr(advice_use_color))
		return advice_colors[ix];
	return "";
}

static struct {
	const char *name;
	int *preference;
} advice_config[] = {
	{ "pushupdaterejected", &advice_push_update_rejected },
	{ "pushnonffcurrent", &advice_push_non_ff_current },
	{ "pushnonffmatching", &advice_push_non_ff_matching },
	{ "pushalreadyexists", &advice_push_already_exists },
	{ "pushfetchfirst", &advice_push_fetch_first },
	{ "pushneedsforce", &advice_push_needs_force },
	{ "statushints", &advice_status_hints },
	{ "statusuoption", &advice_status_u_option },
	{ "commitbeforemerge", &advice_commit_before_merge },
	{ "resolveconflict", &advice_resolve_conflict },
	{ "implicitidentity", &advice_implicit_identity },
	{ "detachedhead", &advice_detached_head },
	{ "setupstreamfailure", &advice_set_upstream_failure },
	{ "objectnamewarning", &advice_object_name_warning },
	{ "rmhints", &advice_rm_hints },
	{ "addembeddedrepo", &advice_add_embedded_repo },
	{ "ignoredhook", &advice_ignored_hook },
	{ "waitingforeditor", &advice_waiting_for_editor },

	/* make this an alias for backward compatibility */
	{ "pushnonfastforward", &advice_push_update_rejected }
};

void advise(const char *advice, ...)
{
	struct strbuf buf = STRBUF_INIT;
	va_list params;
	const char *cp, *np;

	va_start(params, advice);
	strbuf_vaddf(&buf, advice, params);
	va_end(params);

	for (cp = buf.buf; *cp; cp = np) {
		np = strchrnul(cp, '\n');
		fprintf(stderr,	_("%shint: %.*s%s\n"),
			advise_get_color(ADVICE_COLOR_HINT),
			(int)(np - cp), cp,
			advise_get_color(ADVICE_COLOR_RESET));
		if (*np)
			np++;
	}
	strbuf_release(&buf);
}

int git_default_advice_config(const char *var, const char *value)
{
	const char *k, *slot_name;
	int i;

	if (!strcmp(var, "color.advice")) {
		advice_use_color = git_config_colorbool(var, value);
		return 0;
	}

	if (skip_prefix(var, "color.advice.", &slot_name)) {
		int slot = parse_advise_color_slot(slot_name);
		if (slot < 0)
			return 0;
		if (!value)
			return config_error_nonbool(var);
		return color_parse(value, advice_colors[slot]);
	}

	if (!skip_prefix(var, "advice.", &k))
		return 0;

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
	if (!strcmp(me, "cherry-pick"))
		error(_("Cherry-picking is not possible because you have unmerged files."));
	else if (!strcmp(me, "commit"))
		error(_("Committing is not possible because you have unmerged files."));
	else if (!strcmp(me, "merge"))
		error(_("Merging is not possible because you have unmerged files."));
	else if (!strcmp(me, "pull"))
		error(_("Pulling is not possible because you have unmerged files."));
	else if (!strcmp(me, "revert"))
		error(_("Reverting is not possible because you have unmerged files."));
	else
		error(_("It is not possible to %s because you have unmerged files."),
			me);

	if (advice_resolve_conflict)
		/*
		 * Message used both when 'git commit' fails and when
		 * other commands doing a merge do.
		 */
		advise(_("Fix them up in the work tree, and then use 'git add/rm <file>'\n"
			 "as appropriate to mark resolution and make a commit."));
	return -1;
}

void NORETURN die_resolve_conflict(const char *me)
{
	error_resolve_conflict(me);
	die(_("Exiting because of an unresolved conflict."));
}

void NORETURN die_conclude_merge(void)
{
	error(_("You have not concluded your merge (MERGE_HEAD exists)."));
	if (advice_resolve_conflict)
		advise(_("Please, commit your changes before merging."));
	die(_("Exiting because of unfinished merge."));
}

void detach_advice(const char *new_name)
{
	const char *fmt =
	_("Note: checking out '%s'.\n\n"
	"You are in 'detached HEAD' state. You can look around, make experimental\n"
	"changes and commit them, and you can discard any commits you make in this\n"
	"state without impacting any branches by performing another checkout.\n\n"
	"If you want to create a new branch to retain commits you create, you may\n"
	"do so (now or later) by using -b with the checkout command again. Example:\n\n"
	"  git checkout -b <new-branch-name>\n\n");

	fprintf(stderr, fmt, new_name);
}
