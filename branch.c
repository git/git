#include "git-compat-util.h"
#include "advice.h"
#include "config.h"
#include "branch.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "object-name.h"
#include "path.h"
#include "refs.h"
#include "refspec.h"
#include "remote.h"
#include "repository.h"
#include "sequencer.h"
#include "commit.h"
#include "worktree.h"
#include "submodule-config.h"
#include "run-command.h"
#include "strmap.h"

struct tracking {
	struct refspec_item spec;
	struct string_list *srcs;
	const char *remote;
	int matches;
};

struct find_tracked_branch_cb {
	struct tracking *tracking;
	struct string_list ambiguous_remotes;
};

static int find_tracked_branch(struct remote *remote, void *priv)
{
	struct find_tracked_branch_cb *ftb = priv;
	struct tracking *tracking = ftb->tracking;

	if (!remote_find_tracking(remote, &tracking->spec)) {
		switch (++tracking->matches) {
		case 1:
			string_list_append_nodup(tracking->srcs, tracking->spec.src);
			tracking->remote = remote->name;
			break;
		case 2:
			/* there are at least two remotes; backfill the first one */
			string_list_append(&ftb->ambiguous_remotes, tracking->remote);
			/* fall through */
		default:
			string_list_append(&ftb->ambiguous_remotes, remote->name);
			free(tracking->spec.src);
			string_list_clear(tracking->srcs, 0);
		break;
		}
		/* remote_find_tracking() searches by src if present */
		tracking->spec.src = NULL;
	}
	return 0;
}

static int should_setup_rebase(const char *origin)
{
	switch (autorebase) {
	case AUTOREBASE_NEVER:
		return 0;
	case AUTOREBASE_LOCAL:
		return origin == NULL;
	case AUTOREBASE_REMOTE:
		return origin != NULL;
	case AUTOREBASE_ALWAYS:
		return 1;
	}
	return 0;
}

/**
 * Install upstream tracking configuration for a branch; specifically, add
 * `branch.<name>.remote` and `branch.<name>.merge` entries.
 *
 * `flag` contains integer flags for options; currently only
 * BRANCH_CONFIG_VERBOSE is checked.
 *
 * `local` is the name of the branch whose configuration we're installing.
 *
 * `origin` is the name of the remote owning the upstream branches. NULL means
 * the upstream branches are local to this repo.
 *
 * `remotes` is a list of refs that are upstream of local
 */
static int install_branch_config_multiple_remotes(int flag, const char *local,
		const char *origin, struct string_list *remotes)
{
	const char *shortname = NULL;
	struct strbuf key = STRBUF_INIT;
	struct string_list_item *item;
	int rebasing = should_setup_rebase(origin);

	if (!remotes->nr)
		BUG("must provide at least one remote for branch config");
	if (rebasing && remotes->nr > 1)
		die(_("cannot inherit upstream tracking configuration of "
		      "multiple refs when rebasing is requested"));

	/*
	 * If the new branch is trying to track itself, something has gone
	 * wrong. Warn the user and don't proceed any further.
	 */
	if (!origin)
		for_each_string_list_item(item, remotes)
			if (skip_prefix(item->string, "refs/heads/", &shortname)
			    && !strcmp(local, shortname)) {
				warning(_("not setting branch '%s' as its own upstream"),
					local);
				return 0;
			}

	strbuf_addf(&key, "branch.%s.remote", local);
	if (git_config_set_gently(key.buf, origin ? origin : ".") < 0)
		goto out_err;

	strbuf_reset(&key);
	strbuf_addf(&key, "branch.%s.merge", local);
	/*
	 * We want to overwrite any existing config with all the branches in
	 * "remotes". Override any existing config, then write our branches. If
	 * more than one is provided, use CONFIG_REGEX_NONE to preserve what
	 * we've written so far.
	 */
	if (git_config_set_gently(key.buf, NULL) < 0)
		goto out_err;
	for_each_string_list_item(item, remotes)
		if (git_config_set_multivar_gently(key.buf, item->string, CONFIG_REGEX_NONE, 0) < 0)
			goto out_err;

	if (rebasing) {
		strbuf_reset(&key);
		strbuf_addf(&key, "branch.%s.rebase", local);
		if (git_config_set_gently(key.buf, "true") < 0)
			goto out_err;
	}
	strbuf_release(&key);

	if (flag & BRANCH_CONFIG_VERBOSE) {
		struct strbuf tmp_ref_name = STRBUF_INIT;
		struct string_list friendly_ref_names = STRING_LIST_INIT_DUP;

		for_each_string_list_item(item, remotes) {
			shortname = item->string;
			skip_prefix(shortname, "refs/heads/", &shortname);
			if (origin) {
				strbuf_addf(&tmp_ref_name, "%s/%s",
					    origin, shortname);
				string_list_append_nodup(
					&friendly_ref_names,
					strbuf_detach(&tmp_ref_name, NULL));
			} else {
				string_list_append(
					&friendly_ref_names, shortname);
			}
		}

		if (remotes->nr == 1) {
			/*
			 * Rebasing is only allowed in the case of a single
			 * upstream branch.
			 */
			printf_ln(rebasing ?
				_("branch '%s' set up to track '%s' by rebasing.") :
				_("branch '%s' set up to track '%s'."),
				local, friendly_ref_names.items[0].string);
		} else {
			printf_ln(_("branch '%s' set up to track:"), local);
			for_each_string_list_item(item, &friendly_ref_names)
				printf_ln("  %s", item->string);
		}

		string_list_clear(&friendly_ref_names, 0);
	}

	return 0;

out_err:
	strbuf_release(&key);
	error(_("unable to write upstream branch configuration"));

	advise(_("\nAfter fixing the error cause you may try to fix up\n"
		"the remote tracking information by invoking:"));
	if (remotes->nr == 1)
		advise("  git branch --set-upstream-to=%s%s%s",
			origin ? origin : "",
			origin ? "/" : "",
			remotes->items[0].string);
	else {
		advise("  git config --add branch.\"%s\".remote %s",
			local, origin ? origin : ".");
		for_each_string_list_item(item, remotes)
			advise("  git config --add branch.\"%s\".merge %s",
				local, item->string);
	}

	return -1;
}

int install_branch_config(int flag, const char *local, const char *origin,
		const char *remote)
{
	int ret;
	struct string_list remotes = STRING_LIST_INIT_DUP;

	string_list_append(&remotes, remote);
	ret = install_branch_config_multiple_remotes(flag, local, origin, &remotes);
	string_list_clear(&remotes, 0);
	return ret;
}

static int inherit_tracking(struct tracking *tracking, const char *orig_ref)
{
	const char *bare_ref;
	struct branch *branch;
	int i;

	bare_ref = orig_ref;
	skip_prefix(orig_ref, "refs/heads/", &bare_ref);

	branch = branch_get(bare_ref);
	if (!branch->remote_name) {
		warning(_("asked to inherit tracking from '%s', but no remote is set"),
			bare_ref);
		return -1;
	}

	if (branch->merge_nr < 1 || !branch->merge_name || !branch->merge_name[0]) {
		warning(_("asked to inherit tracking from '%s', but no merge configuration is set"),
			bare_ref);
		return -1;
	}

	tracking->remote = branch->remote_name;
	for (i = 0; i < branch->merge_nr; i++)
		string_list_append(tracking->srcs, branch->merge_name[i]);
	return 0;
}

/*
 * Used internally to set the branch.<new_ref>.{remote,merge} config
 * settings so that branch 'new_ref' tracks 'orig_ref'. Unlike
 * dwim_and_setup_tracking(), this does not do DWIM, i.e. "origin/main"
 * will not be expanded to "refs/remotes/origin/main", so it is not safe
 * for 'orig_ref' to be raw user input.
 */
static void setup_tracking(const char *new_ref, const char *orig_ref,
			   enum branch_track track, int quiet)
{
	struct tracking tracking;
	struct string_list tracking_srcs = STRING_LIST_INIT_DUP;
	int config_flags = quiet ? 0 : BRANCH_CONFIG_VERBOSE;
	struct find_tracked_branch_cb ftb_cb = {
		.tracking = &tracking,
		.ambiguous_remotes = STRING_LIST_INIT_DUP,
	};

	if (!track)
		BUG("asked to set up tracking, but tracking is disallowed");

	memset(&tracking, 0, sizeof(tracking));
	tracking.spec.dst = (char *)orig_ref;
	tracking.srcs = &tracking_srcs;
	if (track != BRANCH_TRACK_INHERIT)
		for_each_remote(find_tracked_branch, &ftb_cb);
	else if (inherit_tracking(&tracking, orig_ref))
		goto cleanup;

	if (!tracking.matches)
		switch (track) {
		/* If ref is not remote, still use local */
		case BRANCH_TRACK_ALWAYS:
		case BRANCH_TRACK_EXPLICIT:
		case BRANCH_TRACK_OVERRIDE:
		/* Remote matches not evaluated */
		case BRANCH_TRACK_INHERIT:
			break;
		/* Otherwise, if no remote don't track */
		default:
			goto cleanup;
		}

	/*
	 * This check does not apply to BRANCH_TRACK_INHERIT;
	 * that supports multiple entries in tracking_srcs but
	 * leaves tracking.matches at 0.
	 */
	if (tracking.matches > 1) {
		int status = die_message(_("not tracking: ambiguous information for ref '%s'"),
					    orig_ref);
		if (advice_enabled(ADVICE_AMBIGUOUS_FETCH_REFSPEC)) {
			struct strbuf remotes_advice = STRBUF_INIT;
			struct string_list_item *item;

			for_each_string_list_item(item, &ftb_cb.ambiguous_remotes)
				/*
				 * TRANSLATORS: This is a line listing a remote with duplicate
				 * refspecs in the advice message below. For RTL languages you'll
				 * probably want to swap the "%s" and leading "  " space around.
				 */
				strbuf_addf(&remotes_advice, _("  %s\n"), item->string);

			/*
			 * TRANSLATORS: The second argument is a \n-delimited list of
			 * duplicate refspecs, composed above.
			 */
			advise(_("There are multiple remotes whose fetch refspecs map to the remote\n"
				 "tracking ref '%s':\n"
				 "%s"
				 "\n"
				 "This is typically a configuration error.\n"
				 "\n"
				 "To support setting up tracking branches, ensure that\n"
				 "different remotes' fetch refspecs map into different\n"
				 "tracking namespaces."), orig_ref,
			       remotes_advice.buf);
			strbuf_release(&remotes_advice);
		}
		exit(status);
	}

	if (track == BRANCH_TRACK_SIMPLE) {
		/*
		 * Only track if remote branch name matches.
		 * Reaching into items[0].string is safe because
		 * we know there is at least one and not more than
		 * one entry (because only BRANCH_TRACK_INHERIT can
		 * produce more than one entry).
		 */
		const char *tracked_branch;
		if (!skip_prefix(tracking.srcs->items[0].string,
				 "refs/heads/", &tracked_branch) ||
		    strcmp(tracked_branch, new_ref))
			goto cleanup;
	}

	if (tracking.srcs->nr < 1)
		string_list_append(tracking.srcs, orig_ref);
	if (install_branch_config_multiple_remotes(config_flags, new_ref,
				tracking.remote, tracking.srcs) < 0)
		exit(1);

cleanup:
	string_list_clear(&tracking_srcs, 0);
	string_list_clear(&ftb_cb.ambiguous_remotes, 0);
}

int read_branch_desc(struct strbuf *buf, const char *branch_name)
{
	char *v = NULL;
	struct strbuf name = STRBUF_INIT;
	strbuf_addf(&name, "branch.%s.description", branch_name);
	if (git_config_get_string(name.buf, &v)) {
		strbuf_release(&name);
		return -1;
	}
	strbuf_addstr(buf, v);
	free(v);
	strbuf_release(&name);
	return 0;
}

/*
 * Check if 'name' can be a valid name for a branch; die otherwise.
 * Return 1 if the named branch already exists; return 0 otherwise.
 * Fill ref with the full refname for the branch.
 */
int validate_branchname(const char *name, struct strbuf *ref)
{
	if (strbuf_check_branch_ref(ref, name))
		die(_("'%s' is not a valid branch name"), name);

	return ref_exists(ref->buf);
}

static int initialized_checked_out_branches;
static struct strmap current_checked_out_branches = STRMAP_INIT;

static void prepare_checked_out_branches(void)
{
	int i = 0;
	struct worktree **worktrees;

	if (initialized_checked_out_branches)
		return;
	initialized_checked_out_branches = 1;

	worktrees = get_worktrees();

	while (worktrees[i]) {
		char *old;
		struct wt_status_state state = { 0 };
		struct worktree *wt = worktrees[i++];
		struct string_list update_refs = STRING_LIST_INIT_DUP;

		if (wt->is_bare)
			continue;

		if (wt->head_ref) {
			old = strmap_put(&current_checked_out_branches,
					 wt->head_ref,
					 xstrdup(wt->path));
			free(old);
		}

		if (wt_status_check_rebase(wt, &state) &&
		    (state.rebase_in_progress || state.rebase_interactive_in_progress) &&
		    state.branch) {
			struct strbuf ref = STRBUF_INIT;
			strbuf_addf(&ref, "refs/heads/%s", state.branch);
			old = strmap_put(&current_checked_out_branches,
					 ref.buf,
					 xstrdup(wt->path));
			free(old);
			strbuf_release(&ref);
		}
		wt_status_state_free_buffers(&state);

		if (wt_status_check_bisect(wt, &state) &&
		    state.branch) {
			struct strbuf ref = STRBUF_INIT;
			strbuf_addf(&ref, "refs/heads/%s", state.branch);
			old = strmap_put(&current_checked_out_branches,
					 ref.buf,
					 xstrdup(wt->path));
			free(old);
			strbuf_release(&ref);
		}
		wt_status_state_free_buffers(&state);

		if (!sequencer_get_update_refs_state(get_worktree_git_dir(wt),
						     &update_refs)) {
			struct string_list_item *item;
			for_each_string_list_item(item, &update_refs) {
				old = strmap_put(&current_checked_out_branches,
						 item->string,
						 xstrdup(wt->path));
				free(old);
			}
			string_list_clear(&update_refs, 1);
		}
	}

	free_worktrees(worktrees);
}

const char *branch_checked_out(const char *refname)
{
	prepare_checked_out_branches();
	return strmap_get(&current_checked_out_branches, refname);
}

/*
 * Check if a branch 'name' can be created as a new branch; die otherwise.
 * 'force' can be used when it is OK for the named branch already exists.
 * Return 1 if the named branch already exists; return 0 otherwise.
 * Fill ref with the full refname for the branch.
 */
int validate_new_branchname(const char *name, struct strbuf *ref, int force)
{
	const char *path;
	if (!validate_branchname(name, ref))
		return 0;

	if (!force)
		die(_("a branch named '%s' already exists"),
		    ref->buf + strlen("refs/heads/"));

	if ((path = branch_checked_out(ref->buf)))
		die(_("cannot force update the branch '%s' "
		      "checked out at '%s'"),
		    ref->buf + strlen("refs/heads/"), path);

	return 1;
}

static int check_tracking_branch(struct remote *remote, void *cb_data)
{
	char *tracking_branch = cb_data;
	struct refspec_item query;
	int res;
	memset(&query, 0, sizeof(struct refspec_item));
	query.dst = tracking_branch;
	res = !remote_find_tracking(remote, &query);
	free(query.src);
	return res;
}

static int validate_remote_tracking_branch(char *ref)
{
	return !for_each_remote(check_tracking_branch, ref);
}

static const char upstream_not_branch[] =
N_("cannot set up tracking information; starting point '%s' is not a branch");
static const char upstream_missing[] =
N_("the requested upstream branch '%s' does not exist");
static const char upstream_advice[] =
N_("\n"
"If you are planning on basing your work on an upstream\n"
"branch that already exists at the remote, you may need to\n"
"run \"git fetch\" to retrieve it.\n"
"\n"
"If you are planning to push out a new local branch that\n"
"will track its remote counterpart, you may want to use\n"
"\"git push -u\" to set the upstream config as you push.");

/**
 * DWIMs a user-provided ref to determine the starting point for a
 * branch and validates it, where:
 *
 *   - r is the repository to validate the branch for
 *
 *   - start_name is the ref that we would like to test. This is
 *     expanded with DWIM and assigned to out_real_ref.
 *
 *   - track is the tracking mode of the new branch. If tracking is
 *     explicitly requested, start_name must be a branch (because
 *     otherwise start_name cannot be tracked)
 *
 *   - out_oid is an out parameter containing the object_id of start_name
 *
 *   - out_real_ref is an out parameter containing the full, 'real' form
 *     of start_name e.g. refs/heads/main instead of main
 *
 */
static void dwim_branch_start(struct repository *r, const char *start_name,
			   enum branch_track track, char **out_real_ref,
			   struct object_id *out_oid)
{
	struct commit *commit;
	struct object_id oid;
	char *real_ref;
	int explicit_tracking = 0;

	if (track == BRANCH_TRACK_EXPLICIT || track == BRANCH_TRACK_OVERRIDE)
		explicit_tracking = 1;

	real_ref = NULL;
	if (repo_get_oid_mb(r, start_name, &oid)) {
		if (explicit_tracking) {
			int code = die_message(_(upstream_missing), start_name);
			advise_if_enabled(ADVICE_SET_UPSTREAM_FAILURE,
					  _(upstream_advice));
			exit(code);
		}
		die(_("not a valid object name: '%s'"), start_name);
	}

	switch (repo_dwim_ref(r, start_name, strlen(start_name), &oid,
			      &real_ref, 0)) {
	case 0:
		/* Not branching from any existing branch */
		if (explicit_tracking)
			die(_(upstream_not_branch), start_name);
		break;
	case 1:
		/* Unique completion -- good, only if it is a real branch */
		if (!starts_with(real_ref, "refs/heads/") &&
		    validate_remote_tracking_branch(real_ref)) {
			if (explicit_tracking)
				die(_(upstream_not_branch), start_name);
			else
				FREE_AND_NULL(real_ref);
		}
		break;
	default:
		die(_("ambiguous object name: '%s'"), start_name);
		break;
	}

	if (!(commit = lookup_commit_reference(r, &oid)))
		die(_("not a valid branch point: '%s'"), start_name);
	if (out_real_ref) {
		*out_real_ref = real_ref;
		real_ref = NULL;
	}
	if (out_oid)
		oidcpy(out_oid, &commit->object.oid);

	FREE_AND_NULL(real_ref);
}

void create_branch(struct repository *r,
		   const char *name, const char *start_name,
		   int force, int clobber_head_ok, int reflog,
		   int quiet, enum branch_track track, int dry_run)
{
	struct object_id oid;
	char *real_ref;
	struct strbuf ref = STRBUF_INIT;
	int forcing = 0;
	struct ref_transaction *transaction;
	struct strbuf err = STRBUF_INIT;
	char *msg;

	if (track == BRANCH_TRACK_OVERRIDE)
		BUG("'track' cannot be BRANCH_TRACK_OVERRIDE. Did you mean to call dwim_and_setup_tracking()?");
	if (clobber_head_ok && !force)
		BUG("'clobber_head_ok' can only be used with 'force'");

	if (clobber_head_ok ?
			  validate_branchname(name, &ref) :
			  validate_new_branchname(name, &ref, force)) {
		forcing = 1;
	}

	dwim_branch_start(r, start_name, track, &real_ref, &oid);
	if (dry_run)
		goto cleanup;

	if (reflog)
		log_all_ref_updates = LOG_REFS_NORMAL;

	if (forcing)
		msg = xstrfmt("branch: Reset to %s", start_name);
	else
		msg = xstrfmt("branch: Created from %s", start_name);
	transaction = ref_transaction_begin(&err);
	if (!transaction ||
		ref_transaction_update(transaction, ref.buf,
					&oid, forcing ? NULL : null_oid(),
					0, msg, &err) ||
		ref_transaction_commit(transaction, &err))
		die("%s", err.buf);
	ref_transaction_free(transaction);
	strbuf_release(&err);
	free(msg);

	if (real_ref && track)
		setup_tracking(ref.buf + 11, real_ref, track, quiet);

cleanup:
	strbuf_release(&ref);
	free(real_ref);
}

void dwim_and_setup_tracking(struct repository *r, const char *new_ref,
			     const char *orig_ref, enum branch_track track,
			     int quiet)
{
	char *real_orig_ref = NULL;
	dwim_branch_start(r, orig_ref, track, &real_orig_ref, NULL);
	setup_tracking(new_ref, real_orig_ref, track, quiet);
	free(real_orig_ref);
}

/**
 * Creates a branch in a submodule by calling
 * create_branches_recursively() in a child process. The child process
 * is necessary because install_branch_config_multiple_remotes() (which
 * is called by setup_tracking()) does not support writing configs to
 * submodules.
 */
static int submodule_create_branch(struct repository *r,
				   const struct submodule *submodule,
				   const char *name, const char *start_oid,
				   const char *tracking_name, int force,
				   int reflog, int quiet,
				   enum branch_track track, int dry_run)
{
	int ret = 0;
	struct child_process child = CHILD_PROCESS_INIT;
	struct strbuf child_err = STRBUF_INIT;
	struct strbuf out_buf = STRBUF_INIT;
	char *out_prefix = xstrfmt("submodule '%s': ", submodule->name);
	child.git_cmd = 1;
	child.err = -1;
	child.stdout_to_stderr = 1;

	prepare_other_repo_env(&child.env, r->gitdir);
	/*
	 * submodule_create_branch() is indirectly invoked by "git
	 * branch", but we cannot invoke "git branch" in the child
	 * process. "git branch" accepts a branch name and start point,
	 * where the start point is assumed to provide both the OID
	 * (start_oid) and the branch to use for tracking
	 * (tracking_name). But when recursing through submodules,
	 * start_oid and tracking name need to be specified separately
	 * (see create_branches_recursively()).
	 */
	strvec_pushl(&child.args, "submodule--helper", "create-branch", NULL);
	if (dry_run)
		strvec_push(&child.args, "--dry-run");
	if (force)
		strvec_push(&child.args, "--force");
	if (quiet)
		strvec_push(&child.args, "--quiet");
	if (reflog)
		strvec_push(&child.args, "--create-reflog");

	switch (track) {
	case BRANCH_TRACK_NEVER:
		strvec_push(&child.args, "--no-track");
		break;
	case BRANCH_TRACK_ALWAYS:
	case BRANCH_TRACK_EXPLICIT:
		strvec_push(&child.args, "--track=direct");
		break;
	case BRANCH_TRACK_OVERRIDE:
		BUG("BRANCH_TRACK_OVERRIDE cannot be used when creating a branch.");
		break;
	case BRANCH_TRACK_INHERIT:
		strvec_push(&child.args, "--track=inherit");
		break;
	case BRANCH_TRACK_UNSPECIFIED:
		/* Default for "git checkout". Do not pass --track. */
	case BRANCH_TRACK_REMOTE:
		/* Default for "git branch". Do not pass --track. */
	case BRANCH_TRACK_SIMPLE:
		/* Config-driven only. Do not pass --track. */
		break;
	}

	strvec_pushl(&child.args, name, start_oid, tracking_name, NULL);

	if ((ret = start_command(&child)))
		return ret;
	ret = finish_command(&child);
	strbuf_read(&child_err, child.err, 0);
	strbuf_add_lines(&out_buf, out_prefix, child_err.buf, child_err.len);

	if (ret)
		fprintf(stderr, "%s", out_buf.buf);
	else
		printf("%s", out_buf.buf);

	strbuf_release(&child_err);
	strbuf_release(&out_buf);
	return ret;
}

void create_branches_recursively(struct repository *r, const char *name,
				 const char *start_commitish,
				 const char *tracking_name, int force,
				 int reflog, int quiet, enum branch_track track,
				 int dry_run)
{
	int i = 0;
	char *branch_point = NULL;
	struct object_id super_oid;
	struct submodule_entry_list submodule_entry_list;

	/* Perform dwim on start_commitish to get super_oid and branch_point. */
	dwim_branch_start(r, start_commitish, BRANCH_TRACK_NEVER,
			  &branch_point, &super_oid);

	/*
	 * If we were not given an explicit name to track, then assume we are at
	 * the top level and, just like the non-recursive case, the tracking
	 * name is the branch point.
	 */
	if (!tracking_name)
		tracking_name = branch_point;

	submodules_of_tree(r, &super_oid, &submodule_entry_list);
	/*
	 * Before creating any branches, first check that the branch can
	 * be created in every submodule.
	 */
	for (i = 0; i < submodule_entry_list.entry_nr; i++) {
		if (!submodule_entry_list.entries[i].repo) {
			int code = die_message(
				_("submodule '%s': unable to find submodule"),
				submodule_entry_list.entries[i].submodule->name);
			if (advice_enabled(ADVICE_SUBMODULES_NOT_UPDATED))
				advise(_("You may try updating the submodules using 'git checkout --no-recurse-submodules %s && git submodule update --init'"),
				       start_commitish);
			exit(code);
		}

		if (submodule_create_branch(
			    submodule_entry_list.entries[i].repo,
			    submodule_entry_list.entries[i].submodule, name,
			    oid_to_hex(&submodule_entry_list.entries[i]
						.name_entry->oid),
			    tracking_name, force, reflog, quiet, track, 1))
			die(_("submodule '%s': cannot create branch '%s'"),
			    submodule_entry_list.entries[i].submodule->name,
			    name);
	}

	create_branch(r, name, start_commitish, force, 0, reflog, quiet,
		      BRANCH_TRACK_NEVER, dry_run);
	if (dry_run)
		return;
	/*
	 * NEEDSWORK If tracking was set up in the superproject but not the
	 * submodule, users might expect "git branch --recurse-submodules" to
	 * fail or give a warning, but this is not yet implemented because it is
	 * tedious to determine whether or not tracking was set up in the
	 * superproject.
	 */
	if (track)
		setup_tracking(name, tracking_name, track, quiet);

	for (i = 0; i < submodule_entry_list.entry_nr; i++) {
		if (submodule_create_branch(
			    submodule_entry_list.entries[i].repo,
			    submodule_entry_list.entries[i].submodule, name,
			    oid_to_hex(&submodule_entry_list.entries[i]
						.name_entry->oid),
			    tracking_name, force, reflog, quiet, track, 0))
			die(_("submodule '%s': cannot create branch '%s'"),
			    submodule_entry_list.entries[i].submodule->name,
			    name);
		repo_clear(submodule_entry_list.entries[i].repo);
	}
}

void remove_merge_branch_state(struct repository *r)
{
	unlink(git_path_merge_head(r));
	unlink(git_path_merge_rr(r));
	unlink(git_path_merge_msg(r));
	unlink(git_path_merge_mode(r));
	unlink(git_path_auto_merge(r));
	save_autostash(git_path_merge_autostash(r));
}

void remove_branch_state(struct repository *r, int verbose)
{
	sequencer_post_commit_cleanup(r, verbose);
	unlink(git_path_squash_msg(r));
	remove_merge_branch_state(r);
}

void die_if_checked_out(const char *branch, int ignore_current_worktree)
{
	struct worktree **worktrees = get_worktrees();

	for (int i = 0; worktrees[i]; i++) {
		if (worktrees[i]->is_current && ignore_current_worktree)
			continue;

		if (is_shared_symref(worktrees[i], "HEAD", branch)) {
			skip_prefix(branch, "refs/heads/", &branch);
			die(_("'%s' is already checked out at '%s'"),
				branch, worktrees[i]->path);
		}
	}

	free_worktrees(worktrees);
}
