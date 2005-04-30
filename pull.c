#include "pull.h"

#include "cache.h"
#include "commit.h"
#include "tree.h"

int get_tree = 0;
int get_history = 0;
int get_all = 0;

static int process_tree(unsigned char *sha1)
{
	struct tree *tree = lookup_tree(sha1);
	struct tree_entry_list *entries;

	if (parse_tree(tree))
		return -1;

	for (entries = tree->entries; entries; entries = entries->next) {
		if (fetch(entries->item.tree->object.sha1))
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

	if (fetch(sha1))
		return -1;

	if (parse_commit(obj))
		return -1;

	if (get_tree) {
		if (fetch(obj->tree->object.sha1))
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
			if (fetch(parents->item->object.sha1)) {
				/* The server might not have it, and
				 * we don't mind. 
				 */
				continue;
			}
			if (process_commit(parents->item->object.sha1))
				return -1;
		}
	}
	return 0;
}

int pull(char *target)
{
	int retval;
	unsigned char sha1[20];
	retval = get_sha1_hex(target, sha1);
	if (retval)
		return retval;
	retval = fetch(sha1);
	if (retval)
		return retval;
	return process_commit(sha1);
}
