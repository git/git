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
	N_("git push [<options>] [<repository> [<refspec>...]]"),
	NULL,
};

static int thin;
static int deleterefs;
static const char *receivepack;
static int verbosity;
static int progress = -1;

static const char **refspec;
static int refspec_nr;
static int refspec_alloc;
static int default_matching_used;

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

static int push_url_of_remote(struct remote *remote, const char ***url_p)
{
	if (remote->pushurl_nr) {
		*url_p = remote->pushurl;
		return remote->pushurl_nr;
	}
	*url_p = remote->url;
	return remote->url_nr;
}

static NORETURN int die_push_simple(struct branch *branch, struct remote *remote) {
	/*
	 * There's no point in using shorten_unambiguous_ref here,
	 * as the ambiguity would be on the remote side, not what
	 * we have locally. Plus, this is supposed to be the simple
	 * mode. If the user is doing something crazy like setting
	 * upstream to a non-branch, we should probably be showing
	 * them the big ugly fully qualified ref.
	 */
	const char *advice_maybe = "";
	const char *short_upstream =
		skip_prefix(branch->merge[0]->src, "refs/heads/");

	if (!short_upstream)
		short_upstream = branch->merge[0]->src;
	/*
	 * Don't show advice for people who explicitely set
	 * push.default.
	 */
	if (push_default == PUSH_DEFAULT_UNSPECIFIED)
		advice_maybe = _("\n"
				 "To choose either option permanently, "
				 "see push.default in 'git help config'.");
	die(_("The upstream branch of your current branch does not match\n"
	      "the name of your current branch.  To push to the upstream branch\n"
	      "on the remote, use\n"
	      "\n"
	      "    git push %s HEAD:%s\n"
	      "\n"
	      "To push to the branch of the same name on the remote, use\n"
	      "\n"
	      "    git push %s %s\n"
	      "%s"),
	    remote->name, short_upstream,
	    remote->name, branch->name, advice_maybe);
}

static void setup_push_upstream(struct remote *remote, int simple)
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
	if (!branch->merge_nr || !branch->merge || !branch->remote_name)
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
	if (strcmp(branch->remote_name, remote->name))
		die(_("You are pushing to remote '%s', which is not the upstream of\n"
		      "your current branch '%s', without telling me what to push\n"
		      "to update which remote branch."),
		    remote->name, branch->name);
	if (simple && strcmp(branch->refname, branch->merge[0]->src))
		die_push_simple(branch, remote);

	strbuf_addf(&refspec, "%s:%s", branch->name, branch->merge[0]->src);
	add_refspec(refspec.buf);
}

static char warn_unspecified_push_default_msg[] =
N_("push.default is unset; its implicit value is changing in\n"
   "Git 2.0 from 'matching' to 'simple'. To squelch this message\n"
   "and maintain the current behavior after the default changes, use:\n"
   "\n"
   "  git config --global push.default matching\n"
   "\n"
   "To squelch this message and adopt the new behavior now, use:\n"
   "\n"
   "  git config --global push.default simple\n"
   "\n"
   "See 'git help config' and search for 'push.default' for further information.\n"
   "(the 'simple' mode was introduced in Git 1.7.11. Use the similar mode\n"
   "'current' instead of 'simple' if you sometimes use older versions of Git)");

static void warn_unspecified_push_default_configuration(void)
{
	static int warn_once;

	if (warn_once++)
		return;
	warning("%s\n", _(warn_unspecified_push_default_msg));
}

static void setup_default_push_refspecs(struct remote *remote)
{
	switch (push_default) {
	default:
	case PUSH_DEFAULT_UNSPECIFIED:
		default_matching_used = 1;
		warn_unspecified_push_default_configuration();
		/* fallthru */
	case PUSH_DEFAULT_MATCHING:
		add_refspec(":");
		break;

	case PUSH_DEFAULT_SIMPLE:
		setup_push_upstream(remote, 1);
		break;

	case PUSH_DEFAULT_UPSTREAM:
		setup_push_upstream(remote, 0);
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

static const char message_advice_pull_before_push[] =
	N_("Updates were rejected because the tip of your current branch is behind\n"
	   "its remote counterpart. Merge the remote changes (e.g. 'git pull')\n"
	   "before pushing again.\n"
	   "See the 'Note about fast-forwards' in 'git push --help' for details.");

static const char message_advice_use_upstream[] =
	N_("Updates were rejected because a pushed branch tip is behind its remote\n"
	   "counterpart. If you did not intend to push that branch, you may want to\n"
	   "specify branches to push or set the 'push.default' configuration variable\n"
	   "to 'simple', 'current' or 'upstream' to push only the current branch.");

static const char message_advice_checkout_pull_push[] =
	N_("Updates were rejected because a pushed branch tip is behind its remote\n"
	   "counterpart. Check out this branch and merge the remote changes\n"
	   "(e.g. 'git pull') before pushing again.\n"
	   "See the 'Note about fast-forwards' in 'git push --help' for details.");

static void advise_pull_before_push(void)
{
	if (!advice_push_non_ff_current || !advice_push_nonfastforward)
		return;
	advise(_(message_advice_pull_before_push));
}

static void advise_use_upstream(void)
{
	if (!advice_push_non_ff_default || !advice_push_nonfastforward)
		return;
	advise(_(message_advice_use_upstream));
}

static void advise_checkout_pull_push(void)
{
	if (!advice_push_non_ff_matching || !advice_push_nonfastforward)
		return;
	advise(_(message_advice_checkout_pull_push));
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

	switch (nonfastforward) {
	default:
		break;
	case NON_FF_HEAD:
		advise_pull_before_push();
		break;
	case NON_FF_OTHER:
		if (default_matching_used)
			advise_use_upstream();
		else
			advise_checkout_pull_push();
		break;
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
	url_nr = push_url_of_remote(remote, &url);
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

	if (*flags & (TRANSPORT_RECURSE_SUBMODULES_CHECK |
		      TRANSPORT_RECURSE_SUBMODULES_ON_DEMAND))
		die("%s can only be used once.", opt->long_name);

	if (arg) {
		if (!strcmp(arg, "check"))
			*flags |= TRANSPORT_RECURSE_SUBMODULES_CHECK;
		else if (!strcmp(arg, "on-demand"))
			*flags |= TRANSPORT_RECURSE_SUBMODULES_ON_DEMAND;
		else
			die("bad %s argument: %s", opt->long_name, arg);
	} else
		die("option %s needs an argument (check|on-demand)",
				opt->long_name);

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
		OPT_STRING( 0 , "repo", &repo, N_("repository"), N_("repository")),
		OPT_BIT( 0 , "all", &flags, N_("push all refs"), TRANSPORT_PUSH_ALL),
		OPT_BIT( 0 , "mirror", &flags, N_("mirror all refs"),
			    (TRANSPORT_PUSH_MIRROR|TRANSPORT_PUSH_FORCE)),
		OPT_BOOLEAN( 0, "delete", &deleterefs, N_("delete refs")),
		OPT_BOOLEAN( 0 , "tags", &tags, N_("push tags (can't be used with --all or --mirror)")),
		OPT_BIT('n' , "dry-run", &flags, N_("dry run"), TRANSPORT_PUSH_DRY_RUN),
		OPT_BIT( 0,  "porcelain", &flags, N_("machine-readable output"), TRANSPORT_PUSH_PORCELAIN),
		OPT_BIT('f', "force", &flags, N_("force updates"), TRANSPORT_PUSH_FORCE),
		{ OPTION_CALLBACK, 0, "recurse-submodules", &flags, N_("check"),
			N_("control recursive pushing of submodules"),
			PARSE_OPT_OPTARG, option_parse_recurse_submodules },
		OPT_BOOLEAN( 0 , "thin", &thin, N_("use thin pack")),
		OPT_STRING( 0 , "receive-pack", &receivepack, "receive-pack", N_("receive pack program")),
		OPT_STRING( 0 , "exec", &receivepack, "receive-pack", N_("receive pack program")),
		OPT_BIT('u', "set-upstream", &flags, N_("set upstream for git pull/status"),
			TRANSPORT_PUSH_SET_UPSTREAM),
		OPT_BOOL(0, "progress", &progress, N_("force progress reporting")),
		OPT_BIT(0, "prune", &flags, N_("prune locally removed refs"),
			TRANSPORT_PUSH_PRUNE),
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
