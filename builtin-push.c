/*
 * "git push"
 */
#include "cache.h"
#include "refs.h"
#include "run-command.h"
#include "builtin.h"
#include "remote.h"

static const char push_usage[] = "git-push [--all] [--tags] [--receive-pack=<git-receive-pack>] [--repo=all] [-f | --force] [-v] [<repository> <refspec>...]";

static int all, force, thin = 1, verbose;
static const char *receivepack;

static const char **refspec;
static int refspec_nr;

static void add_refspec(const char *ref)
{
	int nr = refspec_nr + 1;
	refspec = xrealloc(refspec, nr * sizeof(char *));
	refspec[nr-1] = ref;
	refspec_nr = nr;
}

static void set_refspecs(const char **refs, int nr)
{
	int i;
	for (i = 0; i < nr; i++) {
		const char *ref = refs[i];
		if (!strcmp("tag", ref)) {
			char *tag;
			int len;
			if (nr <= ++i)
				die("tag shorthand without <tag>");
			len = strlen(refs[i]) + 11;
			tag = xmalloc(len);
			strcpy(tag, "refs/tags/");
			strcat(tag, refs[i]);
			ref = tag;
		}
		add_refspec(ref);
	}
}

static int do_push(const char *repo)
{
	int i, errs;
	int common_argc;
	const char **argv;
	int argc;
	struct remote *remote = remote_get(repo);

	if (!remote)
		die("bad repository '%s'", repo);

	if (remote->receivepack) {
		char *rp = xmalloc(strlen(remote->receivepack) + 16);
		sprintf(rp, "--receive-pack=%s", remote->receivepack);
		receivepack = rp;
	}
	if (!refspec && !all && remote->push_refspec_nr) {
		refspec = remote->push_refspec;
		refspec_nr = remote->push_refspec_nr;
	}

	argv = xmalloc((refspec_nr + 10) * sizeof(char *));
	argv[0] = "dummy-send-pack";
	argc = 1;
	if (all)
		argv[argc++] = "--all";
	if (force)
		argv[argc++] = "--force";
	if (receivepack)
		argv[argc++] = receivepack;
	common_argc = argc;

	errs = 0;
	for (i = 0; i < remote->uri_nr; i++) {
		int err;
		int dest_argc = common_argc;
		int dest_refspec_nr = refspec_nr;
		const char **dest_refspec = refspec;
		const char *dest = remote->uri[i];
		const char *sender = "send-pack";
		if (!prefixcmp(dest, "http://") ||
		    !prefixcmp(dest, "https://"))
			sender = "http-push";
		else {
			char *rem = xmalloc(strlen(remote->name) + 10);
			sprintf(rem, "--remote=%s", remote->name);
			argv[dest_argc++] = rem;
			if (thin)
				argv[dest_argc++] = "--thin";
		}
		argv[0] = sender;
		argv[dest_argc++] = dest;
		while (dest_refspec_nr--)
			argv[dest_argc++] = *dest_refspec++;
		argv[dest_argc] = NULL;
		if (verbose)
			fprintf(stderr, "Pushing to %s\n", dest);
		err = run_command_v_opt(argv, RUN_GIT_CMD);
		if (!err)
			continue;

		error("failed to push to '%s'", remote->uri[i]);
		switch (err) {
		case -ERR_RUN_COMMAND_FORK:
			error("unable to fork for %s", sender);
		case -ERR_RUN_COMMAND_EXEC:
			error("unable to exec %s", sender);
			break;
		case -ERR_RUN_COMMAND_WAITPID:
		case -ERR_RUN_COMMAND_WAITPID_WRONG_PID:
		case -ERR_RUN_COMMAND_WAITPID_SIGNAL:
		case -ERR_RUN_COMMAND_WAITPID_NOEXIT:
			error("%s died with strange error", sender);
		}
		errs++;
	}
	return !!errs;
}

int cmd_push(int argc, const char **argv, const char *prefix)
{
	int i;
	const char *repo = NULL;	/* default repository */

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-') {
			repo = arg;
			i++;
			break;
		}
		if (!strcmp(arg, "-v")) {
			verbose=1;
			continue;
		}
		if (!prefixcmp(arg, "--repo=")) {
			repo = arg+7;
			continue;
		}
		if (!strcmp(arg, "--all")) {
			all = 1;
			continue;
		}
		if (!strcmp(arg, "--tags")) {
			add_refspec("refs/tags/*");
			continue;
		}
		if (!strcmp(arg, "--force") || !strcmp(arg, "-f")) {
			force = 1;
			continue;
		}
		if (!strcmp(arg, "--thin")) {
			thin = 1;
			continue;
		}
		if (!strcmp(arg, "--no-thin")) {
			thin = 0;
			continue;
		}
		if (!prefixcmp(arg, "--receive-pack=")) {
			receivepack = arg;
			continue;
		}
		if (!prefixcmp(arg, "--exec=")) {
			receivepack = arg;
			continue;
		}
		usage(push_usage);
	}
	set_refspecs(argv + i, argc - i);
	if (all && refspec)
		usage(push_usage);

	return do_push(repo);
}
