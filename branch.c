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

/*
 * This is called when new_ref is branched off of orig_ref, and tries
 * to infer the settings for branch.<new_ref>.{remote,merge} from the
 * config.
 */
static int setup_tracking(const char *new_ref, const char *orig_ref)
{
	char key[1024];
	struct tracking tracking;

	if (strlen(new_ref) > 1024 - 7 - 7 - 1)
		return error("Tracking not set up: name too long: %s",
				new_ref);

	memset(&tracking, 0, sizeof(tracking));
	tracking.spec.dst = (char *)orig_ref;
	if (for_each_remote(find_tracked_branch, &tracking) ||
			!tracking.matches)
		return 1;

	if (tracking.matches > 1)
		return error("Not tracking: ambiguous information for ref %s",
				orig_ref);

	if (tracking.matches == 1) {
		sprintf(key, "branch.%s.remote", new_ref);
		git_config_set(key, tracking.remote ?  tracking.remote : ".");
		sprintf(key, "branch.%s.merge", new_ref);
		git_config_set(key, tracking.src);
		free(tracking.src);
		printf("Branch %s set up to track remote branch %s.\n",
			       new_ref, orig_ref);
	}

	return 0;
}

void create_branch(const char *head,
		   const char *name, const char *start_name,
		   int force, int reflog, int track)
{
	struct ref_lock *lock;
	struct commit *commit;
	unsigned char sha1[20];
	char *real_ref, ref[PATH_MAX], msg[PATH_MAX + 20];
	int forcing = 0;

	snprintf(ref, sizeof ref, "refs/heads/%s", name);
	if (check_ref_format(ref))
		die("'%s' is not a valid branch name.", name);

	if (resolve_ref(ref, sha1, 1, NULL)) {
		if (!force)
			die("A branch named '%s' already exists.", name);
		else if (!is_bare_repository() && !strcmp(head, name))
			die("Cannot force update the current branch.");
		forcing = 1;
	}

	real_ref = NULL;
	if (get_sha1(start_name, sha1))
		die("Not a valid object name: '%s'.", start_name);

	switch (dwim_ref(start_name, strlen(start_name), sha1, &real_ref)) {
	case 0:
		/* Not branching from any existing branch */
		real_ref = NULL;
		break;
	case 1:
		/* Unique completion -- good */
		break;
	default:
		die("Ambiguous object name: '%s'.", start_name);
		break;
	}

	if ((commit = lookup_commit_reference(sha1)) == NULL)
		die("Not a valid branch point: '%s'.", start_name);
	hashcpy(sha1, commit->object.sha1);

	lock = lock_any_ref_for_update(ref, NULL, 0);
	if (!lock)
		die("Failed to lock ref for update: %s.", strerror(errno));

	if (reflog)
		log_all_ref_updates = 1;

	if (forcing)
		snprintf(msg, sizeof msg, "branch: Reset from %s",
			 start_name);
	else
		snprintf(msg, sizeof msg, "branch: Created from %s",
			 start_name);

	/* When branching off a remote branch, set up so that git-pull
	   automatically merges from there.  So far, this is only done for
	   remotes registered via .git/config.  */
	if (real_ref && track)
		setup_tracking(name, real_ref);

	if (write_ref_sha1(lock, sha1, msg) < 0)
		die("Failed to write ref: %s.", strerror(errno));

	if (real_ref)
		free(real_ref);
}
