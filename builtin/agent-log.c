/*
 * Builtin "git agent-log"
 *
 * Token-budget-aware history for agents.
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
#include "hex.h"
#include "strbuf.h"
#include "trailer.h"

static const char * const builtin_agent_log_usage[] = {
	N_("git agent-log [--max-tokens=<n>] [--task=<task>] [--agent=<agent-id>] [--session=<session-id>] [--format=<format>]"),
	NULL
};

enum output_format {
	FORMAT_JSON,
	FORMAT_TSV
};

int cmd_agent_log(int argc,
		      const char **argv,
		      const char *prefix,
		      struct repository *repo UNUSED)
{
	int max_tokens = 2000;
	const char *task_filter = NULL;
	const char *agent_filter = NULL;
	const char *session_filter = NULL;
	int format = FORMAT_TSV;
	int read_reasoning = 0;
	int read_plan = 0;
	int read_context = 0;
	int read_diff_summary = 0;
	struct option options[] = {
		OPT_INTEGER(0, "max-tokens", &max_tokens,
			    N_("maximum approximate tokens to output")),
		OPT_STRING(0, "task", &task_filter, N_("task"),
			   N_("filter by Agent-Task")),
		OPT_STRING(0, "agent", &agent_filter, N_("agent-id"),
			   N_("filter by Agent-Id")),
		OPT_STRING(0, "session", &session_filter, N_("session"),
			   N_("filter by session id")),
		OPT_BOOL(0, "reasoning", &read_reasoning,
			 N_("output reasoning annotation")),
		OPT_BOOL(0, "plan", &read_plan,
			 N_("output plan annotation")),
		OPT_BOOL(0, "context", &read_context,
			 N_("output context annotation")),
		OPT_BOOL(0, "diff-summary", &read_diff_summary,
			 N_("output diff-summary annotation")),
		{
			.type = OPTION_SET_INT,
			.long_name = "json",
			.value = &format,
			.defval = FORMAT_JSON,
			.help = N_("output as JSON"),
			.flags = PARSE_OPT_NOARG | PARSE_OPT_NONEG,
		},
		OPT_END()
	};
	struct rev_info rev;
	struct commit *commit;
	size_t budget = (size_t)max_tokens * 4;
	struct strbuf out = STRBUF_INIT;
	int skipped = 0;
	int first_json = 1;
	int ret = 0;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_agent_log_usage,
			     PARSE_OPT_KEEP_ARGV0 |
			     PARSE_OPT_KEEP_UNKNOWN_OPT |
			     PARSE_OPT_KEEP_DASHDASH);

	repo_init_revisions(the_repository, &rev, prefix);
	setup_revisions(argc, argv, &rev, NULL);
	prepare_revision_walk(&rev);

	if (format == FORMAT_JSON)
		strbuf_addstr(&out, "[\n");

	while ((commit = get_revision(&rev))) {
		const char *msg;
		struct agent_commit_meta meta;
		struct strbuf subject = STRBUF_INIT;
		struct strbuf line = STRBUF_INIT;

		msg = repo_get_commit_buffer(the_repository, commit, NULL);
		agent_parse_trailers(msg, strlen(msg), &meta);

		/* Apply filters. */
		if (task_filter && (!meta.agent_task ||
				    strcmp(meta.agent_task, task_filter))) {
			repo_unuse_commit_buffer(the_repository, commit, msg);
			agent_commit_meta_release(&meta);
			continue;
		}
		if (agent_filter && (!meta.agent_id ||
				     strcmp(meta.agent_id, agent_filter))) {
			repo_unuse_commit_buffer(the_repository, commit, msg);
			agent_commit_meta_release(&meta);
			continue;
		}

			{
			struct pretty_print_context pp = {0};
			repo_format_commit_message(the_repository, commit, "%h",
						  &subject, &pp);
		}

		if (format == FORMAT_TSV) {
			strbuf_addf(&line, "%s\t", subject.buf);
			strbuf_addstr(&line,
				      meta.agent_id ? meta.agent_id : "(none)");
			strbuf_addch(&line, '\t');
			strbuf_addstr(&line,
				      meta.agent_task ? meta.agent_task :
				      "(none)");
			strbuf_addch(&line, '\t');
			strbuf_addf(&line, "%f",
				    meta.agent_confidence >= 0.0f ?
				    meta.agent_confidence : 0.0f);
			strbuf_addch(&line, '\t');
			strbuf_addstr(&line,
				      meta.agent_intent ? meta.agent_intent :
				      "(none)");
			strbuf_addch(&line, '\t');
			strbuf_addstr(&line,
				      meta.agent_autonomy ==
				      AGENT_AUTONOMY_FULL ? "full" :
				      meta.agent_autonomy ==
				      AGENT_AUTONOMY_SUPERVISED ?
				      "supervised" :
				      meta.agent_autonomy ==
				      AGENT_AUTONOMY_DRY_RUN ? "dry-run" :
				      "(none)");
			strbuf_addch(&line, '\n');
		} else {
			struct json_writer jw = JSON_WRITER_INIT;
			jw_object_begin(&jw, 0);
			jw_object_string(&jw, "sha",
					 subject.buf);
			jw_object_string(&jw, "agent_id",
					 meta.agent_id ?
					 meta.agent_id : "");
			jw_object_string(&jw, "task",
					 meta.agent_task ?
					 meta.agent_task : "");
			{
				struct strbuf cf = STRBUF_INIT;
				strbuf_addf(&cf, "%.2f", meta.agent_confidence);
				jw_object_string(&jw, "confidence",
						 cf.buf);
				strbuf_release(&cf);
			}
			jw_object_string(&jw, "intent",
					 meta.agent_intent ?
					 meta.agent_intent : "");
			jw_object_string(&jw, "autonomy",
					 meta.agent_autonomy ==
					 AGENT_AUTONOMY_FULL ?
					 "full" :
					 meta.agent_autonomy ==
					 AGENT_AUTONOMY_SUPERVISED ?
					 "supervised" :
					 meta.agent_autonomy ==
					 AGENT_AUTONOMY_DRY_RUN ?
					 "dry-run" : "");
			jw_end(&jw);
			if (!first_json)
				strbuf_addstr(&line, ",\n");
			strbuf_addbuf(&line, &jw.json);
			first_json = 0;
			jw_release(&jw);
		}

		if (out.len + line.len > budget) {
			skipped++;
			strbuf_release(&line);
			strbuf_release(&subject);
			agent_commit_meta_release(&meta);
			repo_unuse_commit_buffer(the_repository, commit, msg);
			continue;
		}

		strbuf_addbuf(&out, &line);

		if (read_reasoning || read_plan || read_context ||
		    read_diff_summary) {
			const char *keys[] = {
				AGENT_KEY_REASONING,
				AGENT_KEY_PLAN,
				AGENT_KEY_CONTEXT,
				AGENT_KEY_DIFF_SUMMARY,
			};
			int flags[] = {
				read_reasoning,
				read_plan,
				read_context,
				read_diff_summary,
			};
			size_t k;
			for (k = 0; k < ARRAY_SIZE(keys); k++) {
				struct strbuf ann = STRBUF_INIT;
				if (!flags[k])
					continue;
				if (!agent_ref_read(&commit->object.oid,
						    keys[k], &ann) &&
				    ann.len) {
					strbuf_addf(&out, "%s:\n%s\n",
						    keys[k], ann.buf);
				}
				strbuf_release(&ann);
			}
		}

		strbuf_release(&line);
		strbuf_release(&subject);
		agent_commit_meta_release(&meta);
		repo_unuse_commit_buffer(the_repository, commit, msg);
	}

	release_revisions(&rev);

	if (format == FORMAT_JSON)
		strbuf_addstr(&out, "\n]\n");

	printf("%s", out.buf);
	if (skipped)
		printf("[TRUNCATED: %d more commits, use --all to expand]\n",
		       skipped);

	strbuf_release(&out);
	return ret;
}
