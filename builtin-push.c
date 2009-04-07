/*
 * "git push"
 */
#include "cache.h"
#include "refs.h"
#include "run-command.h"
#include "builtin.h"
#include "remote.h"
#include "transport.h"
#include "parse-options.h"

static const char * const push_usage[] = {
	"git push [--all | --mirror] [--dry-run] [--tags] [--receive-pack=<git-receive-pack>] [--repo=<repository>] [-f | --force] [-v] [<repository> <refspec>...]",
	NULL,
};

static int thin;
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

static void setup_push_tracking(void)
{
	struct strbuf refspec = STRBUF_INIT;
	struct branch *branch = branch_get(NULL);
	if (!branch)
		die("You are not currently on a branch.");
	if (!branch->merge_nr)
		die("The current branch %s is not tracking anything.",
		    branch->name);
	if (branch->merge_nr != 1)
		die("The current branch %s is tracking multiple branches, "
		    "refusing to push.", branch->name);
	strbuf_addf(&refspec, "%s:%s", branch->name, branch->merge[0]->src);
	add_refspec(refspec.buf);
}

static const char *warn_unconfigured_push_msg[] = {
	"You did not specify any refspecs to push, and the current remote",
	"has not configured any push refspecs. The default action in this",
	"case is to push all matching refspecs, that is, all branches",
	"that exist both locally and remotely will be updated.  This may",
	"not necessarily be what you want to happen.",
	"",
	"You can specify what action you want to take in this case, and",
	"avoid seeing this message again, by configuring 'push.default' to:",
	"  'nothing'  : Do not push anything",
	"  'matching' : Push all matching branches (default)",
	"  'tracking' : Push the current branch to whatever it is tracking",
	"  'current'  : Push the current branch"
};

static void warn_unconfigured_push(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(warn_unconfigured_push_msg); i++)
		warning("%s", warn_unconfigured_push_msg[i]);
}

static void setup_default_push_refspecs(void)
{
	git_config(git_default_config, NULL);
	switch (push_default) {
	case PUSH_DEFAULT_UNSPECIFIED:
		warn_unconfigured_push();
		/* fallthrough */

	case PUSH_DEFAULT_MATCHING:
		add_refspec(":");
		break;

	case PUSH_DEFAULT_TRACKING:
		setup_push_tracking();
		break;

	case PUSH_DEFAULT_CURRENT:
		add_refspec("HEAD");
		break;

	case PUSH_DEFAULT_NOTHING:
		die("You didn't specify any refspecs to push, and "
		    "push.default is \"nothing\".");
		break;
	}
}

static int do_push(const char *repo, int flags)
{
	int i, errs;
	struct remote *remote = remote_get(repo);

	if (!remote) {
		if (repo)
			die("bad repository '%s'", repo);
		die("No destination configured to push to.");
	}

	if (remote->mirror)
		flags |= (TRANSPORT_PUSH_MIRROR|TRANSPORT_PUSH_FORCE);

	if ((flags & TRANSPORT_PUSH_ALL) && refspec) {
		if (!strcmp(*refspec, "refs/tags/*"))
			return error("--all and --tags are incompatible");
		return error("--all can't be combined with refspecs");
	}

	if ((flags & TRANSPORT_PUSH_MIRROR) && refspec) {
		if (!strcmp(*refspec, "refs/tags/*"))
			return error("--mirror and --tags are incompatible");
		return error("--mirror can't be combined with refspecs");
	}

	if ((flags & (TRANSPORT_PUSH_ALL|TRANSPORT_PUSH_MIRROR)) ==
				(TRANSPORT_PUSH_ALL|TRANSPORT_PUSH_MIRROR)) {
		return error("--all and --mirror are incompatible");
	}

	if (!refspec && !(flags & TRANSPORT_PUSH_ALL)) {
		if (remote->push_refspec_nr) {
			refspec = remote->push_refspec;
			refspec_nr = remote->push_refspec_nr;
		} else if (!(flags & TRANSPORT_PUSH_MIRROR))
			setup_default_push_refspecs();
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

		if (flags & TRANSPORT_PUSH_VERBOSE)
			fprintf(stderr, "Pushing to %s\n", remote->url[i]);
		err = transport_push(transport, refspec_nr, refspec, flags);
		err |= transport_disconnect(transport);

		if (!err)
			continue;

		error("failed to push some refs to '%s'", remote->url[i]);
		errs++;
	}
	return !!errs;
}

int cmd_push(int argc, const char **argv, const char *prefix)
{
	int flags = 0;
	int tags = 0;
	int rc;
	const char *repo = NULL;	/* default repository */

	struct option options[] = {
		OPT_BIT('v', "verbose", &flags, "be verbose", TRANSPORT_PUSH_VERBOSE),
		OPT_STRING( 0 , "repo", &repo, "repository", "repository"),
		OPT_BIT( 0 , "all", &flags, "push all refs", TRANSPORT_PUSH_ALL),
		OPT_BIT( 0 , "mirror", &flags, "mirror all refs",
			    (TRANSPORT_PUSH_MIRROR|TRANSPORT_PUSH_FORCE)),
		OPT_BOOLEAN( 0 , "tags", &tags, "push tags"),
		OPT_BIT( 0 , "dry-run", &flags, "dry run", TRANSPORT_PUSH_DRY_RUN),
		OPT_BIT('f', "force", &flags, "force updates", TRANSPORT_PUSH_FORCE),
		OPT_BOOLEAN( 0 , "thin", &thin, "use thin pack"),
		OPT_STRING( 0 , "receive-pack", &receivepack, "receive-pack", "receive pack program"),
		OPT_STRING( 0 , "exec", &receivepack, "receive-pack", "receive pack program"),
		OPT_END()
	};

	argc = parse_options(argc, argv, options, push_usage, 0);

	if (tags)
		add_refspec("refs/tags/*");

	if (argc > 0) {
		repo = argv[0];
		set_refspecs(argv + 1, argc - 1);
	}

	rc = do_push(repo, flags);
	if (rc == -1)
		usage_with_options(push_usage, options);
	else
		return rc;
}
