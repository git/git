/*
 * "git push"
 */
#include "cache.h"
#include "refs.h"
#include "run-command.h"
#include "builtin.h"
#include "remote.h"
#include "transport.h"

static const char push_usage[] = "git-push [--all] [--dry-run] [--tags] [--receive-pack=<git-receive-pack>] [--repo=all] [-f | --force] [-v] [<repository> <refspec>...]";

static int thin, verbose;
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

static int do_push(const char *repo, int flags)
{
	int i, errs;
	struct remote *remote = remote_get(repo);

	if (!remote)
		die("bad repository '%s'", repo);

	if (!refspec
		&& !(flags & TRANSPORT_PUSH_ALL)
		&& remote->push_refspec_nr) {
		refspec = remote->push_refspec;
		refspec_nr = remote->push_refspec_nr;
	}
	errs = 0;
	for (i = 0; i < remote->url_nr; i++) {
		struct transport *transport =
			transport_get(remote, remote->url[i]);
		int err;
		if (receivepack)
			transport_set_option(transport,
					     TRANS_OPT_RECEIVEPACK, receivepack);
		if (thin)
			transport_set_option(transport, TRANS_OPT_THIN, "yes");

		if (verbose)
			fprintf(stderr, "Pushing to %s\n", remote->url[i]);
		err = transport_push(transport, refspec_nr, refspec, flags);
		err |= transport_disconnect(transport);

		if (!err)
			continue;

		error("failed to push to '%s'", remote->url[i]);
		errs++;
	}
	return !!errs;
}

int cmd_push(int argc, const char **argv, const char *prefix)
{
	int i;
	int flags = 0;
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
			flags |= TRANSPORT_PUSH_ALL;
			continue;
		}
		if (!strcmp(arg, "--dry-run")) {
			flags |= TRANSPORT_PUSH_DRY_RUN;
			continue;
		}
		if (!strcmp(arg, "--tags")) {
			add_refspec("refs/tags/*");
			continue;
		}
		if (!strcmp(arg, "--force") || !strcmp(arg, "-f")) {
			flags |= TRANSPORT_PUSH_FORCE;
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
			receivepack = arg + 15;
			continue;
		}
		if (!prefixcmp(arg, "--exec=")) {
			receivepack = arg + 7;
			continue;
		}
		usage(push_usage);
	}
	set_refspecs(argv + i, argc - i);
	if ((flags & TRANSPORT_PUSH_ALL) && refspec)
		usage(push_usage);

	return do_push(repo, flags);
}
