#include "git-compat-util.h"
#include "cache.h"
#include "branch.h"
#include "refs.h"
#include "remote.h"
#include "commit.h"

struct tracking {
	struct refspec spec;
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
			if (tracking->src) {
				free(tracking->src);
				tracking->src = NULL;
			}
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

void install_branch_config(int flag, const char *local, const char *origin, const char *remote)
{
	const char *shortname = NULL;
	struct strbuf key = STRBUF_INIT;
	int rebasing = should_setup_rebase(origin);

	if (skip_prefix(remote, "refs/heads/", &shortname)
	    && !strcmp(local, shortname)
	    && !origin) {
		warning(_("Not setting branch %s as its own upstream."),
			local);
		return;
	}

	strbuf_addf(&key, "branch.%s.remote", local);
	git_config_set(key.buf, origin ? origin : ".");

	strbuf_reset(&key);
	strbuf_addf(&key, "branch.%s.merge", local);
	git_config_set(key.buf, remote);

	if (rebasing) {
		strbuf_reset(&key);
		strbuf_addf(&key, "branch.%s.rebase", local);
		git_config_set(key.buf, "true");
	}
	strbuf_release(&key);

	if (flag & BRANCH_CONFIG_VERBOSE) {
		if (shortname) {
			if (origin)
				printf_ln(rebasing ?
					  _("Branch %s set up to track remote branch %s from %s by rebasing.") :
					  _("Branch %s set up to track remote branch %s from %s."),
					  local, shortname, origin);
			else
				printf_ln(rebasing ?
					  _("Branch %s set up to track local branch %s by rebasing.") :
					  _("Branch %s set up to track local branch %s."),
					  local, shortname);
		} else {
			if (origin)
				printf_ln(rebasing ?
					  _("Branch %s set up to track remote ref %s by rebasing.") :
					  _("Branch %s set up to track remote ref %s."),
					  local, remote);
			else
				printf_ln(rebasing ?
					  _("Branch %s set up to track local ref %s by rebasing.") :
					  _("Branch %s set up to track local ref %s."),
					  local, remote);
		}
	}
}

/*
 * This is called when new_ref is branched off of orig_ref, and tries
 * to infer the settings for branch.<new_ref>.{remote,merge} from the
 * config.
 */
static int setup_tracking(const char *new_ref, const char *orig_ref,
			  enum branch_track track, int quiet)
{
	struct tracking tracking;
	int config_flags = quiet ? 0 : BRANCH_CONFIG_VERBOSE;

	memset(&tracking, 0, sizeof(tracking));
	tracking.spec.dst = (char *)orig_ref;
	if (for_each_remote(find_tracked_branch, &tracking))
		return 1;

	if (!tracking.matches)
		switch (track) {
		case BRANCH_TRACK_ALWAYS:
		case BRANCH_TRACK_EXPLICIT:
		case BRANCH_TRACK_OVERRIDE:
			break;
		default:
			return 1;
		}

	if (tracking.matches > 1)
		return error(_("Not tracking: ambiguous information for ref %s"),
				orig_ref);

	install_branch_config(config_flags, new_ref, tracking.remote,
			      tracking.src ? tracking.src : orig_ref);

	free(tracking.src);
	return 0;
}

struct branch_desc_cb {
	const char *config_name;
	const char *value;
};

static int read_branch_desc_cb(const char *var, const char *value, void *cb)
{
	struct branch_desc_cb *desc = cb;
	if (strcmp(desc->config_name, var))
		return 0;
	free((char *)desc->value);
	return git_config_string(&desc->value, var, value);
}

int read_branch_desc(struct strbuf *buf, const char *branch_name)
{
	struct branch_desc_cb cb;
	struct strbuf name = STRBUF_INIT;
	strbuf_addf(&name, "branch.%s.description", branch_name);
	cb.config_name = name.buf;
	cb.value = NULL;
	if (git_config(read_branch_desc_cb, &cb) < 0) {
		strbuf_release(&name);
		return -1;
	}
	if (cb.value)
		strbuf_addstr(buf, cb.value);
	strbuf_release(&name);
	return 0;
}

int validate_new_branchname(const char *name, struct strbuf *ref,
			    int force, int attr_only)
{
	if (strbuf_check_branch_ref(ref, name))
		die(_("'%s' is not a valid branch name."), name);

	if (!ref_exists(ref->buf))
		return 0;
	else if (!force && !attr_only)
		die(_("A branch named '%s' already exists."), ref->buf + strlen("refs/heads/"));

	if (!attr_only) {
		const char *head;
		unsigned char sha1[20];

		head = resolve_ref_unsafe("HEAD", sha1, 0, NULL);
		if (!is_bare_repository() && head && !strcmp(head, ref->buf))
			die(_("Cannot force update the current branch."));
	}
	return 1;
}

static int check_tracking_branch(struct remote *remote, void *cb_data)
{
	char *tracking_branch = cb_data;
	struct refspec query;
	memset(&query, 0, sizeof(struct refspec));
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

void create_branch(const char *head,
		   const char *name, const char *start_name,
		   int force, int reflog, int clobber_head,
		   int quiet, enum branch_track track)
{
	struct ref_lock *lock = NULL;
	struct commit *commit;
	unsigned char sha1[20];
	char *real_ref, msg[PATH_MAX + 20];
	struct strbuf ref = STRBUF_INIT;
	int forcing = 0;
	int dont_change_ref = 0;
	int explicit_tracking = 0;

	if (track == BRANCH_TRACK_EXPLICIT || track == BRANCH_TRACK_OVERRIDE)
		explicit_tracking = 1;

	if (validate_new_branchname(name, &ref, force,
				    track == BRANCH_TRACK_OVERRIDE ||
				    clobber_head)) {
		if (!force)
			dont_change_ref = 1;
		else
			forcing = 1;
	}

	real_ref = NULL;
	if (get_sha1(start_name, sha1)) {
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

	switch (dwim_ref(start_name, strlen(start_name), sha1, &real_ref)) {
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

	if ((commit = lookup_commit_reference(sha1)) == NULL)
		die(_("Not a valid branch point: '%s'."), start_name);
	hashcpy(sha1, commit->object.sha1);

	if (!dont_change_ref) {
		lock = lock_any_ref_for_update(ref.buf, NULL, 0, NULL);
		if (!lock)
			die_errno(_("Failed to lock ref for update"));
	}

	if (reflog)
		log_all_ref_updates = 1;

	if (forcing)
		snprintf(msg, sizeof msg, "branch: Reset to %s",
			 start_name);
	else if (!dont_change_ref)
		snprintf(msg, sizeof msg, "branch: Created from %s",
			 start_name);

	if (real_ref && track)
		setup_tracking(ref.buf + 11, real_ref, track, quiet);

	if (!dont_change_ref)
		if (write_ref_sha1(lock, sha1, msg) < 0)
			die_errno(_("Failed to write ref"));

	strbuf_release(&ref);
	free(real_ref);
}

void remove_branch_state(void)
{
	unlink(git_path("CHERRY_PICK_HEAD"));
	unlink(git_path("REVERT_HEAD"));
	unlink(git_path("MERGE_HEAD"));
	unlink(git_path("MERGE_RR"));
	unlink(git_path("MERGE_MSG"));
	unlink(git_path("MERGE_MODE"));
	unlink(git_path("SQUASH_MSG"));
}
