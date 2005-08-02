#include "pull.h"

#include "cache.h"
#include "commit.h"
#include "tree.h"
#include "tag.h"
#include "blob.h"
#include "refs.h"

const char *write_ref = NULL;

const unsigned char *current_ref = NULL;

int get_tree = 0;
int get_history = 0;
int get_all = 0;
int get_verbosely = 0;
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

static int make_sure_we_have_it(const char *what, unsigned char *sha1)
{
	int status = 0;

	if (!has_sha1_file(sha1)) {
		status = fetch(sha1);
		if (status && what)
			report_missing(what, sha1);
	}
	return status;
}

static int process(unsigned char *sha1, const char *type);

static int process_tree(struct tree *tree)
{
	struct tree_entry_list *entries;

	if (parse_tree(tree))
		return -1;

	for (entries = tree->entries; entries; entries = entries->next) {
		if (process(entries->item.any->sha1,
			    entries->directory ? tree_type : blob_type))
			return -1;
	}
	return 0;
}

static int process_commit(struct commit *commit)
{
	if (parse_commit(commit))
		return -1;

	memcpy(current_commit_sha1, commit->object.sha1, 20);

	if (get_tree) {
		if (process(commit->tree->object.sha1, tree_type))
			return -1;
		if (!get_all)
			get_tree = 0;
	}
	if (get_history) {
		struct commit_list *parents = commit->parents;
		for (; parents; parents = parents->next) {
			if (has_sha1_file(parents->item->object.sha1))
				continue;
			if (process(parents->item->object.sha1,
				    commit_type))
				return -1;
		}
	}
	return 0;
}

static int process_tag(struct tag *tag)
{
	if (parse_tag(tag))
		return -1;
	return process(tag->tagged->sha1, NULL);
}

static struct object_list *process_queue = NULL;
static struct object_list **process_queue_end = &process_queue;

static int process(unsigned char *sha1, const char *type)
{
	struct object *obj;
	if (has_sha1_file(sha1))
		return 0;
	obj = lookup_object_type(sha1, type);
	if (object_list_contains(process_queue, obj))
		return 0;
	object_list_insert(obj, process_queue_end);
	process_queue_end = &(*process_queue_end)->next;

	//fprintf(stderr, "prefetch %s\n", sha1_to_hex(sha1));
	prefetch(sha1);
		
	return 0;
}

static int loop(void)
{
	while (process_queue) {
		struct object *obj = process_queue->item;
		/*
		fprintf(stderr, "%d objects to pull\n", 
			object_list_length(process_queue));
		*/
		process_queue = process_queue->next;
		if (!process_queue)
			process_queue_end = &process_queue;

		//fprintf(stderr, "fetch %s\n", sha1_to_hex(obj->sha1));
		
		if (make_sure_we_have_it(obj->type ?: "object", 
					 obj->sha1))
			return -1;
		if (!obj->type)
			parse_object(obj->sha1);
		if (obj->type == commit_type) {
			if (process_commit((struct commit *)obj))
				return -1;
			continue;
		}
		if (obj->type == tree_type) {
			if (process_tree((struct tree *)obj))
				return -1;
			continue;
		}
		if (obj->type == blob_type) {
			continue;
		}
		if (obj->type == tag_type) {
			if (process_tag((struct tag *)obj))
				return -1;
			continue;
		}
		return error("Unable to determine requirements "
			     "of type %s for %s",
			     obj->type, sha1_to_hex(obj->sha1));
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


int pull(char *target)
{
	unsigned char sha1[20];
	int fd = -1;

	if (write_ref && current_ref) {
		fd = lock_ref_sha1(write_ref, current_ref);
		if (fd < 0)
			return -1;
	}

	if (interpret_target(target, sha1))
		return error("Could not interpret %s as something to pull",
			     target);
	if (process(sha1, NULL))
		return -1;
	if (loop())
		return -1;
	
	if (write_ref) {
		if (current_ref) {
			write_ref_sha1(write_ref, fd, sha1);
		} else {
			write_ref_sha1_unlocked(write_ref, sha1);
		}
	}
	return 0;
}
