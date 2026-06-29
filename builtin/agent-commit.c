/*
 * Builtin "git agent-commit"
 *
 * Wraps git commit with agent metadata trailers and stores
 * annotations under refs/agent/commits/.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "agent.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "environment.h"
#include "json-writer.h"
#include "object-name.h"
#include "parse-options.h"
#include "revision.h"
#include "run-command.h"
#include "setup.h"
#include "strbuf.h"
#include "trailer.h"

/* cmd_commit is declared in builtin.h */

static const char * const builtin_agent_commit_usage[] = {
	N_("git agent-commit [<git-commit-options>]\n"
	   "               [--agent-id=<id>]\n"
	   "               [--agent-task=<task>]\n"
	   "               [--agent-confidence=<f>]\n"
	   "               [--agent-intent=<text>]\n"
	   "               [--agent-autonomy=<full|supervised|dry-run>]\n"
	   "               [--agent-context-hash=<hash>]\n"
	   "               [--agent-parent-commit=<sha>]\n"
	   "               [--agent-tool-version=<ver>]\n"
	   "               [--agent-checkpoint]\n"
	   "               [--write-semantic-diff]\n"
	   "               [--write-reasoning=<file>]\n"
	   "               [--write-plan=<file>]\n"
	   "               [--write-context=<file>]\n"
	   "               [--session-id=<id>]"),
	NULL
};

struct agent_trailer_arg {
	struct agent_trailer_arg *next;
	char *text;
};

static void free_trailer_args(struct agent_trailer_arg *head)
{
	while (head) {
		struct agent_trailer_arg *next = head->next;
		free(head->text);
		free(head);
		head = next;
	}
}

int cmd_agent_commit(int argc,
		     const char **argv,
		     const char *prefix,
		     struct repository *repo UNUSED)
{
	const char *agent_id = NULL;
	const char *agent_task = NULL;
	const char *agent_confidence_str = NULL;
	float agent_confidence = -1.0f;
	const char *agent_intent = NULL;
	const char *agent_autonomy = NULL;
	const char *agent_context_hash = NULL;
	const char *agent_parent_commit = NULL;
	const char *agent_tool_version = NULL;
	int agent_checkpoint = 0;
	int write_semantic_diff = 0;
	const char *reasoning = NULL;
	const char *plan = NULL;
	const char *diff_summary = NULL;
	const char *context = NULL;
	const char *write_reasoning = NULL;
	const char *write_plan = NULL;
	const char *write_context = NULL;
	const char *session_id = NULL;

	struct option agent_options[] = {
		OPT_STRING(0, "agent-id", &agent_id, N_("id"),
			   N_("agent identifier")),
		OPT_STRING(0, "agent-task", &agent_task, N_("task"),
			   N_("task or ticket id")),
		OPT_STRING(0, "agent-confidence", &agent_confidence_str,
			   N_("confidence"),
			   N_("confidence 0.0-1.0")),
		OPT_STRING(0, "agent-intent", &agent_intent, N_("intent"),
			   N_("intent summary (max 120 chars)")),
		OPT_STRING(0, "agent-autonomy", &agent_autonomy,
			   N_("autonomy"),
			   N_("full, supervised, or dry-run")),
		OPT_STRING(0, "agent-context-hash", &agent_context_hash,
			   N_("hash"),
			   N_("sha256 of context blob")),
		OPT_STRING(0, "agent-parent-commit", &agent_parent_commit,
			   N_("commit"),
			   N_("parent commit for reasoning fork")),
		OPT_STRING(0, "agent-tool-version", &agent_tool_version,
			   N_("version"),
			   N_("tool version string")),
		OPT_BOOL(0, "agent-checkpoint", &agent_checkpoint,
			 N_("mark as checkpoint")),
		OPT_BOOL(0, "write-semantic-diff", &write_semantic_diff,
			 N_("generate and store semantic diff")),
		OPT_STRING(0, "reasoning", &reasoning, N_("text"),
			   N_("store reasoning text directly")),
		OPT_STRING(0, "plan", &plan, N_("text"),
			   N_("store plan text directly")),
		OPT_STRING(0, "diff-summary", &diff_summary, N_("text"),
			   N_("store diff summary text directly")),
		OPT_STRING(0, "context", &context, N_("text"),
			   N_("store context text directly")),
		OPT_FILENAME(0, "write-reasoning", &write_reasoning,
			     N_("store reasoning blob from file")),
		OPT_FILENAME(0, "write-plan", &write_plan,
			     N_("store plan blob from file")),
		OPT_FILENAME(0, "write-context", &write_context,
			     N_("store context blob from file")),
		OPT_STRING(0, "session-id", &session_id, N_("session"),
			   N_("session identifier to log commit under")),
		OPT_END()
	};

	struct agent_trailer_arg *trailers = NULL;
	struct agent_trailer_arg **trailer_tail = &trailers;
	const char **new_argv = NULL;
	int new_argc;
	int i;
	int ret;
	struct object_id head_oid;
	struct strbuf reasoning_buf = STRBUF_INIT;
	struct strbuf plan_buf = STRBUF_INIT;
	struct strbuf context_buf = STRBUF_INIT;

	/* Parse only the agent options; leave the rest for commit. */
	argc = parse_options(argc, argv, prefix, agent_options,
			     builtin_agent_commit_usage,
			     PARSE_OPT_KEEP_UNKNOWN_OPT);

	if (agent_confidence_str) {
		char *endptr;
		agent_confidence = strtof(agent_confidence_str, &endptr);
		if (*endptr || agent_confidence < 0.0f || agent_confidence > 1.0f)
			die(_("invalid confidence value: %s"),
			    agent_confidence_str);
	}

	/* Build trailer arguments. */
	if (agent_id) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrfmt("Agent-Id: %s", agent_id);
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}
	if (agent_task) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrfmt("Agent-Task: %s", agent_task);
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}
	if (agent_confidence >= 0.0f) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrfmt("Agent-Confidence: %f", agent_confidence);
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}
	if (agent_intent) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrfmt("Agent-Intent: %s", agent_intent);
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}
	if (agent_autonomy) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrfmt("Agent-Autonomy: %s", agent_autonomy);
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}
	if (agent_context_hash) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrfmt("Agent-Context-Hash: %s", agent_context_hash);
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}
	if (agent_parent_commit) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrfmt("Agent-Parent-Commit: %s", agent_parent_commit);
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}
	if (agent_tool_version) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrfmt("Agent-Tool-Version: %s", agent_tool_version);
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}
	if (agent_checkpoint) {
		struct agent_trailer_arg *arg = xmalloc(sizeof(*arg));
		arg->text = xstrdup("Agent-Checkpoint: yes");
		arg->next = NULL;
		*trailer_tail = arg;
		trailer_tail = &arg->next;
	}

	/* Count how many entries we need.
	 * After parse_options with KEEP_UNKNOWN_OPT, argv[0..argc-1]
	 * are the unconsumed args (the original argv[0] command name
	 * has been overwritten by the first unconsumed token).
	 * We prepend "commit" as the new command name, copy all
	 * unconsumed args, then append --trailer <text> pairs.
	 */
	new_argc = 1 + argc;
	{
		struct agent_trailer_arg *arg;
		for (arg = trailers; arg; arg = arg->next)
			new_argc += 2; /* --trailer <text> */
	}

	new_argv = xcalloc(new_argc + 1, sizeof(*new_argv));
	new_argv[0] = "commit";
	for (i = 0; i < argc; i++)
		new_argv[1 + i] = argv[i];
	{
		struct agent_trailer_arg *arg;
		int j = 1 + argc;
		for (arg = trailers; arg; arg = arg->next) {
			new_argv[j++] = "--trailer";
			new_argv[j++] = arg->text;
		}
	}
	new_argv[new_argc] = NULL;

	/* Remember HEAD so we can detect if a commit was made. */
	if (refs_resolve_ref_unsafe(get_main_ref_store(the_repository), "HEAD", RESOLVE_REF_READING,
			       &head_oid, NULL)) {
		/* HEAD exists. */
	} else {
		oidclr(&head_oid, the_repository->hash_algo);
	}

	/* Call the standard commit command. */
	ret = cmd_commit(new_argc, new_argv, prefix, the_repository);

	free(new_argv);
	free_trailer_args(trailers);

	if (ret)
		return ret;

	/* Commit succeeded.  Read new HEAD. */
	{
		struct object_id new_head;
		if (!refs_resolve_ref_unsafe(get_main_ref_store(the_repository), "HEAD", RESOLVE_REF_READING,
					&new_head, NULL)) {
			warning(_("could not read HEAD after commit"));
			return 0;
		}
		if (oideq(&head_oid, &new_head)) {
			/* No new commit was created (e.g. --amend without
			 * changes, or nothing to commit).
			 */
			return 0;
		}

		/* Store annotations. */
		if (reasoning) {
			agent_ref_write(&new_head, AGENT_KEY_REASONING,
					reasoning, strlen(reasoning));
		}
		if (write_reasoning &&
		    !strbuf_read_file(&reasoning_buf, write_reasoning, 0)) {
			agent_ref_write(&new_head, AGENT_KEY_REASONING,
					reasoning_buf.buf,
					reasoning_buf.len);
		}
		if (plan) {
			agent_ref_write(&new_head, AGENT_KEY_PLAN,
					plan, strlen(plan));
		}
		if (write_plan &&
		    !strbuf_read_file(&plan_buf, write_plan, 0)) {
			agent_ref_write(&new_head, AGENT_KEY_PLAN,
					plan_buf.buf, plan_buf.len);
		}
		if (diff_summary) {
			agent_ref_write(&new_head, AGENT_KEY_DIFF_SUMMARY,
					diff_summary, strlen(diff_summary));
		}
		if (context) {
			agent_ref_write(&new_head, AGENT_KEY_CONTEXT,
					context, strlen(context));
		}
		if (write_context &&
		    !strbuf_read_file(&context_buf, write_context, 0)) {
			agent_ref_write(&new_head, AGENT_KEY_CONTEXT,
					context_buf.buf,
					context_buf.len);
		}

		if (write_semantic_diff) {
			/* For a first pass, we generate a simple semantic
			 * diff from the diff against HEAD~1.
			 */
			/* Not fully implemented in this prototype. */
		}

		/* Log to session if requested. */
		if (session_id)
			agent_session_log_commit(session_id, &new_head);
	}

	strbuf_release(&reasoning_buf);
	strbuf_release(&plan_buf);
	strbuf_release(&context_buf);
	return 0;
}
