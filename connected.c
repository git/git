#include "cache.h"
#include "run-command.h"
#include "sigchain.h"
#include "connected.h"

/*
 * If we feed all the commits we want to verify to this command
 *
 *  $ git rev-list --objects --stdin --not --all
 *
 * and if it does not error out, that means everything reachable from
 * these commits locally exists and is connected to our existing refs.
 * Note that this does _not_ validate the individual objects.
 *
 * Returns 0 if everything is connected, non-zero otherwise.
 */
int check_everything_connected(sha1_iterate_fn fn, int quiet, void *cb_data)
{
	struct child_process rev_list;
	const char *argv[] = {"rev-list", "--objects",
			      "--stdin", "--not", "--all", NULL, NULL};
	char commit[41];
	unsigned char sha1[20];
	int err = 0;

	if (fn(cb_data, sha1))
		return err;

	if (quiet)
		argv[5] = "--quiet";

	memset(&rev_list, 0, sizeof(rev_list));
	rev_list.argv = argv;
	rev_list.git_cmd = 1;
	rev_list.in = -1;
	rev_list.no_stdout = 1;
	rev_list.no_stderr = quiet;
	if (start_command(&rev_list))
		return error(_("Could not run 'git rev-list'"));

	sigchain_push(SIGPIPE, SIG_IGN);

	commit[40] = '\n';
	do {
		memcpy(commit, sha1_to_hex(sha1), 40);
		if (write_in_full(rev_list.in, commit, 41) < 0) {
			if (errno != EPIPE && errno != EINVAL)
				error(_("failed write to rev-list: %s"),
				      strerror(errno));
			err = -1;
			break;
		}
	} while (!fn(cb_data, sha1));

	if (close(rev_list.in)) {
		error(_("failed to close rev-list's stdin: %s"), strerror(errno));
		err = -1;
	}

	sigchain_pop(SIGPIPE);
	return finish_command(&rev_list) || err;
}
