#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "advice.h"
#include "config.h"
#include "color.h"
#include "environment.h"
#include "gettext.h"
#include "help.h"
#include "string-list.h"

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

enum advice_level {
	ADVICE_LEVEL_NONE = 0,
	ADVICE_LEVEL_DISABLED,
	ADVICE_LEVEL_ENABLED,
};

static struct {
	const char *key;
	enum advice_level level;
} advice_setting[] = {
	[ADVICE_ADD_EMBEDDED_REPO]			= { "addEmbeddedRepo" },
	[ADVICE_ADD_EMPTY_PATHSPEC]			= { "addEmptyPathspec" },
	[ADVICE_ADD_IGNORED_FILE]			= { "addIgnoredFile" },
	[ADVICE_AMBIGUOUS_FETCH_REFSPEC]		= { "ambiguousFetchRefspec" },
	[ADVICE_AM_WORK_DIR] 				= { "amWorkDir" },
	[ADVICE_CHECKOUT_AMBIGUOUS_REMOTE_BRANCH_NAME] 	= { "checkoutAmbiguousRemoteBranchName" },
	[ADVICE_COMMIT_BEFORE_MERGE]			= { "commitBeforeMerge" },
	[ADVICE_DETACHED_HEAD]				= { "detachedHead" },
	[ADVICE_DIVERGING]				= { "diverging" },
	[ADVICE_FETCH_SHOW_FORCED_UPDATES]		= { "fetchShowForcedUpdates" },
	[ADVICE_FORCE_DELETE_BRANCH]			= { "forceDeleteBranch" },
	[ADVICE_GRAFT_FILE_DEPRECATED]			= { "graftFileDeprecated" },
	[ADVICE_IGNORED_HOOK]				= { "ignoredHook" },
	[ADVICE_IMPLICIT_IDENTITY]			= { "implicitIdentity" },
	[ADVICE_MERGE_CONFLICT]				= { "mergeConflict" },
	[ADVICE_NESTED_TAG]				= { "nestedTag" },
	[ADVICE_OBJECT_NAME_WARNING]			= { "objectNameWarning" },
	[ADVICE_PUSH_ALREADY_EXISTS]			= { "pushAlreadyExists" },
	[ADVICE_PUSH_FETCH_FIRST]			= { "pushFetchFirst" },
	[ADVICE_PUSH_NEEDS_FORCE]			= { "pushNeedsForce" },
	[ADVICE_PUSH_NON_FF_CURRENT]			= { "pushNonFFCurrent" },
	[ADVICE_PUSH_NON_FF_MATCHING]			= { "pushNonFFMatching" },
	[ADVICE_PUSH_REF_NEEDS_UPDATE]			= { "pushRefNeedsUpdate" },
	[ADVICE_PUSH_UNQUALIFIED_REF_NAME]		= { "pushUnqualifiedRefName" },
	[ADVICE_PUSH_UPDATE_REJECTED]			= { "pushUpdateRejected" },
	[ADVICE_PUSH_UPDATE_REJECTED_ALIAS]		= { "pushNonFastForward" }, /* backwards compatibility */
	[ADVICE_REBASE_TODO_ERROR]			= { "rebaseTodoError" },
	[ADVICE_REF_SYNTAX]				= { "refSyntax" },
	[ADVICE_RESET_NO_REFRESH_WARNING]		= { "resetNoRefresh" },
	[ADVICE_RESOLVE_CONFLICT]			= { "resolveConflict" },
	[ADVICE_RM_HINTS]				= { "rmHints" },
	[ADVICE_SEQUENCER_IN_USE]			= { "sequencerInUse" },
	[ADVICE_SET_UPSTREAM_FAILURE]			= { "setUpstreamFailure" },
	[ADVICE_SKIPPED_CHERRY_PICKS]			= { "skippedCherryPicks" },
	[ADVICE_SPARSE_INDEX_EXPANDED]			= { "sparseIndexExpanded" },
	[ADVICE_STATUS_AHEAD_BEHIND_WARNING]		= { "statusAheadBehindWarning" },
	[ADVICE_STATUS_HINTS]				= { "statusHints" },
	[ADVICE_STATUS_U_OPTION]			= { "statusUoption" },
	[ADVICE_SUBMODULES_NOT_UPDATED] 		= { "submodulesNotUpdated" },
	[ADVICE_SUBMODULE_ALTERNATE_ERROR_STRATEGY_DIE] = { "submoduleAlternateErrorStrategyDie" },
	[ADVICE_SUBMODULE_MERGE_CONFLICT]               = { "submoduleMergeConflict" },
	[ADVICE_SUGGEST_DETACHING_HEAD]			= { "suggestDetachingHead" },
	[ADVICE_UPDATE_SPARSE_PATH]			= { "updateSparsePath" },
	[ADVICE_WAITING_FOR_EDITOR]			= { "waitingForEditor" },
	[ADVICE_WORKTREE_ADD_ORPHAN]			= { "worktreeAddOrphan" },
};

static const char turn_off_instructions[] =
N_("\n"
   "Disable this message with \"git config advice.%s false\"");

static void vadvise(const char *advice, int display_instructions,
		    const char *key, va_list params)
{
	struct strbuf buf = STRBUF_INIT;
	const char *cp, *np;

	strbuf_vaddf(&buf, advice, params);

	if (display_instructions)
		strbuf_addf(&buf, turn_off_instructions, key);

	for (cp = buf.buf; *cp; cp = np) {
		np = strchrnul(cp, '\n');
		fprintf(stderr,	_("%shint:%s%.*s%s\n"),
			advise_get_color(ADVICE_COLOR_HINT),
			(np == cp) ? "" : " ",
			(int)(np - cp), cp,
			advise_get_color(ADVICE_COLOR_RESET));
		if (*np)
			np++;
	}
	strbuf_release(&buf);
}

void advise(const char *advice, ...)
{
	va_list params;
	va_start(params, advice);
	vadvise(advice, 0, "", params);
	va_end(params);
}

int advice_enabled(enum advice_type type)
{
	int enabled = advice_setting[type].level != ADVICE_LEVEL_DISABLED;
	static int globally_enabled = -1;

	if (globally_enabled < 0)
		globally_enabled = git_env_bool(GIT_ADVICE_ENVIRONMENT, 1);
	if (!globally_enabled)
		return 0;

	if (type == ADVICE_PUSH_UPDATE_REJECTED)
		return enabled &&
		       advice_enabled(ADVICE_PUSH_UPDATE_REJECTED_ALIAS);

	return enabled;
}

void advise_if_enabled(enum advice_type type, const char *advice, ...)
{
	va_list params;

	if (!advice_enabled(type))
		return;

	va_start(params, advice);
	vadvise(advice, !advice_setting[type].level, advice_setting[type].key,
		params);
	va_end(params);
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

	for (i = 0; i < ARRAY_SIZE(advice_setting); i++) {
		if (strcasecmp(k, advice_setting[i].key))
			continue;
		advice_setting[i].level = git_config_bool(var, value)
					  ? ADVICE_LEVEL_ENABLED
					  : ADVICE_LEVEL_DISABLED;
		return 0;
	}

	return 0;
}

void list_config_advices(struct string_list *list, const char *prefix)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(advice_setting); i++)
		list_config_item(list, prefix, advice_setting[i].key);
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
	else if (!strcmp(me, "rebase"))
		error(_("Rebasing is not possible because you have unmerged files."));
	else
		BUG("Unhandled conflict reason '%s'", me);

	if (advice_enabled(ADVICE_RESOLVE_CONFLICT))
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
	if (advice_enabled(ADVICE_RESOLVE_CONFLICT))
		advise(_("Please, commit your changes before merging."));
	die(_("Exiting because of unfinished merge."));
}

void NORETURN die_ff_impossible(void)
{
	advise_if_enabled(ADVICE_DIVERGING,
		_("Diverging branches can't be fast-forwarded, you need to either:\n"
		"\n"
		"\tgit merge --no-ff\n"
		"\n"
		"or:\n"
		"\n"
		"\tgit rebase\n"));
	die(_("Not possible to fast-forward, aborting."));
}

void advise_on_updating_sparse_paths(struct string_list *pathspec_list)
{
	struct string_list_item *item;

	if (!pathspec_list->nr)
		return;

	fprintf(stderr, _("The following paths and/or pathspecs matched paths that exist\n"
			  "outside of your sparse-checkout definition, so will not be\n"
			  "updated in the index:\n"));
	for_each_string_list_item(item, pathspec_list)
		fprintf(stderr, "%s\n", item->string);

	advise_if_enabled(ADVICE_UPDATE_SPARSE_PATH,
			  _("If you intend to update such entries, try one of the following:\n"
			    "* Use the --sparse option.\n"
			    "* Disable or modify the sparsity rules."));
}

void detach_advice(const char *new_name)
{
	const char *fmt =
	_("Note: switching to '%s'.\n"
	"\n"
	"You are in 'detached HEAD' state. You can look around, make experimental\n"
	"changes and commit them, and you can discard any commits you make in this\n"
	"state without impacting any branches by switching back to a branch.\n"
	"\n"
	"If you want to create a new branch to retain commits you create, you may\n"
	"do so (now or later) by using -c with the switch command. Example:\n"
	"\n"
	"  git switch -c <new-branch-name>\n"
	"\n"
	"Or undo this operation with:\n"
	"\n"
	"  git switch -\n"
	"\n"
	"Turn off this advice by setting config variable advice.detachedHead to false\n\n");

	fprintf(stderr, fmt, new_name);
}

void advise_on_moving_dirty_path(struct string_list *pathspec_list)
{
	struct string_list_item *item;

	if (!pathspec_list->nr)
		return;

	fprintf(stderr, _("The following paths have been moved outside the\n"
			  "sparse-checkout definition but are not sparse due to local\n"
			  "modifications.\n"));
	for_each_string_list_item(item, pathspec_list)
		fprintf(stderr, "%s\n", item->string);

	advise_if_enabled(ADVICE_UPDATE_SPARSE_PATH,
			  _("To correct the sparsity of these paths, do the following:\n"
			    "* Use \"git add --sparse <paths>\" to update the index\n"
			    "* Use \"git sparse-checkout reapply\" to apply the sparsity rules"));
}
