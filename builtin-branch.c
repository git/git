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
  "git-branch (-d | -D) <branchname> | [-l] [-f] <branchname> [<start-point>] | (-m | -M) [<oldbranch>] <newbranch> | [-r | -a] [-v [--abbrev=<length>]]";


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
	struct commit *rev, *head_rev = head_rev;
	unsigned char sha1[20];
	char *name;
	int i;

	if (!force) {
		head_rev = lookup_commit_reference(head_sha1);
		if (!head_rev)
			die("Couldn't look up commit object for HEAD");
	}
	for (i = 0; i < argc; i++) {
		if (!strcmp(head, argv[i]))
			die("Cannot delete the branch you are currently on.");

		name = xstrdup(mkpath("refs/heads/%s", argv[i]));
		if (!resolve_ref(name, sha1, 1, NULL))
			die("Branch '%s' not found.", argv[i]);

		rev = lookup_commit_reference(sha1);
		if (!rev)
			die("Couldn't look up commit object for '%s'", name);

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

#define REF_UNKNOWN_TYPE    0x00
#define REF_LOCAL_BRANCH    0x01
#define REF_REMOTE_BRANCH   0x02
#define REF_TAG             0x04

struct ref_item {
	char *name;
	unsigned int kind;
	unsigned char sha1[20];
};

struct ref_list {
	int index, alloc, maxwidth;
	struct ref_item *list;
	int kinds;
};

static int append_ref(const char *refname, const unsigned char *sha1, int flags, void *cb_data)
{
	struct ref_list *ref_list = (struct ref_list*)(cb_data);
	struct ref_item *newitem;
	int kind = REF_UNKNOWN_TYPE;
	int len;

	/* Detect kind */
	if (!strncmp(refname, "refs/heads/", 11)) {
		kind = REF_LOCAL_BRANCH;
		refname += 11;
	} else if (!strncmp(refname, "refs/remotes/", 13)) {
		kind = REF_REMOTE_BRANCH;
		refname += 13;
	} else if (!strncmp(refname, "refs/tags/", 10)) {
		kind = REF_TAG;
		refname += 10;
	}

	/* Don't add types the caller doesn't want */
	if ((kind & ref_list->kinds) == 0)
		return 0;

	/* Resize buffer */
	if (ref_list->index >= ref_list->alloc) {
		ref_list->alloc = alloc_nr(ref_list->alloc);
		ref_list->list = xrealloc(ref_list->list,
				ref_list->alloc * sizeof(struct ref_item));
	}

	/* Record the new item */
	newitem = &(ref_list->list[ref_list->index++]);
	newitem->name = xstrdup(refname);
	newitem->kind = kind;
	hashcpy(newitem->sha1, sha1);
	len = strlen(newitem->name);
	if (len > ref_list->maxwidth)
		ref_list->maxwidth = len;

	return 0;
}

static void free_ref_list(struct ref_list *ref_list)
{
	int i;

	for (i = 0; i < ref_list->index; i++)
		free(ref_list->list[i].name);
	free(ref_list->list);
}

static int ref_cmp(const void *r1, const void *r2)
{
	struct ref_item *c1 = (struct ref_item *)(r1);
	struct ref_item *c2 = (struct ref_item *)(r2);

	if (c1->kind != c2->kind)
		return c1->kind - c2->kind;
	return strcmp(c1->name, c2->name);
}

static void print_ref_info(const unsigned char *sha1, int abbrev)
{
	struct commit *commit;
	char subject[256];


	commit = lookup_commit(sha1);
	if (commit && !parse_commit(commit))
		pretty_print_commit(CMIT_FMT_ONELINE, commit, ~0,
				    subject, sizeof(subject), 0,
				    NULL, NULL, 0);
	else
		strcpy(subject, " **** invalid ref ****");

	printf(" %s %s\n", find_unique_abbrev(sha1, abbrev), subject);
}

static void print_ref_list(int kinds, int verbose, int abbrev)
{
	int i;
	char c;
	struct ref_list ref_list;

	memset(&ref_list, 0, sizeof(ref_list));
	ref_list.kinds = kinds;
	for_each_ref(append_ref, &ref_list);

	qsort(ref_list.list, ref_list.index, sizeof(struct ref_item), ref_cmp);

	for (i = 0; i < ref_list.index; i++) {
		c = ' ';
		if (ref_list.list[i].kind == REF_LOCAL_BRANCH &&
				!strcmp(ref_list.list[i].name, head))
			c = '*';

		if (verbose) {
			printf("%c %-*s", c, ref_list.maxwidth,
			       ref_list.list[i].name);
			print_ref_info(ref_list.list[i].sha1, abbrev);
		}
		else
			printf("%c %s\n", c, ref_list.list[i].name);
	}

	free_ref_list(&ref_list);
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

static void rename_branch(const char *oldname, const char *newname, int force)
{
	char oldref[PATH_MAX], newref[PATH_MAX], logmsg[PATH_MAX*2 + 100];
	unsigned char sha1[20];

	if (snprintf(oldref, sizeof(oldref), "refs/heads/%s", oldname) > sizeof(oldref))
		die("Old branchname too long");

	if (check_ref_format(oldref))
		die("Invalid branch name: %s", oldref);

	if (snprintf(newref, sizeof(newref), "refs/heads/%s", newname) > sizeof(newref))
		die("New branchname too long");

	if (check_ref_format(newref))
		die("Invalid branch name: %s", newref);

	if (resolve_ref(newref, sha1, 1, NULL) && !force)
		die("A branch named '%s' already exists.", newname);

	snprintf(logmsg, sizeof(logmsg), "Branch: renamed %s to %s",
		 oldref, newref);

	if (rename_ref(oldref, newref, logmsg))
		die("Branch rename failed");

	if (!strcmp(oldname, head) && create_symref("HEAD", newref))
		die("Branch renamed to %s, but HEAD is not updated!", newname);
}

int cmd_branch(int argc, const char **argv, const char *prefix)
{
	int delete = 0, force_delete = 0, force_create = 0;
	int rename = 0, force_rename = 0;
	int verbose = 0, abbrev = DEFAULT_ABBREV;
	int reflog = 0;
	int kinds = REF_LOCAL_BRANCH;
	int i;

	setup_ident();
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
		if (!strcmp(arg, "-m")) {
			rename = 1;
			continue;
		}
		if (!strcmp(arg, "-M")) {
			rename = 1;
			force_rename = 1;
			continue;
		}
		if (!strcmp(arg, "-r")) {
			kinds = REF_REMOTE_BRANCH;
			continue;
		}
		if (!strcmp(arg, "-a")) {
			kinds = REF_REMOTE_BRANCH | REF_LOCAL_BRANCH;
			continue;
		}
		if (!strcmp(arg, "-l")) {
			reflog = 1;
			continue;
		}
		if (!strncmp(arg, "--abbrev=", 9)) {
			abbrev = atoi(arg+9);
			continue;
		}
		if (!strcmp(arg, "-v")) {
			verbose = 1;
			continue;
		}
		usage(builtin_branch_usage);
	}

	if ((delete && rename) || (delete && force_create) ||
	    (rename && force_create))
		usage(builtin_branch_usage);

	head = xstrdup(resolve_ref("HEAD", head_sha1, 0, NULL));
	if (!head)
		die("Failed to resolve HEAD as a valid ref.");
	if (strncmp(head, "refs/heads/", 11))
		die("HEAD not found below refs/heads!");
	head += 11;

	if (delete)
		delete_branches(argc - i, argv + i, force_delete);
	else if (i == argc)
		print_ref_list(kinds, verbose, abbrev);
	else if (rename && (i == argc - 1))
		rename_branch(head, argv[i], force_rename);
	else if (rename && (i == argc - 2))
		rename_branch(argv[i], argv[i + 1], force_rename);
	else if (i == argc - 1)
		create_branch(argv[i], head, force_create, reflog);
	else if (i == argc - 2)
		create_branch(argv[i], argv[i + 1], force_create, reflog);
	else
		usage(builtin_branch_usage);

	return 0;
}
