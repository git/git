/*
 * "git fast-rebase" builtin command
 *
 * FAST: Forking Any Subprocesses (is) Taboo
 *
 * This is meant SOLELY as a demo of what is possible.  sequencer.c and
 * rebase.c should be refactored to use the ideas here, rather than attempting
 * to extend this file to replace those (unless Phillip or Dscho say that
 * refactoring is too hard and we need a clean slate, but I'm guessing that
 * refactoring is the better route).
 */

#define USE_THE_INDEX_VARIABLE
#include "test-tool.h"
#include "cache-tree.h"
#include "commit.h"
#include "environment.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "lockfile.h"
#include "merge-ort.h"
#include "object-name.h"
#include "read-cache-ll.h"
#include "refs.h"
#include "revision.h"
#include "sequencer.h"
#include "setup.h"
#include "strvec.h"
#include "tree.h"

static const char *short_commit_name(struct commit *commit)
{
	return repo_find_unique_abbrev(the_repository, &commit->object.oid,
				       DEFAULT_ABBREV);
}

static struct commit *peel_committish(const char *name)
{
	struct object *obj;
	struct object_id oid;

	if (repo_get_oid(the_repository, name, &oid))
		return NULL;
	obj = parse_object(the_repository, &oid);
	return (struct commit *)repo_peel_to_type(the_repository, name, 0, obj,
						  OBJ_COMMIT);
}

static char *get_author(const char *message)
{
	size_t len;
	const char *a;

	a = find_commit_header(message, "author", &len);
	if (a)
		return xmemdupz(a, len);

	return NULL;
}

static struct commit *create_commit(struct tree *tree,
				    struct commit *based_on,
				    struct commit *parent)
{
	struct object_id ret;
	struct object *obj;
	struct commit_list *parents = NULL;
	char *author;
	char *sign_commit = NULL;
	struct commit_extra_header *extra;
	struct strbuf msg = STRBUF_INIT;
	const char *out_enc = get_commit_output_encoding();
	const char *message = repo_logmsg_reencode(the_repository, based_on,
						   NULL, out_enc);
	const char *orig_message = NULL;
	const char *exclude_gpgsig[] = { "gpgsig", NULL };

	commit_list_insert(parent, &parents);
	extra = read_commit_extra_headers(based_on, exclude_gpgsig);
	find_commit_subject(message, &orig_message);
	strbuf_addstr(&msg, orig_message);
	author = get_author(message);
	reset_ident_date();
	if (commit_tree_extended(msg.buf, msg.len, &tree->object.oid, parents,
				 &ret, author, NULL, sign_commit, extra)) {
		error(_("failed to write commit object"));
		return NULL;
	}
	free(author);
	strbuf_release(&msg);

	obj = parse_object(the_repository, &ret);
	return (struct commit *)obj;
}

int cmd__fast_rebase(int argc, const char **argv)
{
	struct commit *onto;
	struct commit *last_commit = NULL, *last_picked_commit = NULL;
	struct object_id head;
	struct lock_file lock = LOCK_INIT;
	struct strvec rev_walk_args = STRVEC_INIT;
	struct rev_info revs;
	struct commit *commit;
	struct merge_options merge_opt;
	struct tree *next_tree, *base_tree, *head_tree;
	struct merge_result result;
	struct strbuf reflog_msg = STRBUF_INIT;
	struct strbuf branch_name = STRBUF_INIT;
	int ret = 0;

	/*
	 * test-tool stuff doesn't set up the git directory by default; need to
	 * do that manually.
	 */
	setup_git_directory();

	if (argc == 2 && !strcmp(argv[1], "-h")) {
		printf("Sorry, I am not a psychiatrist; I can not give you the help you need.  Oh, you meant usage...\n");
		exit(129);
	}

	if (argc != 5 || strcmp(argv[1], "--onto"))
		die("usage: read the code, figure out how to use it, then do so");

	onto = peel_committish(argv[2]);
	strbuf_addf(&branch_name, "refs/heads/%s", argv[4]);

	/* Sanity check */
	if (repo_get_oid(the_repository, "HEAD", &head))
		die(_("Cannot read HEAD"));
	assert(oideq(&onto->object.oid, &head));

	repo_hold_locked_index(the_repository, &lock, LOCK_DIE_ON_ERROR);
	if (repo_read_index(the_repository) < 0)
		BUG("Could not read index");

	repo_init_revisions(the_repository, &revs, NULL);
	revs.verbose_header = 1;
	revs.max_parents = 1;
	revs.cherry_mark = 1;
	revs.limited = 1;
	revs.reverse = 1;
	revs.right_only = 1;
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;
	strvec_pushl(&rev_walk_args, "", argv[4], "--not", argv[3], NULL);

	if (setup_revisions(rev_walk_args.nr, rev_walk_args.v, &revs, NULL) > 1) {
		ret = error(_("unhandled options"));
		goto cleanup;
	}

	strvec_clear(&rev_walk_args);

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto cleanup;
	}

	init_merge_options(&merge_opt, the_repository);
	memset(&result, 0, sizeof(result));
	merge_opt.show_rename_progress = 1;
	merge_opt.branch1 = "HEAD";
	head_tree = repo_get_commit_tree(the_repository, onto);
	result.tree = head_tree;
	last_commit = onto;
	while ((commit = get_revision(&revs))) {
		struct commit *base;

		fprintf(stderr, "Rebasing %s...\r",
			oid_to_hex(&commit->object.oid));
		assert(commit->parents && !commit->parents->next);
		base = commit->parents->item;

		next_tree = repo_get_commit_tree(the_repository, commit);
		base_tree = repo_get_commit_tree(the_repository, base);

		merge_opt.branch2 = short_commit_name(commit);
		merge_opt.ancestor = xstrfmt("parent of %s", merge_opt.branch2);

		merge_incore_nonrecursive(&merge_opt,
					  base_tree,
					  result.tree,
					  next_tree,
					  &result);

		free((char*)merge_opt.ancestor);
		merge_opt.ancestor = NULL;
		if (!result.clean)
			break;
		last_picked_commit = commit;
		last_commit = create_commit(result.tree, commit, last_commit);
	}

	merge_finalize(&merge_opt, &result);

	if (result.clean < 0)
		exit(128);

	if (result.clean) {
		fprintf(stderr, "\nDone.\n");
		strbuf_addf(&reflog_msg, "finish rebase %s onto %s",
			    oid_to_hex(&last_picked_commit->object.oid),
			    oid_to_hex(&last_commit->object.oid));
		if (update_ref(reflog_msg.buf, branch_name.buf,
			       &last_commit->object.oid,
			       &last_picked_commit->object.oid,
			       REF_NO_DEREF, UPDATE_REFS_MSG_ON_ERR)) {
			error(_("could not update %s"), argv[4]);
			die("Failed to update %s", argv[4]);
		}
		if (create_symref("HEAD", branch_name.buf, reflog_msg.buf) < 0)
			die(_("unable to update HEAD"));
	} else {
		fprintf(stderr, "\nAborting: Hit a conflict.\n");
		strbuf_addf(&reflog_msg, "rebase progress up to %s",
			    oid_to_hex(&last_picked_commit->object.oid));
		if (update_ref(reflog_msg.buf, "HEAD",
			       &last_commit->object.oid,
			       &head,
			       REF_NO_DEREF, UPDATE_REFS_MSG_ON_ERR)) {
			error(_("could not update %s"), argv[4]);
			die("Failed to update %s", argv[4]);
		}
	}
	ret = (result.clean == 0);
cleanup:
	strbuf_release(&reflog_msg);
	strbuf_release(&branch_name);
	release_revisions(&revs);
	return ret;
}
