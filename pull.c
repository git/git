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

static const char commitS[] = "commit";
static const char treeS[] = "tree";
static const char blobS[] = "blob";

void pull_say(const char *fmt, const char *hex) {
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

static int process_unknown(unsigned char *sha1);

static int process_tree(unsigned char *sha1)
{
	struct tree *tree = lookup_tree(sha1);
	struct tree_entry_list *entries;

	if (parse_tree(tree))
		return -1;

	for (entries = tree->entries; entries; entries = entries->next) {
		const char *what = entries->directory ? treeS : blobS;
		if (make_sure_we_have_it(what, entries->item.tree->object.sha1))
			return -1;
		if (entries->directory) {
			if (process_tree(entries->item.tree->object.sha1))
				return -1;
		}
	}
	return 0;
}

static int process_commit(unsigned char *sha1)
{
	struct commit *obj = lookup_commit(sha1);

	if (make_sure_we_have_it(commitS, sha1))
		return -1;

	if (parse_commit(obj))
		return -1;

	if (get_tree) {
		if (make_sure_we_have_it(treeS, obj->tree->object.sha1))
			return -1;
		if (process_tree(obj->tree->object.sha1))
			return -1;
		if (!get_all)
			get_tree = 0;
	}
	if (get_history) {
		struct commit_list *parents = obj->parents;
		for (; parents; parents = parents->next) {
			if (has_sha1_file(parents->item->object.sha1))
				continue;
			if (make_sure_we_have_it(NULL,
						 parents->item->object.sha1)) {
				/* The server might not have it, and
				 * we don't mind. 
				 */
				continue;
			}
			if (process_commit(parents->item->object.sha1))
				return -1;
			memcpy(current_commit_sha1, sha1, 20);
		}
	}
	return 0;
}

static int process_tag(unsigned char *sha1)
{
	struct tag *obj = lookup_tag(sha1);

	if (parse_tag(obj))
		return -1;
	return process_unknown(obj->tagged->sha1);
}

static int process_unknown(unsigned char *sha1)
{
	struct object *obj;
	if (make_sure_we_have_it("object", sha1))
		return -1;
	obj = parse_object(sha1);
	if (!obj)
		return error("Unable to parse object %s", sha1_to_hex(sha1));
	if (obj->type == commit_type)
		return process_commit(sha1);
	if (obj->type == tree_type)
		return process_tree(sha1);
	if (obj->type == blob_type)
		return 0;
	if (obj->type == tag_type)
		return process_tag(sha1);
	return error("Unable to determine requirement of type %s for %s",
		     obj->type, sha1_to_hex(sha1));
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
	if (process_unknown(sha1))
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
