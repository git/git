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

static int thin = 1;
static int deleterefs;
static const char *receivepack;
static int verbosity;
static int progress = -1;

static struct push_cas_option cas;

static const char **refspec;
static int refspec_nr;
static int refspec_alloc;

static void add_refspec(const char *ref)
{
	refspec_nr++;
	ALLOC_GROW(refspec, refspec_nr, refspec_alloc);
	refspec[refspec_nr-1] = ref;
}

static const char *map_refspec(const char *ref,
			       struct remote *remote, struct ref *local_refs)
{
	struct ref *matched = NULL;

	/* Does "ref" uniquely name our ref? */
	if (count_refspec_match(ref, local_refs, &matched) != 1)
		return ref;

	if (remote->push) {
		struct refspec query;
		memset(&query, 0, sizeof(struct refspec));
		query.src = matched->name;
		if (!query_refspecs(remote->push, remote->push_refspec_nr, &query) &&
		    query.dst) {
			struct strbuf buf = STRBUF_INIT;
			strbuf_addf(&buf, "%s%s:%s",
				    query.force ? "+" : "",
				    query.src, query.dst);
			return strbuf_detach(&buf, NULL);
		}
	}

	if (push_default == PUSH_DEFAULT_UPSTREAM &&
	    starts_with(matched->name, "refs/heads/")) {
		struct branch *branch = branch_get(matched->name + 11);
		if (branch->merge_nr == 1 && branch->merge[0]->src) {
			struct strbuf buf = STRBUF_INIT;
			strbuf_addf(&buf, "%s:%s",
				    ref, branch->merge[0]->src);
			return strbuf_detach(&buf, NULL);
		}
	}

	return ref;
}

static void set_refspecs(const char **refs, int nr, const char *repo)
{
	struct remote *remote = NULL;
	struct ref *local_refs = NULL;
	int i;

	for (i = 0; i < nr; i++) {
		const char *ref = refs[i];
		if (!strcmp("tag", ref)) {
			struct strbuf tagref = STRBUF_INIT;
			if (nr <= ++i)
				die(_("tag shorthand without <tag>"));
			ref = refs[i];
			if (deleterefs)
				strbuf_addf(&tagref, ":refs/tags/%s", ref);
			else
				strbuf_addf(&tagref, "refs/tags/%s", ref);
			ref = strbuf_detach(&tagref, NULL);
		} else if (deleterefs) {
			struct strbuf delref = STRBUF_INIT;
			if (strchr(ref, ':'))
				die(_("--delete only accepts plain target ref names"));
			strbuf_addf(&delref, ":%s", ref);
			ref = strbuf_detach(&delref, NULL);
		} else if (!strchr(ref, ':')) {
			if (!remote) {
				/* lazily grab remote and local_refs */
				remote = remote_get(repo);
				local_refs = get_local_heads();
			}
			ref = map_refspec(ref, remote, local_refs);
		}
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
	const char *short_upstream = branch->merge[0]->src;

	skip_prefix(short_upstream, "refs/heads/", &short_upstream);

	/*
	 * Don't show advice for people who explicitly set
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

static const char message_detached_head_die[] =
	N_("You are not currently on a branch.\n"
	   "To push the history leading to the current (detached HEAD)\n"
	   "state now, use\n"
	   "\n"
	   "    git push %s HEAD:<name-of-remote-branch>\n");

static void setup_push_upstream(struct remote *remote, struct branch *branch,
				int triangular)
{
	struct strbuf refspec = STRBUF_INIT;

	if (!branch)
		die(_(message_detached_head_die), remote->name);
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
	if (triangular)
		die(_("You are pushing to remote '%s', which is not the upstream of\n"
		      "your current branch '%s', without telling me what to push\n"
		      "to update which remote branch."),
		    remote->name, branch->name);

	if (push_default == PUSH_DEFAULT_SIMPLE) {
		/* Additional safety */
		if (strcmp(branch->refname, branch->merge[0]->src))
			die_push_simple(branch, remote);
	}

	strbuf_addf(&refspec, "%s:%s", branch->name, branch->merge[0]->src);
	add_refspec(refspec.buf);
}

static void setup_push_current(struct remote *remote, struct branch *branch)
{
	if (!branch)
		die(_(message_detached_head_die), remote->name);
	add_refspec(branch->name);
}

static char warn_unspecified_push_default_msg[] =
N_("push.default is unset; its implicit value has changed in\n"
   "Git 2.0 from 'matching' to 'simple'. To squelch this message\n"
   "and maintain the traditional behavior, use:\n"
   "\n"
   "  git config --global push.default matching\n"
   "\n"
   "To squelch this message and adopt the new behavior now, use:\n"
   "\n"
   "  git config --global push.default simple\n"
   "\n"
   "When push.default is set to 'matching', git will push local branches\n"
   "to the remote branches that already exist with the same name.\n"
   "\n"
   "Since Git 2.0, Git defaults to the more conservative 'simple'\n"
   "behavior, which only pushes the current branch to the corresponding\n"
   "remote branch that 'git pull' uses to update the current branch.\n"
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

static int is_workflow_triangular(struct remote *remote)
{
	struct remote *fetch_remote = remote_get(NULL);
	return (fetch_remote && fetch_remote != remote);
}

static void setup_default_push_refspecs(struct remote *remote)
{
	struct branch *branch = branch_get(NULL);
	int triangular = is_workflow_triangular(remote);

	switch (push_default) {
	default:
	case PUSH_DEFAULT_MATCHING:
		add_refspec(":");
		break;

	case PUSH_DEFAULT_UNSPECIFIED:
		warn_unspecified_push_default_configuration();
		/* fallthru */

	case PUSH_DEFAULT_SIMPLE:
		if (triangular)
			setup_push_current(remote, branch);
		else
			setup_push_upstream(remote, branch, triangular);
		break;

	case PUSH_DEFAULT_UPSTREAM:
		setup_push_upstream(remote, branch, triangular);
		break;

	case PUSH_DEFAULT_CURRENT:
		setup_push_current(remote, branch);
		break;

	case PUSH_DEFAULT_NOTHING:
		die(_("You didn't specify any refspecs to push, and "
		    "push.default is \"nothing\"."));
		break;
	}
}

static const char message_advice_pull_before_push[] =
	N_("Updates were rejected because the tip of your current branch is behind\n"
	   "its remote counterpart. Integrate the remote changes (e.g.\n"
	   "'git pull ...') before pushing again.\n"
	   "See the 'Note about fast-forwards' in 'git push --help' for details.");

static const char message_advice_checkout_pull_push[] =
	N_("Updates were rejected because a pushed branch tip is behind its remote\n"
	   "counterpart. Check out this branch and integrate the remote changes\n"
	   "(e.g. 'git pull ...') before pushing again.\n"
	   "See the 'Note about fast-forwards' in 'git push --help' for details.");

static const char message_advice_ref_fetch_first[] =
	N_("Updates were rejected because the remote contains work that you do\n"
	   "not have locally. This is usually caused by another repository pushing\n"
	   "to the same ref. You may want to first integrate the remote changes\n"
	   "(e.g., 'git pull ...') before pushing again.\n"
	   "See the 'Note about fast-forwards' in 'git push --help' for details.");

static const char message_advice_ref_already_exists[] =
	N_("Updates were rejected because the tag already exists in the remote.");

static const char message_advice_ref_needs_force[] =
	N_("You cannot update a remote ref that points at a non-commit object,\n"
	   "or update a remote ref to make it point at a non-commit object,\n"
	   "without using the '--force' option.\n");

static void advise_pull_before_push(void)
{
	if (!advice_push_non_ff_current || !advice_push_update_rejected)
		return;
	advise(_(message_advice_pull_before_push));
}

static void advise_checkout_pull_push(void)
{
	if (!advice_push_non_ff_matching || !advice_push_update_rejected)
		return;
	advise(_(message_advice_checkout_pull_push));
}

static void advise_ref_already_exists(void)
{
	if (!advice_push_already_exists || !advice_push_update_rejected)
		return;
	advise(_(message_advice_ref_already_exists));
}

static void advise_ref_fetch_first(void)
{
	if (!advice_push_fetch_first || !advice_push_update_rejected)
		return;
	advise(_(message_advice_ref_fetch_first));
}

static void advise_ref_needs_force(void)
{
	if (!advice_push_needs_force || !advice_push_update_rejected)
		return;
	advise(_(message_advice_ref_needs_force));
}

static int push_with_options(struct transport *transport, int flags)
{
	int err;
	unsigned int reject_reasons;

	transport_set_verbosity(transport, verbosity, progress);

	if (receivepack)
		transport_set_option(transport,
				     TRANS_OPT_RECEIVEPACK, receivepack);
	transport_set_option(transport, TRANS_OPT_THIN, thin ? "yes" : NULL);

	if (!is_empty_cas(&cas)) {
		if (!transport->smart_options)
			die("underlying transport does not support --%s option",
			    CAS_OPT_NAME);
		transport->smart_options->cas = &cas;
	}

	if (verbosity > 0)
		fprintf(stderr, _("Pushing to %s\n"), transport->url);
	err = transport_push(transport, refspec_nr, refspec, flags,
			     &reject_reasons);
	if (err != 0)
		error(_("failed to push some refs to '%s'"), transport->url);

	err |= transport_disconnect(transport);
	if (!err)
		return 0;

	if (reject_reasons & REJECT_NON_FF_HEAD) {
		advise_pull_before_push();
	} else if (reject_reasons & REJECT_NON_FF_OTHER) {
		advise_checkout_pull_push();
	} else if (reject_reasons & REJECT_ALREADY_EXISTS) {
		advise_ref_already_exists();
	} else if (reject_reasons & REJECT_FETCH_FIRST) {
		advise_ref_fetch_first();
	} else if (reject_reasons & REJECT_NEEDS_FORCE) {
		advise_ref_needs_force();
	}

	return 1;
}

static int do_push(const char *repo, int flags)
{
	int i, errs;
	struct remote *remote = pushremote_get(repo);
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
		OPT_BOOL( 0, "delete", &deleterefs, N_("delete refs")),
		OPT_BOOL( 0 , "tags", &tags, N_("push tags (can't be used with --all or --mirror)")),
		OPT_BIT('n' , "dry-run", &flags, N_("dry run"), TRANSPORT_PUSH_DRY_RUN),
		OPT_BIT( 0,  "porcelain", &flags, N_("machine-readable output"), TRANSPORT_PUSH_PORCELAIN),
		OPT_BIT('f', "force", &flags, N_("force updates"), TRANSPORT_PUSH_FORCE),
		{ OPTION_CALLBACK,
		  0, CAS_OPT_NAME, &cas, N_("refname>:<expect"),
		  N_("require old value of ref to be at this value"),
		  PARSE_OPT_OPTARG, parseopt_push_cas_option },
		{ OPTION_CALLBACK, 0, "recurse-submodules", &flags, N_("check"),
			N_("control recursive pushing of submodules"),
			PARSE_OPT_OPTARG, option_parse_recurse_submodules },
		OPT_BOOL( 0 , "thin", &thin, N_("use thin pack")),
		OPT_STRING( 0 , "receive-pack", &receivepack, "receive-pack", N_("receive pack program")),
		OPT_STRING( 0 , "exec", &receivepack, "receive-pack", N_("receive pack program")),
		OPT_BIT('u', "set-upstream", &flags, N_("set upstream for git pull/status"),
			TRANSPORT_PUSH_SET_UPSTREAM),
		OPT_BOOL(0, "progress", &progress, N_("force progress reporting")),
		OPT_BIT(0, "prune", &flags, N_("prune locally removed refs"),
			TRANSPORT_PUSH_PRUNE),
		OPT_BIT(0, "no-verify", &flags, N_("bypass pre-push hook"), TRANSPORT_PUSH_NO_HOOK),
		OPT_BIT(0, "follow-tags", &flags, N_("push missing but relevant tags"),
			TRANSPORT_PUSH_FOLLOW_TAGS),
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
		set_refspecs(argv + 1, argc - 1, repo);
	}

	rc = do_push(repo, flags);
	if (rc == -1)
		usage_with_options(push_usage, options);
	else
		return rc;
}
