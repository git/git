/*
 * "git replay" builtin command
 */

#include "git-compat-util.h"

#include "builtin.h"
#include "config.h"
#include "hex.h"
#include "object-name.h"
#include "parse-options.h"
#include "refs.h"
#include "replay.h"
#include "revision.h"

enum ref_action_mode {
	REF_ACTION_UPDATE,
	REF_ACTION_PRINT,
};

static enum ref_action_mode parse_ref_action_mode(const char *ref_action, const char *source)
{
	if (!ref_action || !strcmp(ref_action, "update"))
		return REF_ACTION_UPDATE;
	if (!strcmp(ref_action, "print"))
		return REF_ACTION_PRINT;
	die(_("invalid %s value: '%s'"), source, ref_action);
}

static enum ref_action_mode get_ref_action_mode(struct repository *repo, const char *ref_action)
{
	const char *config_value = NULL;

	/* Command line option takes precedence */
	if (ref_action)
		return parse_ref_action_mode(ref_action, "--ref-action");

	/* Check config value */
	if (!repo_config_get_string_tmp(repo, "replay.refAction", &config_value))
		return parse_ref_action_mode(config_value, "replay.refAction");

	/* Default to update mode */
	return REF_ACTION_UPDATE;
}

static int handle_ref_update(enum ref_action_mode mode,
			     struct ref_transaction *transaction,
			     const char *refname,
			     const struct object_id *new_oid,
			     const struct object_id *old_oid,
			     const char *reflog_msg,
			     struct strbuf *err)
{
	switch (mode) {
	case REF_ACTION_PRINT:
		printf("update %s %s %s\n",
		       refname,
		       oid_to_hex(new_oid),
		       oid_to_hex(old_oid));
		return 0;
	case REF_ACTION_UPDATE:
		return ref_transaction_update(transaction, refname, new_oid, old_oid,
					      NULL, NULL, 0, reflog_msg, err);
	default:
		BUG("unknown ref_action_mode %d", mode);
	}
}

int cmd_replay(int argc,
	       const char **argv,
	       const char *prefix,
	       struct repository *repo)
{
	struct replay_revisions_options opts = { 0 };
	struct replay_result result = { 0 };
	const char *ref_action = NULL;
	enum ref_action_mode ref_mode;
	struct rev_info revs;
	struct ref_transaction *transaction = NULL;
	struct strbuf transaction_err = STRBUF_INIT;
	struct strbuf reflog_msg = STRBUF_INIT;
	int ret = 0;

	const char *const replay_usage[] = {
		N_("(EXPERIMENTAL!) git replay "
		   "([--contained] --onto <newbase> | --advance <branch>) "
		   "[--ref-action[=<mode>]] <revision-range>"),
		NULL
	};
	struct option replay_options[] = {
		OPT_STRING(0, "advance", &opts.advance,
			   N_("branch"),
			   N_("make replay advance given branch")),
		OPT_STRING(0, "onto", &opts.onto,
			   N_("revision"),
			   N_("replay onto given commit")),
		OPT_BOOL(0, "contained", &opts.contained,
			 N_("update all branches that point at commits in <revision-range>")),
		OPT_STRING(0, "ref-action", &ref_action,
			   N_("mode"),
			   N_("control ref update behavior (update|print)")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, replay_options, replay_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT);

	if (!opts.onto && !opts.advance) {
		error(_("option --onto or --advance is mandatory"));
		usage_with_options(replay_usage, replay_options);
	}

	die_for_incompatible_opt2(!!opts.advance, "--advance",
				  opts.contained, "--contained");
	die_for_incompatible_opt2(!!opts.advance, "--advance",
				  !!opts.onto, "--onto");

	/* Parse ref action mode from command line or config */
	ref_mode = get_ref_action_mode(repo, ref_action);

	repo_init_revisions(repo, &revs, prefix);

	/*
	 * Set desired values for rev walking options here. If they
	 * are changed by some user specified option in setup_revisions()
	 * below, we will detect that below and then warn.
	 *
	 * TODO: In the future we might want to either die(), or allow
	 * some options changing these values if we think they could
	 * be useful.
	 */
	revs.reverse = 1;
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;
	revs.simplify_history = 0;

	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1) {
		ret = error(_("unrecognized argument: %s"), argv[1]);
		goto cleanup;
	}

	/*
	 * Detect and warn if we override some user specified rev
	 * walking options.
	 */
	if (revs.reverse != 1) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"reverse");
		revs.reverse = 1;
	}
	if (revs.sort_order != REV_SORT_IN_GRAPH_ORDER) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"sort_order");
		revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	}
	if (revs.topo_order != 1) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"topo_order");
		revs.topo_order = 1;
	}
	if (revs.simplify_history != 0) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"simplify_history");
		revs.simplify_history = 0;
	}

	ret = replay_revisions(&revs, &opts, &result);
	if (ret)
		goto cleanup;

	/* Build reflog message */
	if (opts.advance) {
		strbuf_addf(&reflog_msg, "replay --advance %s", opts.advance);
	} else {
		struct object_id oid;
		if (repo_get_oid_committish(repo, opts.onto, &oid))
			BUG("--onto commit should have been resolved beforehand already");
		strbuf_addf(&reflog_msg, "replay --onto %s", oid_to_hex(&oid));
	}

	/* Initialize ref transaction if using update mode */
	if (ref_mode == REF_ACTION_UPDATE) {
		transaction = ref_store_transaction_begin(get_main_ref_store(repo),
							  0, &transaction_err);
		if (!transaction) {
			ret = error(_("failed to begin ref transaction: %s"),
				    transaction_err.buf);
			goto cleanup;
		}
	}

	for (size_t i = 0; i < result.updates_nr; i++) {
		ret = handle_ref_update(ref_mode, transaction, result.updates[i].refname,
					&result.updates[i].new_oid, &result.updates[i].old_oid,
					reflog_msg.buf, &transaction_err);
		if (ret) {
			ret = error(_("failed to update ref '%s': %s"),
				    result.updates[i].refname, transaction_err.buf);
			goto cleanup;
		}
	}

	/* Commit the ref transaction if we have one */
	if (transaction) {
		if (ref_transaction_commit(transaction, &transaction_err)) {
			ret = error(_("failed to commit ref transaction: %s"),
				    transaction_err.buf);
			goto cleanup;
		}
	}

	ret = 0;

cleanup:
	if (transaction)
		ref_transaction_free(transaction);
	replay_result_release(&result);
	strbuf_release(&transaction_err);
	strbuf_release(&reflog_msg);
	release_revisions(&revs);

	/* Return */
	if (ret < 0)
		exit(128);
	return ret;
}
