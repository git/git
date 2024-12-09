#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "walker.h"
#include "repository.h"
#include "object-store-ll.h"
#include "commit.h"
#include "strbuf.h"
#include "tree.h"
#include "tree-walk.h"
#include "tag.h"
#include "blob.h"
#include "refs.h"
#include "progress.h"

static struct object_id current_commit_oid;

void walker_say(struct walker *walker, const char *fmt, ...)
{
	if (walker->get_verbosely) {
		va_list ap;
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
}

static void report_missing(const struct object *obj)
{
	fprintf(stderr, "Cannot obtain needed %s %s\n",
		obj->type ? type_name(obj->type): "object",
		oid_to_hex(&obj->oid));
	if (!is_null_oid(&current_commit_oid))
		fprintf(stderr, "while processing commit %s.\n",
			oid_to_hex(&current_commit_oid));
}

static int process(struct walker *walker, struct object *obj);

static int process_tree(struct walker *walker, struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry entry;

	if (parse_tree(tree))
		return -1;

	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		struct object *obj = NULL;

		/* submodule commits are not stored in the superproject */
		if (S_ISGITLINK(entry.mode))
			continue;
		if (S_ISDIR(entry.mode)) {
			struct tree *tree = lookup_tree(the_repository,
							&entry.oid);
			if (tree)
				obj = &tree->object;
		}
		else {
			struct blob *blob = lookup_blob(the_repository,
							&entry.oid);
			if (blob)
				obj = &blob->object;
		}
		if (!obj || process(walker, obj))
			return -1;
	}
	free_tree_buffer(tree);
	return 0;
}

/* Remember to update object flag allocation in object.h */
#define COMPLETE	(1U << 0)
#define SEEN		(1U << 1)
#define TO_SCAN		(1U << 2)

static struct commit_list *complete = NULL;

static int process_commit(struct walker *walker, struct commit *commit)
{
	struct commit_list *parents;

	if (repo_parse_commit(the_repository, commit))
		return -1;

	while (complete && complete->item->date >= commit->date) {
		pop_most_recent_commit(&complete, COMPLETE);
	}

	if (commit->object.flags & COMPLETE)
		return 0;

	oidcpy(&current_commit_oid, &commit->object.oid);

	walker_say(walker, "walk %s\n", oid_to_hex(&commit->object.oid));

	if (process(walker, &repo_get_commit_tree(the_repository, commit)->object))
		return -1;

	for (parents = commit->parents; parents; parents = parents->next) {
		if (process(walker, &parents->item->object))
			return -1;
	}

	return 0;
}

static int process_tag(struct walker *walker, struct tag *tag)
{
	if (parse_tag(tag))
		return -1;
	return process(walker, tag->tagged);
}

static struct object_list *process_queue = NULL;
static struct object_list **process_queue_end = &process_queue;

static int process_object(struct walker *walker, struct object *obj)
{
	if (obj->type == OBJ_COMMIT) {
		if (process_commit(walker, (struct commit *)obj))
			return -1;
		return 0;
	}
	if (obj->type == OBJ_TREE) {
		if (process_tree(walker, (struct tree *)obj))
			return -1;
		return 0;
	}
	if (obj->type == OBJ_BLOB) {
		return 0;
	}
	if (obj->type == OBJ_TAG) {
		if (process_tag(walker, (struct tag *)obj))
			return -1;
		return 0;
	}
	return error("Unable to determine requirements "
		     "of type %s for %s",
		     type_name(obj->type), oid_to_hex(&obj->oid));
}

static int process(struct walker *walker, struct object *obj)
{
	if (obj->flags & SEEN)
		return 0;
	obj->flags |= SEEN;

	if (repo_has_object_file(the_repository, &obj->oid)) {
		/* We already have it, so we should scan it now. */
		obj->flags |= TO_SCAN;
	}
	else {
		if (obj->flags & COMPLETE)
			return 0;
		walker->prefetch(walker, &obj->oid);
	}

	object_list_insert(obj, process_queue_end);
	process_queue_end = &(*process_queue_end)->next;
	return 0;
}

static int loop(struct walker *walker)
{
	struct object_list *elem;
	struct progress *progress = NULL;
	uint64_t nr = 0;

	if (walker->get_progress)
		progress = start_delayed_progress(_("Fetching objects"), 0);

	while (process_queue) {
		struct object *obj = process_queue->item;
		elem = process_queue;
		process_queue = elem->next;
		free(elem);
		if (!process_queue)
			process_queue_end = &process_queue;

		/* If we are not scanning this object, we placed it in
		 * the queue because we needed to fetch it first.
		 */
		if (! (obj->flags & TO_SCAN)) {
			if (walker->fetch(walker, &obj->oid)) {
				stop_progress(&progress);
				report_missing(obj);
				return -1;
			}
		}
		if (!obj->type)
			parse_object(the_repository, &obj->oid);
		if (process_object(walker, obj)) {
			stop_progress(&progress);
			return -1;
		}
		display_progress(progress, ++nr);
	}
	stop_progress(&progress);
	return 0;
}

static int interpret_target(struct walker *walker, char *target, struct object_id *oid)
{
	if (!get_oid_hex(target, oid))
		return 0;
	if (!check_refname_format(target, 0)) {
		struct ref *ref = alloc_ref(target);
		if (!walker->fetch_ref(walker, ref)) {
			oidcpy(oid, &ref->old_oid);
			free(ref);
			return 0;
		}
		free(ref);
	}
	return -1;
}

static int mark_complete(const char *path UNUSED,
			const char *referent UNUSED,
			 const struct object_id *oid,
			 int flag UNUSED,
			 void *cb_data UNUSED)
{
	struct commit *commit = lookup_commit_reference_gently(the_repository,
							       oid, 1);

	if (commit) {
		commit->object.flags |= COMPLETE;
		commit_list_insert(commit, &complete);
	}
	return 0;
}

int walker_targets_stdin(char ***target, const char ***write_ref)
{
	int targets = 0, targets_alloc = 0;
	struct strbuf buf = STRBUF_INIT;
	*target = NULL; *write_ref = NULL;
	while (1) {
		char *rf_one = NULL;
		char *tg_one;

		if (strbuf_getline_lf(&buf, stdin) == EOF)
			break;
		tg_one = buf.buf;
		rf_one = strchr(tg_one, '\t');
		if (rf_one)
			*rf_one++ = 0;

		if (targets >= targets_alloc) {
			targets_alloc = targets_alloc ? targets_alloc * 2 : 64;
			REALLOC_ARRAY(*target, targets_alloc);
			REALLOC_ARRAY(*write_ref, targets_alloc);
		}
		(*target)[targets] = xstrdup(tg_one);
		(*write_ref)[targets] = xstrdup_or_null(rf_one);
		targets++;
	}
	strbuf_release(&buf);
	return targets;
}

void walker_targets_free(int targets, char **target, const char **write_ref)
{
	while (targets--) {
		free(target[targets]);
		if (write_ref)
			free((char *) write_ref[targets]);
	}
}

int walker_fetch(struct walker *walker, int targets, char **target,
		 const char **write_ref, const char *write_ref_log_details)
{
	struct strbuf refname = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;
	struct ref_transaction *transaction = NULL;
	struct object_id *oids;
	char *msg = NULL;
	int i, ret = -1;

	save_commit_buffer = 0;

	ALLOC_ARRAY(oids, targets);

	if (write_ref) {
		transaction = ref_store_transaction_begin(get_main_ref_store(the_repository),
							  0, &err);
		if (!transaction) {
			error("%s", err.buf);
			goto done;
		}
	}

	if (!walker->get_recover) {
		refs_for_each_ref(get_main_ref_store(the_repository),
				  mark_complete, NULL);
		commit_list_sort_by_date(&complete);
	}

	for (i = 0; i < targets; i++) {
		if (interpret_target(walker, target[i], oids + i)) {
			error("Could not interpret response from server '%s' as something to pull", target[i]);
			goto done;
		}
		if (process(walker, lookup_unknown_object(the_repository, &oids[i])))
			goto done;
	}

	if (loop(walker))
		goto done;
	if (!write_ref) {
		ret = 0;
		goto done;
	}
	if (write_ref_log_details) {
		msg = xstrfmt("fetch from %s", write_ref_log_details);
	} else {
		msg = NULL;
	}
	for (i = 0; i < targets; i++) {
		if (!write_ref[i])
			continue;
		strbuf_reset(&refname);
		strbuf_addf(&refname, "refs/%s", write_ref[i]);
		if (ref_transaction_update(transaction, refname.buf,
					   oids + i, NULL, NULL, NULL, 0,
					   msg ? msg : "fetch (unknown)",
					   &err)) {
			error("%s", err.buf);
			goto done;
		}
	}
	if (ref_transaction_commit(transaction, &err)) {
		error("%s", err.buf);
		goto done;
	}

	ret = 0;

done:
	ref_transaction_free(transaction);
	free(msg);
	free(oids);
	strbuf_release(&err);
	strbuf_release(&refname);
	return ret;
}

void walker_free(struct walker *walker)
{
	walker->cleanup(walker);
	free(walker);
}
