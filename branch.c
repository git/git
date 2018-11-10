#include "git-compat-util.h"
#include "cache.h"
#include "config.h"
#include "branch.h"
#include "refs.h"
#include "refspec.h"
#include "remote.h"
#include "commit.h"
#include "worktree.h"

struct tracking {
	struct refspec_item spec;
	char *src;
	const char *remote;
	int matches;
};

static int find_tracked_branch(struct remote *remote, void *priv)
{
	struct tracking *tracking = priv;

	if (!remote_find_tracking(remote, &tracking->spec)) {
		if (++tracking->matches == 1) {
			tracking->src = tracking->spec.src;
			tracking->remote = remote->name;
		} else {
			free(tracking->spec.src);
			FREE_AND_NULL(tracking->src);
		}
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

static const char tracking_advice[] =
N_("\n"
"After fixing the error cause you may try to fix up\n"
"the remote tracking information by invoking\n"
"\"git branch --set-upstream-to=%s%s%s\".");

int install_branch_config(int flag, const char *local, const char *origin, const char *remote)
{
	const char *shortname = NULL;
	struct strbuf key = STRBUF_INIT;
	int rebasing = should_setup_rebase(origin);

	if (skip_prefix(remote, "refs/heads/", &shortname)
	    && !strcmp(local, shortname)
	    && !origin) {
		warning(_("Not setting branch %s as its own upstream."),
			local);
		return 0;
	}

	strbuf_addf(&key, "branch.%s.remote", local);
	if (git_config_set_gently(key.buf, origin ? origin : ".") < 0)
		goto out_err;

	strbuf_reset(&key);
	strbuf_addf(&key, "branch.%s.merge", local);
	if (git_config_set_gently(key.buf, remote) < 0)
		goto out_err;

	if (rebasing) {
		strbuf_reset(&key);
		strbuf_addf(&key, "branch.%s.rebase", local);
		if (git_config_set_gently(key.buf, "true") < 0)
			goto out_err;
	}
	strbuf_release(&key);

	if (flag & BRANCH_CONFIG_VERBOSE) {
		if (shortname) {
			if (origin)
				printf_ln(rebasing ?
					  _("Branch '%s' set up to track remote branch '%s' from '%s' by rebasing.") :
					  _("Branch '%s' set up to track remote branch '%s' from '%s'."),
					  local, shortname, origin);
			else
				printf_ln(rebasing ?
					  _("Branch '%s' set up to track local branch '%s' by rebasing.") :
					  _("Branch '%s' set up to track local branch '%s'."),
					  local, shortname);
		} else {
			if (origin)
				printf_ln(rebasing ?
					  _("Branch '%s' set up to track remote ref '%s' by rebasing.") :
					  _("Branch '%s' set up to track remote ref '%s'."),
					  local, remote);
			else
				printf_ln(rebasing ?
					  _("Branch '%s' set up to track local ref '%s' by rebasing.") :
					  _("Branch '%s' set up to track local ref '%s'."),
					  local, remote);
		}
	}

	return 0;

out_err:
	strbuf_release(&key);
	error(_("Unable to write upstream branch configuration"));

	advise(_(tracking_advice),
	       origin ? origin : "",
	       origin ? "/" : "",
	       shortname ? shortname : remote);

	return -1;
}

/*
 * This is called when new_ref is branched off of orig_ref, and tries
 * to infer the settings for branch.<new_ref>.{remote,merge} from the
 * config.
 */
static void setup_tracking(const char *new_ref, const char *orig_ref,
			   enum branch_track track, int quiet)
{
	struct tracking tracking;
	int config_flags = quiet ? 0 : BRANCH_CONFIG_VERBOSE;

	memset(&tracking, 0, sizeof(tracking));
	tracking.spec.dst = (char *)orig_ref;
	if (for_each_remote(find_tracked_branch, &tracking))
		return;

	if (!tracking.matches)
		switch (track) {
		case BRANCH_TRACK_ALWAYS:
		case BRANCH_TRACK_EXPLICIT:
		case BRANCH_TRACK_OVERRIDE:
			break;
		default:
			return;
		}

	if (tracking.matches > 1)
		die(_("Not tracking: ambiguous information for ref %s"),
		    orig_ref);

	if (install_branch_config(config_flags, new_ref, tracking.remote,
			      tracking.src ? tracking.src : orig_ref) < 0)
		exit(-1);

	free(tracking.src);
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
		die(_("'%s' is not a valid branch name."), name);

	return ref_exists(ref->buf);
}

/*
 * Check if a branch 'name' can be created as a new branch; die otherwise.
 * 'force' can be used when it is OK for the named branch already exists.
 * Return 1 if the named branch already exists; return 0 otherwise.
 * Fill ref with the full refname for the branch.
 */
int validate_new_branchname(const char *name, struct strbuf *ref, int force)
{
	const char *head;

	if (!validate_branchname(name, ref))
		return 0;

	if (!force)
		die(_("A branch named '%s' already exists."),
		    ref->buf + strlen("refs/heads/"));

	head = resolve_ref_unsafe("HEAD", 0, NULL, NULL);
	if (!is_bare_repository() && head && !strcmp(head, ref->buf))
		die(_("Cannot force update the current branch."));

	return 1;
}

static int check_tracking_branch(struct remote *remote, void *cb_data)
{
	char *tracking_branch = cb_data;
	struct refspec_item query;
	memset(&query, 0, sizeof(struct refspec_item));
	query.dst = tracking_branch;
	return !remote_find_tracking(remote, &query);
}

static int validate_remote_tracking_branch(char *ref)
{
	return !for_each_remote(check_tracking_branch, ref);
}

static const char upstream_not_branch[] =
N_("Cannot setup tracking information; starting point '%s' is not a branch.");
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

void create_branch(struct repository *r,
		   const char *name, const char *start_name,
		   int force, int clobber_head_ok, int reflog,
		   int quiet, enum branch_track track)
{
	struct commit *commit;
	struct object_id oid;
	char *real_ref;
	struct strbuf ref = STRBUF_INIT;
	int forcing = 0;
	int dont_change_ref = 0;
	int explicit_tracking = 0;

	if (track == BRANCH_TRACK_EXPLICIT || track == BRANCH_TRACK_OVERRIDE)
		explicit_tracking = 1;

	if ((track == BRANCH_TRACK_OVERRIDE || clobber_head_ok)
	    ? validate_branchname(name, &ref)
	    : validate_new_branchname(name, &ref, force)) {
		if (!force)
			dont_change_ref = 1;
		else
			forcing = 1;
	}

	real_ref = NULL;
	if (get_oid(start_name, &oid)) {
		if (explicit_tracking) {
			if (advice_set_upstream_failure) {
				error(_(upstream_missing), start_name);
				advise(_(upstream_advice));
				exit(1);
			}
			die(_(upstream_missing), start_name);
		}
		die(_("Not a valid object name: '%s'."), start_name);
	}

	switch (dwim_ref(start_name, strlen(start_name), &oid, &real_ref)) {
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
				real_ref = NULL;
		}
		break;
	default:
		die(_("Ambiguous object name: '%s'."), start_name);
		break;
	}

	if ((commit = lookup_commit_reference(r, &oid)) == NULL)
		die(_("Not a valid branch point: '%s'."), start_name);
	oidcpy(&oid, &commit->object.oid);

	if (reflog)
		log_all_ref_updates = LOG_REFS_NORMAL;

	if (!dont_change_ref) {
		struct ref_transaction *transaction;
		struct strbuf err = STRBUF_INIT;
		char *msg;

		if (forcing)
			msg = xstrfmt("branch: Reset to %s", start_name);
		else
			msg = xstrfmt("branch: Created from %s", start_name);

		transaction = ref_transaction_begin(&err);
		if (!transaction ||
		    ref_transaction_update(transaction, ref.buf,
					   &oid, forcing ? NULL : &null_oid,
					   0, msg, &err) ||
		    ref_transaction_commit(transaction, &err))
			die("%s", err.buf);
		ref_transaction_free(transaction);
		strbuf_release(&err);
		free(msg);
	}

	if (real_ref && track)
		setup_tracking(ref.buf + 11, real_ref, track, quiet);

	strbuf_release(&ref);
	free(real_ref);
}

void remove_branch_state(struct repository *r)
{
	unlink(git_path_cherry_pick_head(r));
	unlink(git_path_revert_head(r));
	unlink(git_path_merge_head(r));
	unlink(git_path_merge_rr(r));
	unlink(git_path_merge_msg(r));
	unlink(git_path_merge_mode(r));
	unlink(git_path_squash_msg(r));
}

void die_if_checked_out(const char *branch, int ignore_current_worktree)
{
	const struct worktree *wt;

	wt = find_shared_symref("HEAD", branch);
	if (!wt || (ignore_current_worktree && wt->is_current))
		return;
	skip_prefix(branch, "refs/heads/", &branch);
	die(_("'%s' is already checked out at '%s'"),
	    branch, wt->path);
}

int replace_each_worktree_head_symref(const char *oldref, const char *newref,
				      const char *logmsg)
{
	int ret = 0;
	struct worktree **worktrees = get_worktrees(0);
	int i;

	for (i = 0; worktrees[i]; i++) {
		struct ref_store *refs;

		if (worktrees[i]->is_detached)
			continue;
		if (!worktrees[i]->head_ref)
			continue;
		if (strcmp(oldref, worktrees[i]->head_ref))
			continue;

		refs = get_worktree_ref_store(worktrees[i]);
		if (refs_create_symref(refs, "HEAD", newref, logmsg))
			ret = error(_("HEAD of working tree %s is not updated"),
				    worktrees[i]->path);
	}

	free_worktrees(worktrees);
	return ret;
}
