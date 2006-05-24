#include "fetch.h"

#include "cache.h"
#include "commit.h"
#include "tree.h"
#include "tag.h"
#include "blob.h"
#include "refs.h"

const char *write_ref = NULL;
const char *write_ref_log_details = NULL;

int get_tree = 0;
int get_history = 0;
int get_all = 0;
int get_verbosely = 0;
int get_recover = 0;
static unsigned char current_commit_sha1[20];

void pull_say(const char *fmt, const char *hex) 
{
	if (get_verbosely)
		fprintf(stderr, fmt, hex);
}

static void report_missing(const char *what, const unsigned char *missing)
{
	char missing_hex[41];

	strcpy(missing_hex, sha1_to_hex(missing));;
	fprintf(stderr,
		"Cannot obtain needed %s %s\nwhile processing commit %s.\n",
		what, missing_hex, sha1_to_hex(current_commit_sha1));
}

static int process(struct object *obj);

static int process_tree(struct tree *tree)
{
	struct tree_entry_list *entry;

	if (parse_tree(tree))
		return -1;

	entry = tree->entries;
	tree->entries = NULL;
	while (entry) {
		struct tree_entry_list *next = entry->next;
		if (process(entry->item.any))
			return -1;
		free(entry->name);
		free(entry);
		entry = next;
	}
	return 0;
}

#define COMPLETE	(1U << 0)
#define SEEN		(1U << 1)
#define TO_SCAN		(1U << 2)

static struct commit_list *complete = NULL;

static int process_commit(struct commit *commit)
{
	if (parse_commit(commit))
		return -1;

	while (complete && complete->item->date >= commit->date) {
		pop_most_recent_commit(&complete, COMPLETE);
	}

	if (commit->object.flags & COMPLETE)
		return 0;

	memcpy(current_commit_sha1, commit->object.sha1, 20);

	pull_say("walk %s\n", sha1_to_hex(commit->object.sha1));

	if (get_tree) {
		if (process(&commit->tree->object))
			return -1;
		if (!get_all)
			get_tree = 0;
	}
	if (get_history) {
		struct commit_list *parents = commit->parents;
		for (; parents; parents = parents->next) {
			if (process(&parents->item->object))
				return -1;
		}
	}
	return 0;
}

static int process_tag(struct tag *tag)
{
	if (parse_tag(tag))
		return -1;
	return process(tag->tagged);
}

static struct object_list *process_queue = NULL;
static struct object_list **process_queue_end = &process_queue;

static int process_object(struct object *obj)
{
	if (obj->type == commit_type) {
		if (process_commit((struct commit *)obj))
			return -1;
		return 0;
	}
	if (obj->type == tree_type) {
		if (process_tree((struct tree *)obj))
			return -1;
		return 0;
	}
	if (obj->type == blob_type) {
		return 0;
	}
	if (obj->type == tag_type) {
		if (process_tag((struct tag *)obj))
			return -1;
		return 0;
	}
	return error("Unable to determine requirements "
		     "of type %s for %s",
		     obj->type, sha1_to_hex(obj->sha1));
}

static int process(struct object *obj)
{
	if (obj->flags & SEEN)
		return 0;
	obj->flags |= SEEN;

	if (has_sha1_file(obj->sha1)) {
		/* We already have it, so we should scan it now. */
		obj->flags |= TO_SCAN;
	} else {
		if (obj->flags & COMPLETE)
			return 0;
		prefetch(obj->sha1);
	}
		
	object_list_insert(obj, process_queue_end);
	process_queue_end = &(*process_queue_end)->next;
	return 0;
}

static int loop(void)
{
	struct object_list *elem;

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
			if (fetch(obj->sha1)) {
				report_missing(obj->type
					       ? obj->type
					       : "object", obj->sha1);
				return -1;
			}
		}
		if (!obj->type)
			parse_object(obj->sha1);
		if (process_object(obj))
			return -1;
	}
	return 0;
}

static int interpret_target(char *target, unsigned char *sha1)
{
	if (!get_sha1_hex(target, sha1))
		return 0;
	if (!check_ref_format(target)) {
		if (!fetch_ref(target, sha1)) {
			return 0;
		}
	}
	return -1;
}

static int mark_complete(const char *path, const unsigned char *sha1)
{
	struct commit *commit = lookup_commit_reference_gently(sha1, 1);
	if (commit) {
		commit->object.flags |= COMPLETE;
		insert_by_date(commit, &complete);
	}
	return 0;
}

int pull(char *target)
{
	struct ref_lock *lock;
	unsigned char sha1[20];
	char *msg;
	int ret;

	save_commit_buffer = 0;
	track_object_refs = 0;
	if (write_ref) {
		lock = lock_ref_sha1(write_ref, NULL, 0);
		if (!lock) {
			error("Can't lock ref %s", write_ref);
			return -1;
		}
	}

	if (!get_recover)
		for_each_ref(mark_complete);

	if (interpret_target(target, sha1)) {
		error("Could not interpret %s as something to pull", target);
		unlock_ref(lock);
		return -1;
	}
	if (process(lookup_unknown_object(sha1))) {
		unlock_ref(lock);
		return -1;
	}
	if (loop()) {
		unlock_ref(lock);
		return -1;
	}

	if (write_ref) {
		if (write_ref_log_details) {
			msg = xmalloc(strlen(write_ref_log_details) + 12);
			sprintf(msg, "fetch from %s", write_ref_log_details);
		} else
			msg = NULL;
		ret = write_ref_sha1(lock, sha1, msg ? msg : "fetch (unknown)");
		if (msg)
			free(msg);
		return ret;
	}
	return 0;
}
