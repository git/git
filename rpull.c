#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "cache.h"
#include "commit.h"
#include <errno.h>
#include <stdio.h>
#include "rsh.h"

static int tree = 0;
static int commits = 0;
static int all = 0;

static int fd_in;
static int fd_out;

static int fetch(unsigned char *sha1)
{
	if (has_sha1_file(sha1))
		return 0;
	write(fd_out, sha1, 20);
	return write_sha1_from_fd(sha1, fd_in);
}

static int process_tree(unsigned char *sha1)
{
	struct tree *tree = lookup_tree(sha1);
	struct tree_entry_list *entries;

	if (parse_tree(tree))
		return -1;

	for (entries = tree->entries; entries; entries = entries->next) {
		/*
		  fprintf(stderr, "Tree %s ", sha1_to_hex(sha1));
		  fprintf(stderr, "needs %s\n", 
		  sha1_to_hex(entries->item.tree->object.sha1));
		*/
		if (fetch(entries->item.tree->object.sha1)) {
			return error("Missing item %s",
				     sha1_to_hex(entries->item.tree->object.sha1));
		}
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

	if (fetch(sha1)) {
		return error("Fetching %s", sha1_to_hex(sha1));
	}

	if (parse_commit(obj))
		return -1;

	if (tree) {
		if (fetch(obj->tree->object.sha1))
			return -1;
		if (process_tree(obj->tree->object.sha1))
			return -1;
		if (!all)
			tree = 0;
	}
	if (commits) {
		struct commit_list *parents = obj->parents;
		for (; parents; parents = parents->next) {
			if (has_sha1_file(parents->item->object.sha1))
				continue;
			if (fetch(parents->item->object.sha1)) {
				/* The server might not have it, and
				 * we don't mind. 
				 */
				error("Missing tree %s; continuing", 
				      sha1_to_hex(parents->item->object.sha1));
				continue;
			}
			if (process_commit(parents->item->object.sha1))
				return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	char *commit_id;
	char *url;
	int arg = 1;
	unsigned char sha1[20];

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
			tree = 1;
		} else if (argv[arg][1] == 'c') {
			commits = 1;
		} else if (argv[arg][1] == 'a') {
			all = 1;
			tree = 1;
			commits = 1;
		}
		arg++;
	}
	if (argc < arg + 2) {
		usage("rpull [-c] [-t] [-a] commit-id url");
		return 1;
	}
	commit_id = argv[arg];
	url = argv[arg + 1];

	if (setup_connection(&fd_in, &fd_out, "rpush", url, arg, argv + 1))
		return 1;

	get_sha1_hex(commit_id, sha1);

	if (fetch(sha1))
		return 1;
	if (process_commit(sha1))
		return 1;

	return 0;
}
