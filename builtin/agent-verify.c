/*
 * Builtin "git agent-verify"
 *
 * Audits agent commits for consistency and policy compliance.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "agent.h"
#include "commit.h"
#include "json-writer.h"
#include "object-name.h"
#include "parse-options.h"
#include "pretty.h"
#include "revision.h"
#include "strbuf.h"
#include "config.h"
#include "hex.h"

static const char * const builtin_agent_verify_usage[] = {
	N_("git agent-verify [<commit>...]"),
	N_("git agent-verify --range <commit>..<commit>"),
	NULL
};

static int require_signed = -1; /* -1 = not configured, 0 = false, 1 = true */

static int read_agent_config(const char *key, const char *value,
			   const struct config_context *ctx UNUSED,
			   void *cb UNUSED)
{
	if (!strcmp(key, "agent.requiresignedcommits")) {
		require_signed = git_config_bool(key, value);
	}
	return 0;
}

struct verify_result {
	int issues;
	int total;
};

static void verify_commit(struct commit *commit, struct verify_result *res)
{
	struct agent_commit_meta meta;
	const char *msg;
	int warnings = 0;

	res->total++;
	msg = repo_get_commit_buffer(the_repository, commit, NULL);
	agent_parse_trailers(msg, strlen(msg), &meta);

	if (!meta.has_agent_data) {
		repo_unuse_commit_buffer(the_repository, commit, msg);
		agent_commit_meta_release(&meta);
		return;
	}

	warnings += agent_validate_trailers(msg, strlen(msg));

	if (meta.agent_confidence >= 0.0f &&
	    meta.agent_confidence < 0.5f &&
	    meta.agent_autonomy != AGENT_AUTONOMY_DRY_RUN) {
		warning(_("commit %s: low confidence %f without dry-run"),
			oideq(&commit->object.oid, null_oid(the_hash_algo)) ? "???" :
			oid_to_hex(&commit->object.oid),
			meta.agent_confidence);
		warnings++;
	}

	if (meta.agent_autonomy == AGENT_AUTONOMY_UNKNOWN &&
	    meta.has_agent_data) {
		warning(_("commit %s: missing or unknown Agent-Autonomy"),
			oideq(&commit->object.oid, null_oid(the_hash_algo)) ? "???" :
			oid_to_hex(&commit->object.oid));
		warnings++;
	}

	if (meta.agent_context_hash && *meta.agent_context_hash) {
		struct strbuf ctx_blob = STRBUF_INIT;
		if (agent_ref_read(&commit->object.oid, AGENT_KEY_CONTEXT,
				   &ctx_blob)) {
			warning(_("commit %s: Agent-Context-Hash points to "
				  "missing context blob"),
				oideq(&commit->object.oid, null_oid(the_hash_algo)) ?
				"???" :
				oid_to_hex(&commit->object.oid));
			warnings++;
		}
		strbuf_release(&ctx_blob);
	}

	repo_unuse_commit_buffer(the_repository, commit, msg);
	agent_commit_meta_release(&meta);

	if (warnings)
		res->issues++;
}

int cmd_agent_verify(int argc,
		     const char **argv,
		     const char *prefix,
		     struct repository *repo UNUSED)
{
	const char *range = NULL;
	struct option options[] = {
		OPT_STRING(0, "range", &range, N_("range"),
			   N_("commit range to verify")),
		OPT_END()
	};
	struct verify_result res = { 0, 0 };
	struct json_writer jw = JSON_WRITER_INIT;
	struct rev_info rev;
	struct commit *commit;
	repo_config(the_repository, read_agent_config, NULL);

	argc = parse_options(argc, argv, prefix, options,
			     builtin_agent_verify_usage,
			     PARSE_OPT_KEEP_ARGV0 |
			     PARSE_OPT_KEEP_UNKNOWN_OPT |
			     PARSE_OPT_KEEP_DASHDASH);

	repo_init_revisions(the_repository, &rev, prefix);
	if (range) {
		char *range_arg = xstrfmt("%s", range);
		setup_revisions(1, (const char **)&range_arg, &rev,
				NULL);
		free(range_arg);
	} else if (argc > 1) {
		setup_revisions(argc, argv, &rev, NULL);
	} else {
		setup_revisions(0, NULL, &rev, NULL);
	}

	prepare_revision_walk(&rev);
	while ((commit = get_revision(&rev)))
		verify_commit(commit, &res);
	release_revisions(&rev);

	jw_object_begin(&jw, 0);
	jw_object_intmax(&jw, "commits_examined", res.total);
	jw_object_intmax(&jw, "commits_with_issues", res.issues);
	jw_object_bool(&jw, "passed", res.issues == 0);
	jw_end(&jw);
	printf("%s\n", jw.json.buf);
	jw_release(&jw);

	return res.issues ? 1 : 0;
}
