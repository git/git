#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "agent.h"
#include "strbuf.h"
#include "notes.h"
#include "object-file.h"
#include "trailer.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "refs.h"
#include "repository.h"
#include "diff.h"
#include "diffcore.h"
#include "json-writer.h"
#include "config.h"
#include "run-command.h"
#include "setup.h"
#include "version.h"
#include "commit.h"
#include "revision.h"
#include "tree-walk.h"
#include "pretty.h"
#include "path.h"
#include "quote.h"
#include "odb.h"
#include "mailmap.h"

/*
 * Agent ref store implementation.
 *
 * We reuse the notes machinery keyed under refs/agent/commits.
 * Each commit's note is a composite blob containing zero or more
 * annotations separated by a simple header:
 *
 *   annotation: <key>\n
 *   <len>\n
 *   <len bytes of content>\n
 *
 * A custom combine_notes function merges blobs by key.
 */

static struct notes_tree agent_commits_tree;
static struct notes_tree agent_sessions_tree;
static int agent_trees_initialized;

static void agent_ensure_init(void)
{
	if (!agent_trees_initialized) {
		memset(&agent_commits_tree, 0, sizeof(agent_commits_tree));
		memset(&agent_sessions_tree, 0, sizeof(agent_sessions_tree));
		agent_trees_initialized = 1;
	}
}

static struct notes_tree *agent_sessions_tree_get(void)
{
	agent_ensure_init();
	if (!agent_sessions_tree.initialized) {
		init_notes(&agent_sessions_tree, AGENT_REFS_SESSIONS,
			combine_notes_concatenate, NOTES_INIT_WRITABLE);
	}
	return &agent_sessions_tree;
}

/*
 * Composite blob format helpers.
 */

struct agent_annotation {
	struct agent_annotation *next;
	char *key;
	char *data;
	size_t len;
};

static void free_annotations(struct agent_annotation *head)
{
	while (head) {
		struct agent_annotation *next = head->next;
		free(head->key);
		free(head->data);
		free(head);
		head = next;
	}
}

static struct agent_annotation *parse_composite_blob(const char *blob,
							     size_t len)
{
	struct agent_annotation *head = NULL, *tail = NULL;
	const char *p = blob;
	const char *end = blob + len;

	while (p < end) {
		const char *nl;
		struct agent_annotation *ann;
		char *key = NULL;
		char *data = NULL;
		size_t data_len = 0;

		/* Expect "annotation: <key>\n" */
		if (end - p < 12 || strncmp(p, "annotation: ", 12))
			break;
		p += 12;
		nl = memchr(p, '\n', end - p);
		if (!nl)
			break;
		key = xmemdupz(p, nl - p);
		p = nl + 1;

		/* Expect "<len>\n" */
		nl = memchr(p, '\n', end - p);
		if (!nl)
			break;
		{
			char *num_end;
			char *num = xmemdupz(p, nl - p);
			data_len = strtoul(num, &num_end, 10);
			if (num == num_end || (*num_end && !isspace(*num_end))) {
				free(num);
				break;
			}
			free(num);
		}
		p = nl + 1;
		if (p + data_len > end)
			break;
		data = xmemdupz(p, data_len);
		p += data_len;
		if (p < end && *p == '\n')
			p++;

		ann = xmalloc(sizeof(*ann));
		ann->key = key;
		ann->data = data;
		ann->len = data_len;
		ann->next = NULL;

		if (!head)
			head = tail = ann;
		else
			tail = tail->next = ann;
	}

	return head;
}

static void write_composite_blob(struct strbuf *out,
				 struct agent_annotation *head)
{
	struct agent_annotation *ann;
	strbuf_reset(out);
	for (ann = head; ann; ann = ann->next) {
		strbuf_addf(out, "annotation: %s\n", ann->key);
		strbuf_addf(out, "%zu\n", ann->len);
		strbuf_add(out, ann->data, ann->len);
		strbuf_addch(out, '\n');
	}
}

static int agent_combine_notes(struct object_id *cur_oid,
			       const struct object_id *new_oid)
{
	char *cur_data = NULL, *new_data = NULL;
	enum object_type cur_type, new_type;
	unsigned long cur_size = 0, new_size = 0;
	struct agent_annotation *merged = NULL, *cur_list, *new_list;
	struct strbuf out = STRBUF_INIT;
	struct object_id merged_oid;
	int ret = 0;

	if (!is_null_oid(cur_oid)) {
	cur_data = odb_read_object(the_repository->objects, cur_oid,
							&cur_type, &cur_size);
	if (!cur_data)
		return 0;
	new_data = odb_read_object(the_repository->objects, new_oid,
							&new_type, &new_size);
	if (!new_data) {
		free(cur_data);
		return 0;
	}
	}
	new_data = odb_read_object(the_repository->objects, new_oid,
							&new_type, &new_size);
	if (!new_data || new_type != OBJ_BLOB) {
		free(new_data);
		new_data = NULL;
	}

	cur_list = cur_data ? parse_composite_blob(cur_data, cur_size) : NULL;
	new_list = new_data ? parse_composite_blob(new_data, new_size) : NULL;

	/* Start with current annotations. */
	if (cur_list)
		merged = cur_list;

	/* Overlay new annotations (by key). */
	if (new_list) {
		struct agent_annotation *n;
		for (n = new_list; n; n = n->next) {
			struct agent_annotation *m;
			int replaced = 0;
			for (m = merged; m; m = m->next) {
				if (!strcmp(m->key, n->key)) {
					free(m->data);
					m->data = xmemdupz(n->data, n->len);
					m->len = n->len;
					replaced = 1;
					break;
				}
			}
			if (!replaced) {
				struct agent_annotation *copy = xmalloc(sizeof(*copy));
				copy->key = xstrdup(n->key);
				copy->data = xmemdupz(n->data, n->len);
				copy->len = n->len;
				copy->next = merged;
				merged = copy;
			}
		}
	}

	write_composite_blob(&out, merged);

	ret = odb_write_object(the_repository->objects, out.buf, out.len, OBJ_BLOB, &merged_oid);
	if (!ret)
		oidcpy(cur_oid, &merged_oid);

	free_annotations(new_list);
	/* cur_list is now part of merged; free all. */
	free_annotations(merged);
	free(cur_data);
	free(new_data);
	strbuf_release(&out);
	return ret;
}

/*
 * Re-init with our custom combiner.
 */
static struct notes_tree *agent_commits_tree_get_merged(void)
{
	agent_ensure_init();
	if (!agent_commits_tree.initialized) {
		init_notes(&agent_commits_tree, AGENT_REFS_COMMITS,
			agent_combine_notes, NOTES_INIT_WRITABLE);
	}
	return &agent_commits_tree;
}

int agent_ref_write(const struct object_id *commit_oid,
		    const char *key,
		    const char *blob,
		    size_t len)
{
	struct notes_tree *t = agent_commits_tree_get_merged();
	struct strbuf composite = STRBUF_INIT;
	struct object_id blob_oid;
	int ret;

	if (!key || !*key)
		return error(_("agent_ref_write: missing key"));

	strbuf_addf(&composite, "annotation: %s\n", key);
	strbuf_addf(&composite, "%zu\n", len);
	strbuf_add(&composite, blob, len);
	strbuf_addch(&composite, '\n');

	ret = odb_write_object(the_repository->objects, composite.buf, composite.len, OBJ_BLOB,
				&blob_oid);
	if (!ret)
		ret = add_note(t, commit_oid, &blob_oid,
			       agent_combine_notes);
	if (!ret) {
		struct object_id result_oid;
		ret = write_notes_tree(t, &result_oid);
		if (!ret)
			ret = refs_update_ref(get_main_ref_store(the_repository), "agent annotation",
					AGENT_REFS_COMMITS,
					&result_oid,
					NULL,
					REF_NO_DEREF,
					UPDATE_REFS_MSG_ON_ERR);
	}
	strbuf_release(&composite);
	return ret;
}

int agent_ref_read(const struct object_id *commit_oid,
		   const char *key,
		   struct strbuf *buf)
{
	struct object_id blob_oid;
	char *data = NULL;
	enum object_type type;
	unsigned long size = 0;
	struct agent_annotation *list, *ann;
	int ret = -1;
	struct strbuf path = STRBUF_INIT;

	strbuf_reset(buf);

	/* The notes tree is flat (full SHA as filename), so we can
	 * look up the blob directly via <ref>:<hex> syntax.
	 */
	strbuf_addf(&path, "%s:%s", AGENT_REFS_COMMITS,
		    oid_to_hex(commit_oid));
	if (repo_get_oid(the_repository, path.buf, &blob_oid))
		goto done;

	data = odb_read_object(the_repository->objects, &blob_oid,
				   &type, &size);
	if (!data || type != OBJ_BLOB)
		goto done;

	list = parse_composite_blob(data, size);
	for (ann = list; ann; ann = ann->next) {
		if (!strcmp(ann->key, key)) {
			strbuf_add(buf, ann->data, ann->len);
			ret = 0;
			break;
		}
	}
	free_annotations(list);

done:
	free(data);
	strbuf_release(&path);
	return ret;
}

struct agent_list_cb_data {
	agent_ref_list_fn fn;
	void *cb_data;
	int ret;
};

static int agent_list_each_note(const struct object_id *object_oid UNUSED,
				const struct object_id *note_oid,
				char *path UNUSED,
				void *cb_data)
{
	struct agent_list_cb_data *d = cb_data;
	char *data;
	enum object_type type;
	unsigned long size;
	struct agent_annotation *list, *ann;

	data = odb_read_object(the_repository->objects, note_oid, &type,
							&size);
	if (!data || type != OBJ_BLOB)
		return 0;

	list = parse_composite_blob(data, size);
	for (ann = list; ann; ann = ann->next) {
		int fn_ret;
		if (!d->fn)
			continue;
		fn_ret = d->fn(ann->key, ann->data, ann->len,
			       d->cb_data);
		if (fn_ret) {
			d->ret = fn_ret;
			break;
		}
	}
	free_annotations(list);
	free(data);
	return d->ret;
}

int agent_ref_list(const struct object_id *commit_oid,
		   agent_ref_list_fn fn,
		   void *cb_data)
{
	struct notes_tree *t = agent_commits_tree_get_merged();
	struct agent_list_cb_data d = { fn, cb_data, 0 };
	int ret;

	if (commit_oid) {
		/* List only annotations for this commit. */
		const struct object_id *note_oid = get_note(t, commit_oid);
		if (note_oid)
			ret = agent_list_each_note(commit_oid, note_oid,
						   NULL, &d);
		else
			ret = 0;
	} else {
		ret = for_each_note(t, 0, agent_list_each_note, &d);
	}
	if (ret < 0)
		return ret;
	return d.ret;
}

/*
 * Agent trailer parsing and validation.
 */

static enum agent_autonomy parse_autonomy(const char *val)
{
	if (!val || !*val)
		return AGENT_AUTONOMY_UNKNOWN;
	if (!strcasecmp(val, "full"))
		return AGENT_AUTONOMY_FULL;
	if (!strcasecmp(val, "supervised"))
		return AGENT_AUTONOMY_SUPERVISED;
	if (!strcasecmp(val, "dry-run"))
		return AGENT_AUTONOMY_DRY_RUN;
	return AGENT_AUTONOMY_UNKNOWN;
}

void agent_parse_trailers(const char *msg, size_t len,
			  struct agent_commit_meta *meta)
{
	struct trailer_iterator iter;
	char *copy = NULL;

	memset(meta, 0, sizeof(*meta));
	meta->agent_confidence = -1.0f;

	if (!msg || !len)
		return;

	if (msg[len - 1] != '\0') {
		copy = xmemdupz(msg, len);
		trailer_iterator_init(&iter, copy);
	} else {
		trailer_iterator_init(&iter, msg);
	}

	while (trailer_iterator_advance(&iter)) {
		const char *key = iter.key.buf;
		const char *val = iter.val.buf;

		if (!strcasecmp(key, AGENT_TRAILER_ID)) {
			meta->agent_id = xstrdup(val);
			meta->has_agent_data = 1;
		} else if (!strcasecmp(key, AGENT_TRAILER_TASK)) {
			meta->agent_task = xstrdup(val);
			meta->has_agent_data = 1;
		} else if (!strcasecmp(key, AGENT_TRAILER_CONFIDENCE)) {
			char *ep;
			meta->agent_confidence = strtof(val, &ep);
			if (ep == val || *ep)
				meta->agent_confidence = -1.0f;
			meta->has_agent_data = 1;
		} else if (!strcasecmp(key, AGENT_TRAILER_INTENT)) {
			meta->agent_intent = xstrdup(val);
			meta->has_agent_data = 1;
		} else if (!strcasecmp(key, AGENT_TRAILER_CTX_HASH)) {
			meta->agent_context_hash = xstrdup(val);
			meta->has_agent_data = 1;
		} else if (!strcasecmp(key, AGENT_TRAILER_PARENT_COMMIT)) {
			meta->agent_parent_commit = xstrdup(val);
			meta->has_agent_data = 1;
		} else if (!strcasecmp(key, AGENT_TRAILER_AUTONOMY)) {
			meta->agent_autonomy = parse_autonomy(val);
			meta->has_agent_data = 1;
		} else if (!strcasecmp(key, AGENT_TRAILER_TOOL_VERSION)) {
			meta->agent_tool_version = xstrdup(val);
			meta->has_agent_data = 1;
		} else if (!strcasecmp(key, AGENT_TRAILER_CHECKPOINT)) {
			meta->is_checkpoint = 1;
			meta->has_agent_data = 1;
		}
	}

	trailer_iterator_release(&iter);
	free(copy);
}

int agent_validate_trailers(const char *msg, size_t len)
{
	struct trailer_iterator iter;
	int warned = 0;
	char *copy = NULL;

	if (!msg || !len)
		return 0;

	if (msg[len - 1] != '\0') {
		copy = xmemdupz(msg, len);
		trailer_iterator_init(&iter, copy);
	} else {
		trailer_iterator_init(&iter, msg);
	}

	while (trailer_iterator_advance(&iter)) {
		const char *key = iter.key.buf;
		const char *val = iter.val.buf;
		float confidence;
		char *ep;

		if (strncasecmp(key, "Agent-", 6))
			continue;

		if (!strcasecmp(key, AGENT_TRAILER_CONFIDENCE)) {
			confidence = strtof(val, &ep);
			if (ep == val || *ep) {
				warning(_("malformed %s: '%s' is not a float"),
					AGENT_TRAILER_CONFIDENCE, val);
				warned = 1;
			} else if (confidence < 0.0f || confidence > 1.0f) {
				warning(_("suspicious %s: %f outside [0,1]"),
					AGENT_TRAILER_CONFIDENCE, confidence);
				warned = 1;
			}
		} else if (!strcasecmp(key, AGENT_TRAILER_AUTONOMY)) {
			if (parse_autonomy(val) == AGENT_AUTONOMY_UNKNOWN &&
			    *val) {
				warning(_("unknown %s value: '%s'"),
					AGENT_TRAILER_AUTONOMY, val);
				warned = 1;
			}
		} else if (!strcasecmp(key, AGENT_TRAILER_CTX_HASH)) {
			if (strlen(val) != 64) {
				warning(_("suspicious %s length "
					"(expected 64 hex chars)"),
					AGENT_TRAILER_CTX_HASH);
				warned = 1;
			}
		}
	}

	trailer_iterator_release(&iter);
	free(copy);
	return warned;
}

void agent_commit_meta_release(struct agent_commit_meta *meta)
{
	if (!meta)
		return;
	free(meta->agent_id);
	free(meta->agent_task);
	free(meta->agent_intent);
	free(meta->agent_context_hash);
	free(meta->agent_parent_commit);
	free(meta->agent_tool_version);
	memset(meta, 0, sizeof(*meta));
}

/*
 * Session management.
 *
 * Sessions are stored under refs/agent/sessions using the notes
 * machinery, keyed by a synthetic object id of the path string
 * <session-id>/<key>.  This allows multiple keys per session.
 */

static int session_build_oid(const char *session_id,
			     const char *key,
			     struct object_id *out)
{
	struct strbuf path = STRBUF_INIT;
	struct object_id blob_oid;
	int ret;

	strbuf_addf(&path, "%s/%s", session_id, key);
	ret = odb_write_object(the_repository->objects, path.buf, path.len, OBJ_BLOB, &blob_oid);
	if (!ret)
		oidcpy(out, &blob_oid);
	strbuf_release(&path);
	return ret;
}

int agent_session_start(const char *task_id, char **session_id)
{
	struct strbuf sid = STRBUF_INIT;
	struct object_id key_oid, empty_oid;
	struct notes_tree *t;
	int ret;

	strbuf_addstr(&sid, "session-");
	strbuf_add_unique_abbrev(&sid, null_oid(the_hash_algo),
					 the_hash_algo->hexsz);
	strbuf_addf(&sid, "-%ld", (long)time(NULL));

	*session_id = strbuf_detach(&sid, NULL);

	/* Write an empty log to establish the session. */
	t = agent_sessions_tree_get();
	ret = odb_write_object(the_repository->objects, "", 0, OBJ_BLOB, &empty_oid);
	if (ret)
		return ret;

	ret = session_build_oid(*session_id, AGENT_SESSION_KEY_LOG,
				&key_oid);
	if (ret)
		return ret;

	ret = add_note(t, &key_oid, &empty_oid, combine_notes_overwrite);
	if (!ret) {
		struct object_id result_oid;
		ret = write_notes_tree(t, &result_oid);
		if (!ret)
			ret = refs_update_ref(get_main_ref_store(the_repository), "agent session start",
					AGENT_REFS_SESSIONS,
					&result_oid,
					NULL,
					REF_NO_DEREF,
					UPDATE_REFS_MSG_ON_ERR);
	}
	return ret;
}

int agent_session_log_commit(const char *session_id,
			     const struct object_id *commit_oid)
{
	struct notes_tree *t = agent_sessions_tree_get();
	struct object_id key_oid, blob_oid;
	struct strbuf data = STRBUF_INIT;
	int ret;

	ret = session_build_oid(session_id, AGENT_SESSION_KEY_COMMITS,
				&key_oid);
	if (ret)
		goto out;

	strbuf_addstr(&data, oid_to_hex(commit_oid));
	strbuf_addch(&data, '\n');
	ret = odb_write_object(the_repository->objects, data.buf, data.len, OBJ_BLOB, &blob_oid);
	if (ret)
		goto out;

	ret = add_note(t, &key_oid, &blob_oid, combine_notes_concatenate);
	if (!ret) {
		struct object_id result_oid;
		ret = write_notes_tree(t, &result_oid);
		if (!ret)
			ret = refs_update_ref(get_main_ref_store(the_repository), "agent session log",
					AGENT_REFS_SESSIONS,
					&result_oid,
					NULL,
					REF_NO_DEREF,
					UPDATE_REFS_MSG_ON_ERR);
	}
out:
	strbuf_release(&data);
	return ret;
}

int agent_session_transcribe(const char *session_id,
			     const char *line,
			     size_t len)
{
	struct notes_tree *t = agent_sessions_tree_get();
	struct object_id key_oid, blob_oid;
	struct strbuf data = STRBUF_INIT;
	int ret;

	ret = session_build_oid(session_id, AGENT_SESSION_KEY_LOG,
				&key_oid);
	if (ret)
		goto out;

	strbuf_add(&data, line, len);
	if (!len || line[len - 1] != '\n')
		strbuf_addch(&data, '\n');
	ret = odb_write_object(the_repository->objects, data.buf, data.len, OBJ_BLOB, &blob_oid);
	if (ret)
		goto out;

	ret = add_note(t, &key_oid, &blob_oid, combine_notes_concatenate);
	if (!ret) {
		struct object_id result_oid;
		ret = write_notes_tree(t, &result_oid);
		if (!ret)
			ret = refs_update_ref(get_main_ref_store(the_repository), "agent session transcribe",
					AGENT_REFS_SESSIONS,
					&result_oid,
					NULL,
					REF_NO_DEREF,
					UPDATE_REFS_MSG_ON_ERR);
	}
out:
	strbuf_release(&data);
	return ret;
}

int agent_session_end(const char *session_id)
{
	struct object_id key_oid;
	int ret;

	ret = session_build_oid(session_id, AGENT_SESSION_KEY_LOG,
				&key_oid);
	return ret;
}

int agent_session_read_log(const char *session_id, struct strbuf *buf)
{
	struct notes_tree *t = agent_sessions_tree_get();
	struct object_id key_oid;
	const struct object_id *note_oid;
	char *data;
	enum object_type type;
	unsigned long size;
	int ret = -1;

	strbuf_reset(buf);
	ret = session_build_oid(session_id, AGENT_SESSION_KEY_LOG,
				&key_oid);
	if (ret)
		return ret;

	note_oid = get_note(t, &key_oid);
	if (!note_oid)
		return -1;

	data = odb_read_object(the_repository->objects, note_oid,
					     &type, &size);
	if (data && type == OBJ_BLOB) {
		strbuf_add(buf, data, size);
		ret = 0;
	}
	free(data);
	return ret;
}

int agent_session_get_commits(const char *session_id, struct strbuf *buf)
{
	struct notes_tree *t = agent_sessions_tree_get();
	struct object_id key_oid;
	const struct object_id *note_oid;
	char *data;
	enum object_type type;
	unsigned long size;
	int ret = -1;

	strbuf_reset(buf);
	ret = session_build_oid(session_id, AGENT_SESSION_KEY_COMMITS,
				&key_oid);
	if (ret)
		return ret;

	note_oid = get_note(t, &key_oid);
	if (!note_oid)
		return -1;

	data = odb_read_object(the_repository->objects, note_oid,
					     &type, &size);
	if (data && type == OBJ_BLOB) {
		strbuf_add(buf, data, size);
		ret = 0;
	}
	free(data);
	return ret;
}

/*
 * Semantic diff.
 */

int agent_generate_semantic_diff(struct diff_queue_struct *dq,
				 const char *base,
				 const char *head,
				 struct strbuf *out)
{
	struct json_writer jw = JSON_WRITER_INIT;
	int i;
	size_t token_estimate = 0;

	jw_object_begin(&jw, 0);
	jw_object_string(&jw, "schema", "1");
	jw_object_string(&jw, "base", base ? base : "");
	jw_object_string(&jw, "head", head ? head : "");

	jw_object_inline_begin_array(&jw, "changes");
	for (i = 0; i < dq->nr; i++) {
		struct diff_filepair *p = dq->queue[i];
		const char *change_type;

		switch (p->status) {
		case DIFF_STATUS_ADDED:
			change_type = "add-file";
			break;
		case DIFF_STATUS_DELETED:
			change_type = "delete-file";
			break;
		case DIFF_STATUS_RENAMED:
			change_type = "rename";
			break;
		case DIFF_STATUS_COPIED:
			change_type = "copy";
			break;
		case DIFF_STATUS_MODIFIED:
			change_type = "modify";
			break;
		case DIFF_STATUS_TYPE_CHANGED:
			change_type = "type-change";
			break;
		case DIFF_STATUS_UNMERGED:
			change_type = "unmerged";
			break;
		default:
			change_type = "unknown";
			break;
		}

		jw_object_begin(&jw, 0);
		jw_object_string(&jw, "type", change_type);
		if (p->one && p->one->path)
			jw_object_string(&jw, "from", p->one->path);
		if (p->two && p->two->path)
			jw_object_string(&jw, "to", p->two->path);
		if (p->one && p->one->size >= 0)
			jw_object_intmax(&jw, "old_size", p->one->size);
		if (p->two && p->two->size >= 0)
			jw_object_intmax(&jw, "new_size", p->two->size);
		if (p->score)
			jw_object_intmax(&jw, "score", p->score);
		jw_end(&jw);

		if (p->two && p->two->size > 0)
			token_estimate += p->two->size / 4;
		else if (p->one && p->one->size > 0)
			token_estimate += p->one->size / 4;
		else
			token_estimate += 10;
	}
	jw_end(&jw);

	jw_object_intmax(&jw, "token_estimate", token_estimate);
	jw_end(&jw);

	strbuf_addstr(out, jw.json.buf);
	jw_release(&jw);
	return 0;
}

int agent_write_semantic_diff(const struct object_id *commit_oid,
			      struct diff_queue_struct *dq,
			      const char *base,
			      const char *head)
{
	struct strbuf json = STRBUF_INIT;
	int ret;

	ret = agent_generate_semantic_diff(dq, base, head, &json);
	if (!ret)
		ret = agent_ref_write(commit_oid, AGENT_KEY_DIFF_SUMMARY,
				    json.buf, json.len);
	strbuf_release(&json);
	return ret;
}

/*
 * Token estimation.
 */

size_t agent_estimate_tokens(const char *str, size_t len)
{
	if (!str)
		return 0;
	if (!len)
		len = strlen(str);
	return len / 4 + 1;
}

/*
 * Repository orientation.
 */

static const char *agent_basename(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

void agent_orient_repo(struct strbuf *out, int max_tokens)
{
	struct strbuf buf = STRBUF_INIT;
	size_t budget;
	int i;
	const char *languages[] = {
		"c", "py", "rs", "go", "js",
		"ts", "java", "rb", "pl", NULL
	};
	const char *primary_lang = "C";
	struct rev_info rev;
	struct commit *commit;
	const char *wt;

	budget = (size_t)max_tokens * 4;

	strbuf_addstr(out, "REPO: ");
	wt = repo_get_work_tree(the_repository);
	if (wt) {
		strbuf_addstr(out, agent_basename(wt));
	} else {
		strbuf_addstr(out, "unknown");
	}
	strbuf_addch(out, '\n');

	/* Try to read a one-line description. */
	if (!strbuf_read_file(&buf, "AGENTS.md", 0) && buf.len > 0) {
		const char *nl = strchr(buf.buf, '\n');
		if (nl)
			strbuf_setlen(&buf, nl - buf.buf);
		strbuf_addf(out, "DESCRIPTION: %s\n", buf.buf);
	} else if (!strbuf_read_file(&buf, "README.md", 0) && buf.len > 0) {
		const char *nl = strchr(buf.buf, '\n');
		if (nl)
			strbuf_setlen(&buf, nl - buf.buf);
		strbuf_addf(out, "DESCRIPTION: %s\n", buf.buf);
	} else {
		strbuf_addstr(out, "DESCRIPTION: (none)\n");
	}
	strbuf_release(&buf);

	/* Naive language detection. */
	for (i = 0; languages[i]; i++) {
		struct strbuf glob = STRBUF_INIT;
		strbuf_addf(&glob, "*.%s", languages[i]);
		if (!access(glob.buf, F_OK)) {
			if (!strcmp(languages[i], "c"))
				primary_lang = "C";
			else if (!strcmp(languages[i], "py"))
				primary_lang = "Python";
			else if (!strcmp(languages[i], "rs"))
				primary_lang = "Rust";
			else if (!strcmp(languages[i], "go"))
				primary_lang = "Go";
			else if (!strcmp(languages[i], "js"))
				primary_lang = "JavaScript";
			else if (!strcmp(languages[i], "ts"))
				primary_lang = "TypeScript";
			else if (!strcmp(languages[i], "java"))
				primary_lang = "Java";
			else if (!strcmp(languages[i], "rb"))
				primary_lang = "Ruby";
			else if (!strcmp(languages[i], "pl"))
				primary_lang = "Perl";
			strbuf_release(&glob);
			break;
		}
		strbuf_release(&glob);
	}
	strbuf_addf(out, "PRIMARY-LANGUAGE: %s\n", primary_lang);

	/* Top authors via git shortlog. */
	strbuf_addstr(out, "TOP-AUTHORS:\n");
{
	struct child_process cp = CHILD_PROCESS_INIT;
	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "shortlog", "-sn", "HEAD~20..HEAD", NULL);
	cp.no_stdin = 1;
	cp.no_stderr = 1;
	if (!capture_command(&cp, &buf, 1024)) {
		char *p = buf.buf;
		int lines = 0;
		while (p && lines < 5) {
			char *nl = strchr(p, '\n');
			if (nl)
				*nl = '\0';
			if (*p) {
				strbuf_addf(out, "  %s\n", p);
				lines++;
			}
			if (!nl)
				break;
			p = nl + 1;
		}
	}
	strbuf_release(&buf);
}

	/* Recent activity: last 3 commit one-liners. */
	strbuf_addstr(out, "RECENT-ACTIVITY:\n");
	repo_init_revisions(the_repository, &rev, NULL);
	setup_revisions(0, NULL, &rev, NULL);
	rev.max_count = 10;
	prepare_revision_walk(&rev);
	i = 0;
	while ((commit = get_revision(&rev)) && i < 3) {
		struct strbuf subject = STRBUF_INIT;
		repo_format_commit_message(the_repository, commit, "%h %s", &subject, NULL);
		strbuf_addf(out, "  %s\n", subject.buf);
		strbuf_release(&subject);
		i++;
	}
	release_revisions(&rev);

	/* Open branches. */
	strbuf_addstr(out, "OPEN-BRANCHES:\n");
	{
	struct child_process cp = CHILD_PROCESS_INIT;
	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "branch", "--format=%(refname:short)", NULL);
	cp.no_stdin = 1;
	cp.no_stderr = 1;
	if (!capture_command(&cp, &buf, 512)) {
		char *p = buf.buf;
		int lines = 0;
		while (p && lines < 5) {
			char *nl = strchr(p, '\n');
			if (nl)
				*nl = '\0';
			if (*p) {
				strbuf_addf(out, "  %s\n", p);
				lines++;
			}
			if (!nl)
				break;
			p = nl + 1;
		}
	}
	strbuf_release(&buf);
	}

	strbuf_addstr(out, "AGENT-COMMITS: yes\n");
	strbuf_addstr(out, "LAST-AGENT-SESSION: (not tracked)\n");

	if (out->len > budget) {
		strbuf_setlen(out, budget);
		strbuf_addstr(out, "\n[TRUNCATED]\n");
	}
}
