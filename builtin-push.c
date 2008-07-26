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
	"git push [--all | --mirror] [--dry-run] [--tags] [--receive-pack=<git-receive-pack>] [--repo=all] [-f | --force] [-v] [<repository> <refspec>...]",
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

static int do_push(const char *repo, int flags)
{
	int i, errs;
	struct remote *remote = remote_get(repo);

	if (!remote)
		die("bad repository '%s'", repo);

	if (remote->mirror)
		flags |= (TRANSPORT_PUSH_MIRROR|TRANSPORT_PUSH_FORCE);

	if ((flags & (TRANSPORT_PUSH_ALL|TRANSPORT_PUSH_MIRROR)) && refspec)
		return -1;

	if ((flags & (TRANSPORT_PUSH_ALL|TRANSPORT_PUSH_MIRROR)) ==
				(TRANSPORT_PUSH_ALL|TRANSPORT_PUSH_MIRROR)) {
		return error("--all and --mirror are incompatible");
	}

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
