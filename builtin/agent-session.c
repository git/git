/*
 * Builtin "git agent-session"
 *
 * Manages agent sessions: start, end, log.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "agent.h"
#include "parse-options.h"
#include "strbuf.h"
#include "run-command.h"

static const char * const builtin_agent_session_usage[] = {
	N_("git agent-session start [--task=<task-id>]"),
	N_("git agent-session end"),
	N_("git agent-session log [--max-tokens=<n>]"),
	NULL
};

enum session_action {
	ACTION_START,
	ACTION_END,
	ACTION_LOG,
	ACTION_NONE
};

int cmd_agent_session(int argc,
		      const char **argv,
		      const char *prefix,
		      struct repository *repo UNUSED)
{
	enum session_action action = ACTION_NONE;
	const char *task_id = NULL;
	const char *session_id = NULL;
	int max_tokens = 2000;
	struct strbuf out = STRBUF_INIT;
	struct option options[] = {
		OPT_CMDMODE(0, "start", &action,
			    N_("start a new session"), ACTION_START),
		OPT_CMDMODE(0, "end", &action,
			    N_("finalize a session"), ACTION_END),
		OPT_CMDMODE(0, "log", &action,
			    N_("show session transcript"), ACTION_LOG),
		OPT_STRING(0, "task", &task_id, N_("task"),
			   N_("task or ticket identifier")),
		OPT_STRING(0, "session-id", &session_id, N_("session"),
			   N_("session identifier")),
		OPT_INTEGER(0, "max-tokens", &max_tokens,
			    N_("maximum tokens to output")),
		OPT_END()
	};
	int ret = 0;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_agent_session_usage,
			     PARSE_OPT_KEEP_ARGV0);

	if (action == ACTION_NONE) {
		usage_msg_opt(_("no action specified"),
			      builtin_agent_session_usage, options);
	}

	if (action == ACTION_START) {
		char *sid = NULL;
		ret = agent_session_start(task_id, &sid);
		if (ret)
			die(_("failed to start session"));
		printf("%s\n", sid);
		free(sid);
	} else if (action == ACTION_END) {
		if (!session_id)
			die(_("--session-id required for end"));
		ret = agent_session_end(session_id);
		if (ret)
			die(_("failed to end session '%s'"), session_id);
	} else if (action == ACTION_LOG) {
		size_t budget;

		if (!session_id)
			die(_("--session-id required for log"));
		ret = agent_session_read_log(session_id, &out);
		if (ret)
			die(_("no log found for session '%s'"), session_id);

		budget = (size_t)max_tokens * 4;
		if (out.len > budget) {
			strbuf_setlen(&out, budget);
			strbuf_addstr(&out, "\n[TRUNCATED]\n");
		}
		printf("%s", out.buf);
	}

	strbuf_release(&out);
	return ret;
}
