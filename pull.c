#include "pull.h"

#include "cache.h"
#include "commit.h"
#include "tree.h"

int get_tree = 0;
int get_history = 0;
int get_all = 0;
static unsigned char current_commit_sha1[20];

static const char commitS[] = "commit";
static const char treeS[] = "tree";
static const char blobS[] = "blob";

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
	int status;
	if (has_sha1_file(sha1))
		return 0;
	status = fetch(sha1);
	if (status && what)
		report_missing(what, sha1);
	return status;
}

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

int pull(char *target)
{
	int retval;
	unsigned char sha1[20];
	retval = get_sha1_hex(target, sha1);
	if (retval)
		return retval;
	retval = make_sure_we_have_it(commitS, sha1);
	if (retval)
		return retval;
	memcpy(current_commit_sha1, sha1, 20);
	return process_commit(sha1);
}
