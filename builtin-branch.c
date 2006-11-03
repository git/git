/*
 * Builtin "git branch"
 *
 * Copyright (c) 2006 Kristian Høgsberg <krh@redhat.com>
 * Based on git-branch.sh by Junio C Hamano.
 */

#include "cache.h"
#include "refs.h"
#include "commit.h"
#include "builtin.h"

static const char builtin_branch_usage[] =
"git-branch (-d | -D) <branchname> | [-l] [-f] <branchname> [<start-point>] | [-r]";


static const char *head;
static unsigned char head_sha1[20];

static int in_merge_bases(const unsigned char *sha1,
			  struct commit *rev1,
			  struct commit *rev2)
{
	struct commit_list *bases, *b;
	int ret = 0;

	bases = get_merge_bases(rev1, rev2, 1);
	for (b = bases; b; b = b->next) {
		if (!hashcmp(sha1, b->item->object.sha1)) {
			ret = 1;
			break;
		}
	}

	free_commit_list(bases);
	return ret;
}

static void delete_branches(int argc, const char **argv, int force)
{
	struct commit *rev, *head_rev;
	unsigned char sha1[20];
	char *name;
	int i;

	head_rev = lookup_commit_reference(head_sha1);
	for (i = 0; i < argc; i++) {
		if (!strcmp(head, argv[i]))
			die("Cannot delete the branch you are currently on.");

		name = xstrdup(mkpath("refs/heads/%s", argv[i]));
		if (!resolve_ref(name, sha1, 1, NULL))
			die("Branch '%s' not found.", argv[i]);

		rev = lookup_commit_reference(sha1);
		if (!rev || !head_rev)
			die("Couldn't look up commit objects.");

		/* This checks whether the merge bases of branch and
		 * HEAD contains branch -- which means that the HEAD
		 * contains everything in both.
		 */

		if (!force &&
		    !in_merge_bases(sha1, rev, head_rev)) {
			fprintf(stderr,
				"The branch '%s' is not a strict subset of your current HEAD.\n"
				"If you are sure you want to delete it, run 'git branch -D %s'.\n",
				argv[i], argv[i]);
			exit(1);
		}

		if (delete_ref(name, sha1))
			printf("Error deleting branch '%s'\n", argv[i]);
		else
			printf("Deleted branch %s.\n", argv[i]);

		free(name);
	}
}

static int ref_index, ref_alloc;
static char **ref_list;

static int append_ref(const char *refname, const unsigned char *sha1, int flags,
		void *cb_data)
{
	if (ref_index >= ref_alloc) {
		ref_alloc = alloc_nr(ref_alloc);
		ref_list = xrealloc(ref_list, ref_alloc * sizeof(char *));
	}

	ref_list[ref_index++] = xstrdup(refname);

	return 0;
}

static int ref_cmp(const void *r1, const void *r2)
{
	return strcmp(*(char **)r1, *(char **)r2);
}

static void print_ref_list(int remote_only)
{
	int i;
	char c;

	if (remote_only)
		for_each_remote_ref(append_ref, NULL);
	else
		for_each_branch_ref(append_ref, NULL);

	qsort(ref_list, ref_index, sizeof(char *), ref_cmp);

	for (i = 0; i < ref_index; i++) {
		c = ' ';
		if (!strcmp(ref_list[i], head))
			c = '*';

		printf("%c %s\n", c, ref_list[i]);
	}
}

static void create_branch(const char *name, const char *start,
			  int force, int reflog)
{
	struct ref_lock *lock;
	struct commit *commit;
	unsigned char sha1[20];
	char ref[PATH_MAX], msg[PATH_MAX + 20];

	snprintf(ref, sizeof ref, "refs/heads/%s", name);
	if (check_ref_format(ref))
		die("'%s' is not a valid branch name.", name);

	if (resolve_ref(ref, sha1, 1, NULL)) {
		if (!force)
			die("A branch named '%s' already exists.", name);
		else if (!strcmp(head, name))
			die("Cannot force update the current branch.");
	}

	if (get_sha1(start, sha1) ||
	    (commit = lookup_commit_reference(sha1)) == NULL)
		die("Not a valid branch point: '%s'.", start);
	hashcpy(sha1, commit->object.sha1);

	lock = lock_any_ref_for_update(ref, NULL);
	if (!lock)
		die("Failed to lock ref for update: %s.", strerror(errno));

	if (reflog) {
		log_all_ref_updates = 1;
		snprintf(msg, sizeof msg, "branch: Created from %s", start);
	}

	if (write_ref_sha1(lock, sha1, msg) < 0)
		die("Failed to write ref: %s.", strerror(errno));
}

int cmd_branch(int argc, const char **argv, const char *prefix)
{
	int delete = 0, force_delete = 0, force_create = 0, remote_only = 0;
	int reflog = 0;
	int i;

	git_config(git_default_config);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-')
			break;
		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		if (!strcmp(arg, "-d")) {
			delete = 1;
			continue;
		}
		if (!strcmp(arg, "-D")) {
			delete = 1;
			force_delete = 1;
			continue;
		}
		if (!strcmp(arg, "-f")) {
			force_create = 1;
			continue;
		}
		if (!strcmp(arg, "-r")) {
			remote_only = 1;
			continue;
		}
		if (!strcmp(arg, "-l")) {
			reflog = 1;
			continue;
		}
		usage(builtin_branch_usage);
	}

	head = xstrdup(resolve_ref("HEAD", head_sha1, 0, NULL));
	if (!head)
		die("Failed to resolve HEAD as a valid ref.");
	if (strncmp(head, "refs/heads/", 11))
		die("HEAD not found below refs/heads!");
	head += 11;

	if (delete)
		delete_branches(argc - i, argv + i, force_delete);
	else if (i == argc)
		print_ref_list(remote_only);
	else if (i == argc - 1)
		create_branch(argv[i], head, force_create, reflog);
	else if (i == argc - 2)
		create_branch(argv[i], argv[i + 1], force_create, reflog);
	else
		usage(builtin_branch_usage);

	return 0;
}
