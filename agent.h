#ifndef AGENT_H
#define AGENT_H

#include "git-compat-util.h"
#include "strbuf.h"
#include "hash.h"
#include "notes.h"

/*
 * Agent trailer field names and validation.
 */
#define AGENT_TRAILER_ID		"Agent-Id"
#define AGENT_TRAILER_TASK		"Agent-Task"
#define AGENT_TRAILER_CONFIDENCE	"Agent-Confidence"
#define AGENT_TRAILER_INTENT		"Agent-Intent"
#define AGENT_TRAILER_CTX_HASH		"Agent-Context-Hash"
#define AGENT_TRAILER_PARENT_COMMIT	"Agent-Parent-Commit"
#define AGENT_TRAILER_AUTONOMY		"Agent-Autonomy"
#define AGENT_TRAILER_TOOL_VERSION	"Agent-Tool-Version"
#define AGENT_TRAILER_CHECKPOINT	"Agent-Checkpoint"

enum agent_autonomy {
	AGENT_AUTONOMY_FULL,
	AGENT_AUTONOMY_SUPERVISED,
	AGENT_AUTONOMY_DRY_RUN,
	AGENT_AUTONOMY_UNKNOWN
};

/*
 * Agent metadata parsed from commit trailers.
 */
struct agent_commit_meta {
	char *agent_id;
	char *agent_task;
	float agent_confidence;
	char *agent_intent;
	char *agent_context_hash;
	char *agent_parent_commit;
	enum agent_autonomy agent_autonomy;
	char *agent_tool_version;
	int is_checkpoint;
	/* internal: was this actually parsed from trailers? */
	int has_agent_data;
};

/*
 * Agent ref namespace helpers.
 */
#define AGENT_REFS_COMMITS	"refs/agent/commits"
#define AGENT_REFS_SESSIONS	"refs/agent/sessions"

/*
 * Agent annotation keys stored under refs/agent/commits/<sha>/
 */
#define AGENT_KEY_REASONING	"reasoning"
#define AGENT_KEY_PLAN		"plan"
#define AGENT_KEY_DIFF_SUMMARY	"diff-summary"
#define AGENT_KEY_CONTEXT	"context"
#define AGENT_KEY_SESSION	"session"

/*
 * Session keys stored under refs/agent/sessions/<session-id>/
 */
#define AGENT_SESSION_KEY_LOG		"log"
#define AGENT_SESSION_KEY_COMMITS	"commits"

/*
 * Parse agent trailers from a commit message.
 * Populates meta; caller must call agent_commit_meta_release() when done.
 */
void agent_parse_trailers(const char *msg, size_t len, struct agent_commit_meta *meta);

/*
 * Validate agent trailers in a commit message.
 * Emits warnings (not errors) on malformed agent trailers.
 * Returns 0 if all agent trailers are well-formed or absent,
 * 1 if warnings were emitted.
 */
int agent_validate_trailers(const char *msg, size_t len);

/*
 * Release memory held by agent_commit_meta.
 */
void agent_commit_meta_release(struct agent_commit_meta *meta);

/*
 * Agent ref store: write/read/list annotations under refs/agent/
 * Built on top of the notes infrastructure.
 */

/*
 * Write a blob as an annotation for a given commit SHA.
 * The key becomes a "directory" in the notes tree, e.g.
 * refs/agent/commits/<sha-short>/<key>.
 *
 * Returns 0 on success, non-zero on failure.
 */
int agent_ref_write(const struct object_id *commit_oid,
		    const char *key,
		    const char *blob,
		    size_t len);

/*
 * Read an annotation blob for a given commit SHA into buf.
 * Returns 0 if found, non-zero if not found or error.
 */
int agent_ref_read(const struct object_id *commit_oid,
		   const char *key,
		   struct strbuf *buf);

/*
 * Callback for agent_ref_list.
 * Receives the key name and the blob contents.
 * Return 0 to continue, non-zero to stop.
 */
typedef int (*agent_ref_list_fn)(const char *key,
				 const char *blob,
				 size_t blob_len,
				 void *cb_data);

/*
 * Enumerate all annotation keys for a commit.
 * Returns 0 on success (even if no keys found).
 */
int agent_ref_list(const struct object_id *commit_oid,
		   agent_ref_list_fn fn,
		   void *cb_data);

/*
 * Session management.
 */

/*
 * Start a new agent session.  Writes the initial empty session ref
 * and returns the session ID in session_id.
 *
 * Returns 0 on success, non-zero on error.
 */
int agent_session_start(const char *task_id, char **session_id);

/*
 * Log a commit as part of the current session.
 * Stores the commit SHA in refs/agent/sessions/<session_id>/commits.
 *
 * Returns 0 on success.
 */
int agent_session_log_commit(const char *session_id,
			     const struct object_id *commit_oid);

/*
 * Append a line to the session transcript log.
 *
 * Returns 0 on success.
 */
int agent_session_transcribe(const char *session_id,
			     const char *line,
			     size_t len);

/*
 * End a session.  Currently a no-op beyond validation.
 */
int agent_session_end(const char *session_id);

/*
 * Read the full session transcript into buf.
 * Returns 0 on success, non-zero if not found.
 */
int agent_session_read_log(const char *session_id,
			   struct strbuf *buf);

/*
 * Retrieve the list of commits associated with a session.
 * Each commit OID is written as a line into buf.
 * Returns 0 on success.
 */
int agent_session_get_commits(const char *session_id,
			      struct strbuf *buf);

/*
 * Semantic diff helpers.
 */

/*
 * Generate a semantic diff JSON summary from a diff_queue_struct.
 * Writes JSON into out.
 * Returns 0 on success.
 */
struct diff_queue_struct;
int agent_generate_semantic_diff(struct diff_queue_struct *dq,
				 const char *base,
				 const char *head,
				 struct strbuf *out);

/*
 * Write a semantic diff blob for a commit.
 * Convenience wrapper: generates JSON and stores via agent_ref_write.
 */
int agent_write_semantic_diff(const struct object_id *commit_oid,
			      struct diff_queue_struct *dq,
			      const char *base,
			      const char *head);

/*
 * Orientation helpers.
 */

/*
 * Produce a structured orientation summary for the current repository.
 * Writes the orientation output (see git agent-orient docs) into out.
 * Respects max_tokens by truncating if necessary.
 */
void agent_orient_repo(struct strbuf *out, int max_tokens);

/*
 * Token estimation helpers.
 */

/*
 * Roughly estimate the number of tokens in a string.
 * Uses a simple heuristic (~4 chars per token).
 */
size_t agent_estimate_tokens(const char *str, size_t len);

#endif /* AGENT_H */
