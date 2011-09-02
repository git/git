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
#include "submodule.h"

static const char * const push_usage[] = {
	"git push [<options>] [<repository> [<refspec>...]]",
	NULL,
};

static int thin;
static int deleterefs;
static const char *receivepack;
static int verbosity;
static int progress;

static const char **refspec;
static int refspec_nr;
static int refspec_alloc;

static void add_refspec(const char *ref)
{
	refspec_nr++;
	ALLOC_GROW(refspec, refspec_nr, refspec_alloc);
	refspec[refspec_nr-1] = ref;
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
				die(_("tag shorthand without <tag>"));
			len = strlen(refs[i]) + 11;
			if (deleterefs) {
				tag = xmalloc(len+1);
				strcpy(tag, ":refs/tags/");
			} else {
				tag = xmalloc(len);
				strcpy(tag, "refs/tags/");
			}
			strcat(tag, refs[i]);
			ref = tag;
		} else if (deleterefs && !strchr(ref, ':')) {
			char *delref;
			int len = strlen(ref)+1;
			delref = xmalloc(len+1);
			strcpy(delref, ":");
			strcat(delref, ref);
			ref = delref;
		} else if (deleterefs)
			die(_("--delete only accepts plain target ref names"));
		add_refspec(ref);
	}
}

static void setup_push_upstream(struct remote *remote)
{
	struct strbuf refspec = STRBUF_INIT;
	struct branch *branch = branch_get(NULL);
	if (!branch)
		die(_("You are not currently on a branch.\n"
		    "To push the history leading to the current (detached HEAD)\n"
		    "state now, use\n"
		    "\n"
		    "    git push %s HEAD:<name-of-remote-branch>\n"),
		    remote->name);
	if (!branch->merge_nr || !branch->merge)
		die(_("The current branch %s has no upstream branch.\n"
		    "To push the current branch and set the remote as upstream, use\n"
		    "\n"
		    "    git push --set-upstream %s %s\n"),
		    branch->name,
		    remote->name,
		    branch->name);
	if (branch->merge_nr != 1)
		die(_("The current branch %s has multiple upstream branches, "
		    "refusing to push."), branch->name);
	strbuf_addf(&refspec, "%s:%s", branch->name, branch->merge[0]->src);
	add_refspec(refspec.buf);
}

static void setup_default_push_refspecs(struct remote *remote)
{
	switch (push_default) {
	default:
	case PUSH_DEFAULT_MATCHING:
		add_refspec(":");
		break;

	case PUSH_DEFAULT_UPSTREAM:
		setup_push_upstream(remote);
		break;

	case PUSH_DEFAULT_CURRENT:
		add_refspec("HEAD");
		break;

	case PUSH_DEFAULT_NOTHING:
		die(_("You didn't specify any refspecs to push, and "
		    "push.default is \"nothing\"."));
		break;
	}
}

static int push_with_options(struct transport *transport, int flags)
{
	int err;
	int nonfastforward;

	transport_set_verbosity(transport, verbosity, progress);

	if (receivepack)
		transport_set_option(transport,
				     TRANS_OPT_RECEIVEPACK, receivepack);
	if (thin)
		transport_set_option(transport, TRANS_OPT_THIN, "yes");

	if (verbosity > 0)
		fprintf(stderr, _("Pushing to %s\n"), transport->url);
	err = transport_push(transport, refspec_nr, refspec, flags,
			     &nonfastforward);
	if (err != 0)
		error(_("failed to push some refs to '%s'"), transport->url);

	err |= transport_disconnect(transport);

	if (!err)
		return 0;

	if (nonfastforward && advice_push_nonfastforward) {
		fprintf(stderr, _("To prevent you from losing history, non-fast-forward updates were rejected\n"
				"Merge the remote changes (e.g. 'git pull') before pushing again.  See the\n"
				"'Note about fast-forwards' section of 'git push --help' for details.\n"));
	}

	return 1;
}

static int do_push(const char *repo, int flags)
{
	int i, errs;
	struct remote *remote = remote_get(repo);
	const char **url;
	int url_nr;

	if (!remote) {
		if (repo)
			die(_("bad repository '%s'"), repo);
		die(_("No configured push destination.\n"
		    "Either specify the URL from the command-line or configure a remote repository using\n"
		    "\n"
		    "    git remote add <name> <url>\n"
		    "\n"
		    "and then push using the remote name\n"
		    "\n"
		    "    git push <name>\n"));
	}

	if (remote->mirror)
		flags |= (TRANSPORT_PUSH_MIRROR|TRANSPORT_PUSH_FORCE);

	if ((flags & TRANSPORT_PUSH_ALL) && refspec) {
		if (!strcmp(*refspec, "refs/tags/*"))
			return error(_("--all and --tags are incompatible"));
		return error(_("--all can't be combined with refspecs"));
	}

	if ((flags & TRANSPORT_PUSH_MIRROR) && refspec) {
		if (!strcmp(*refspec, "refs/tags/*"))
			return error(_("--mirror and --tags are incompatible"));
		return error(_("--mirror can't be combined with refspecs"));
	}

	if ((flags & (TRANSPORT_PUSH_ALL|TRANSPORT_PUSH_MIRROR)) ==
				(TRANSPORT_PUSH_ALL|TRANSPORT_PUSH_MIRROR)) {
		return error(_("--all and --mirror are incompatible"));
	}

	if (!refspec && !(flags & TRANSPORT_PUSH_ALL)) {
		if (remote->push_refspec_nr) {
			refspec = remote->push_refspec;
			refspec_nr = remote->push_refspec_nr;
		} else if (!(flags & TRANSPORT_PUSH_MIRROR))
			setup_default_push_refspecs(remote);
	}
	errs = 0;
	if (remote->pushurl_nr) {
		url = remote->pushurl;
		url_nr = remote->pushurl_nr;
	} else {
		url = remote->url;
		url_nr = remote->url_nr;
	}
	if (url_nr) {
		for (i = 0; i < url_nr; i++) {
			struct transport *transport =
				transport_get(remote, url[i]);
			if (push_with_options(transport, flags))
				errs++;
		}
	} else {
		struct transport *transport =
			transport_get(remote, NULL);

		if (push_with_options(transport, flags))
			errs++;
	}
	return !!errs;
}

static int option_parse_recurse_submodules(const struct option *opt,
				   const char *arg, int unset)
{
	int *flags = opt->value;
	if (arg) {
		if (!strcmp(arg, "check"))
			*flags |= TRANSPORT_RECURSE_SUBMODULES_CHECK;
		else
			die("bad %s argument: %s", opt->long_name, arg);
	} else
		die("option %s needs an argument (check)", opt->long_name);

	return 0;
}

int cmd_push(int argc, const char **argv, const char *prefix)
{
	int flags = 0;
	int tags = 0;
	int rc;
	const char *repo = NULL;	/* default repository */
	struct option options[] = {
		OPT__VERBOSITY(&verbosity),
		OPT_STRING( 0 , "repo", &repo, "repository", "repository"),
		OPT_BIT( 0 , "all", &flags, "push all refs", TRANSPORT_PUSH_ALL),
		OPT_BIT( 0 , "mirror", &flags, "mirror all refs",
			    (TRANSPORT_PUSH_MIRROR|TRANSPORT_PUSH_FORCE)),
		OPT_BOOLEAN( 0, "delete", &deleterefs, "delete refs"),
		OPT_BOOLEAN( 0 , "tags", &tags, "push tags (can't be used with --all or --mirror)"),
		OPT_BIT('n' , "dry-run", &flags, "dry run", TRANSPORT_PUSH_DRY_RUN),
		OPT_BIT( 0,  "porcelain", &flags, "machine-readable output", TRANSPORT_PUSH_PORCELAIN),
		OPT_BIT('f', "force", &flags, "force updates", TRANSPORT_PUSH_FORCE),
		{ OPTION_CALLBACK, 0, "recurse-submodules", &flags, "check",
			"controls recursive pushing of submodules",
			PARSE_OPT_OPTARG, option_parse_recurse_submodules },
		OPT_BOOLEAN( 0 , "thin", &thin, "use thin pack"),
		OPT_STRING( 0 , "receive-pack", &receivepack, "receive-pack", "receive pack program"),
		OPT_STRING( 0 , "exec", &receivepack, "receive-pack", "receive pack program"),
		OPT_BIT('u', "set-upstream", &flags, "set upstream for git pull/status",
			TRANSPORT_PUSH_SET_UPSTREAM),
		OPT_BOOLEAN(0, "progress", &progress, "force progress reporting"),
		OPT_END()
	};

	packet_trace_identity("push");
	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options, push_usage, 0);

	if (deleterefs && (tags || (flags & (TRANSPORT_PUSH_ALL | TRANSPORT_PUSH_MIRROR))))
		die(_("--delete is incompatible with --all, --mirror and --tags"));
	if (deleterefs && argc < 2)
		die(_("--delete doesn't make sense without any refs"));

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
