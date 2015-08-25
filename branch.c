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

		head = resolve_ref_unsafe("HEAD", 0, sha1, NULL);
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

	if (forcing)
		snprintf(msg, sizeof msg, "branch: Reset to %s",
			 start_name);
	else if (!dont_change_ref)
		snprintf(msg, sizeof msg, "branch: Created from %s",
			 start_name);

	if (reflog)
		log_all_ref_updates = 1;

	if (!dont_change_ref) {
		struct ref_transaction *transaction;
		struct strbuf err = STRBUF_INIT;

		transaction = ref_transaction_begin(&err);
		if (!transaction ||
		    ref_transaction_update(transaction, ref.buf,
					   sha1, forcing ? NULL : null_sha1,
					   0, msg, &err) ||
		    ref_transaction_commit(transaction, &err))
			die("%s", err.buf);
		ref_transaction_free(transaction);
		strbuf_release(&err);
	}

	if (real_ref && track)
		setup_tracking(ref.buf + 11, real_ref, track, quiet);

	strbuf_release(&ref);
	free(real_ref);
}

void remove_branch_state(void)
{
	unlink(git_path_cherry_pick_head());
	unlink(git_path_revert_head());
	unlink(git_path_merge_head());
	unlink(git_path_merge_rr());
	unlink(git_path_merge_msg());
	unlink(git_path_merge_mode());
	unlink(git_path_squash_msg());
}

static char *find_linked_symref(const char *symref, const char *branch,
				const char *id)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	struct strbuf gitdir = STRBUF_INIT;
	char *existing = NULL;

	/*
	 * $GIT_COMMON_DIR/$symref (e.g. HEAD) is practically outside
	 * $GIT_DIR so resolve_ref_unsafe() won't work (it uses
	 * git_path). Parse the ref ourselves.
	 */
	if (id)
		strbuf_addf(&path, "%s/worktrees/%s/%s", get_git_common_dir(), id, symref);
	else
		strbuf_addf(&path, "%s/%s", get_git_common_dir(), symref);

	if (!strbuf_readlink(&sb, path.buf, 0)) {
		if (!starts_with(sb.buf, "refs/") ||
		    check_refname_format(sb.buf, 0))
			goto done;
	} else if (strbuf_read_file(&sb, path.buf, 0) >= 0 &&
	    starts_with(sb.buf, "ref:")) {
		strbuf_remove(&sb, 0, strlen("ref:"));
		strbuf_trim(&sb);
	} else
		goto done;
	if (strcmp(sb.buf, branch))
		goto done;
	if (id) {
		strbuf_reset(&path);
		strbuf_addf(&path, "%s/worktrees/%s/gitdir", get_git_common_dir(), id);
		if (strbuf_read_file(&gitdir, path.buf, 0) <= 0)
			goto done;
		strbuf_rtrim(&gitdir);
	} else
		strbuf_addstr(&gitdir, get_git_common_dir());
	strbuf_strip_suffix(&gitdir, ".git");

	existing = strbuf_detach(&gitdir, NULL);
done:
	strbuf_release(&path);
	strbuf_release(&sb);
	strbuf_release(&gitdir);

	return existing;
}

char *find_shared_symref(const char *symref, const char *target)
{
	struct strbuf path = STRBUF_INIT;
	DIR *dir;
	struct dirent *d;
	char *existing;

	if ((existing = find_linked_symref(symref, target, NULL)))
		return existing;

	strbuf_addf(&path, "%s/worktrees", get_git_common_dir());
	dir = opendir(path.buf);
	strbuf_release(&path);
	if (!dir)
		return NULL;

	while ((d = readdir(dir)) != NULL) {
		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;
		existing = find_linked_symref(symref, target, d->d_name);
		if (existing)
			goto done;
	}
done:
	closedir(dir);

	return existing;
}

void die_if_checked_out(const char *branch)
{
	char *existing;

	existing = find_shared_symref("HEAD", branch);
	if (existing) {
		skip_prefix(branch, "refs/heads/", &branch);
		die(_("'%s' is already checked out at '%s'"), branch, existing);
	}
}
