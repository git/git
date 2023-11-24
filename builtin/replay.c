/*
 * "git replay" builtin command
 */

#define USE_THE_INDEX_VARIABLE
#include "git-compat-util.h"

#include "builtin.h"
#include "cache-tree.h"
#include "commit.h"
#include "environment.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "lockfile.h"
#include "merge-ort.h"
#include "object-name.h"
#include "parse-options.h"
#include "refs.h"
#include "revision.h"
#include "sequencer.h"
#include "setup.h"
#include "strvec.h"
#include <oidset.h>
#include <tree.h>

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
	char *sign_commit = NULL; /* FIXME: cli users might want to sign again */
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

static struct commit *pick_regular_commit(struct commit *pickme,
					  struct commit *last_commit,
					  struct merge_options *merge_opt,
					  struct merge_result *result)
{
	struct commit *base;
	struct tree *pickme_tree, *base_tree;

	base = pickme->parents->item;

	pickme_tree = repo_get_commit_tree(the_repository, pickme);
	base_tree = repo_get_commit_tree(the_repository, base);

	merge_opt->branch2 = short_commit_name(pickme);
	merge_opt->ancestor = xstrfmt("parent of %s", merge_opt->branch2);

	merge_incore_nonrecursive(merge_opt,
				  base_tree,
				  result->tree,
				  pickme_tree,
				  result);

	free((char*)merge_opt->ancestor);
	merge_opt->ancestor = NULL;
	if (!result->clean)
		return NULL;
	return create_commit(result->tree, pickme, last_commit);
}

int cmd_replay(int argc, const char **argv, const char *prefix)
{
	struct commit *onto;
	const char *onto_name = NULL;
	struct commit *last_commit = NULL, *last_picked_commit = NULL;
	struct lock_file lock = LOCK_INIT;
	struct strvec rev_walk_args = STRVEC_INIT;
	struct rev_info revs;
	struct commit *commit;
	struct merge_options merge_opt;
	struct tree *head_tree;
	struct merge_result result;
	struct strbuf reflog_msg = STRBUF_INIT;
	struct strbuf branch_name = STRBUF_INIT;
	int ret = 0;

	const char * const replay_usage[] = {
		N_("(EXPERIMENTAL!) git replay --onto <newbase> <oldbase> <branch>"),
		NULL
	};
	struct option replay_options[] = {
		OPT_STRING(0, "onto", &onto_name,
			   N_("revision"),
			   N_("replay onto given commit")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, replay_options, replay_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT);

	if (!onto_name) {
		error(_("option --onto is mandatory"));
		usage_with_options(replay_usage, replay_options);
	}

	if (argc != 3) {
		error(_("bad number of arguments"));
		usage_with_options(replay_usage, replay_options);
	}

	onto = peel_committish(onto_name);
	strbuf_addf(&branch_name, "refs/heads/%s", argv[2]);

	repo_hold_locked_index(the_repository, &lock, LOCK_DIE_ON_ERROR);
	if (repo_read_index(the_repository) < 0)
		BUG("Could not read index");

	repo_init_revisions(the_repository, &revs, prefix);

	strvec_pushl(&rev_walk_args, "", argv[2], "--not", argv[1], NULL);

	/*
	 * Set desired values for rev walking options here. If they
	 * are changed by some user specified option in setup_revisions()
	 * below, we will detect that below and then warn.
	 *
	 * TODO: In the future we might want to either die(), or allow
	 * some options changing these values if we think they could
	 * be useful.
	 */
	revs.reverse = 1;
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;
	revs.simplify_history = 0;

	if (setup_revisions(rev_walk_args.nr, rev_walk_args.v, &revs, NULL) > 1) {
		ret = error(_("unhandled options"));
		goto cleanup;
	}

	/*
	 * Detect and warn if we override some user specified rev
	 * walking options.
	 */
	if (revs.reverse != 1) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"reverse");
		revs.reverse = 1;
	}
	if (revs.sort_order != REV_SORT_IN_GRAPH_ORDER) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"sort_order");
		revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	}
	if (revs.topo_order != 1) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"topo_order");
		revs.topo_order = 1;
	}
	if (revs.simplify_history != 0) {
		warning(_("some rev walking options will be overridden as "
			  "'%s' bit in 'struct rev_info' will be forced"),
			"simplify_history");
		revs.simplify_history = 0;
	}

	strvec_clear(&rev_walk_args);

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto cleanup;
	}

	init_merge_options(&merge_opt, the_repository);
	memset(&result, 0, sizeof(result));
	merge_opt.show_rename_progress = 0;
	merge_opt.branch1 = "HEAD";
	head_tree = repo_get_commit_tree(the_repository, onto);
	result.tree = head_tree;
	last_commit = onto;
	while ((commit = get_revision(&revs))) {
		struct commit *pick;

		if (!commit->parents)
			die(_("replaying down to root commit is not supported yet!"));
		if (commit->parents->next)
			die(_("replaying merge commits is not supported yet!"));

		pick = pick_regular_commit(commit, last_commit, &merge_opt, &result);
		if (!pick)
			break;
		last_commit = pick;
		last_picked_commit = commit;
	}

	merge_finalize(&merge_opt, &result);

	if (result.clean < 0)
		exit(128);

	if (result.clean) {
		strbuf_addf(&reflog_msg, "finish rebase %s onto %s",
			    oid_to_hex(&last_picked_commit->object.oid),
			    oid_to_hex(&last_commit->object.oid));
		if (update_ref(reflog_msg.buf, branch_name.buf,
			       &last_commit->object.oid,
			       &last_picked_commit->object.oid,
			       REF_NO_DEREF, UPDATE_REFS_MSG_ON_ERR)) {
			error(_("could not update %s"), argv[2]);
			die("Failed to update %s", argv[2]);
		}
		if (create_symref("HEAD", branch_name.buf, reflog_msg.buf) < 0)
			die(_("unable to update HEAD"));
	} else {
		strbuf_addf(&reflog_msg, "rebase progress up to %s",
			    oid_to_hex(&last_picked_commit->object.oid));
		if (update_ref(reflog_msg.buf, "HEAD",
			       &last_commit->object.oid,
			       &onto->object.oid,
			       REF_NO_DEREF, UPDATE_REFS_MSG_ON_ERR)) {
			error(_("could not update %s"), argv[2]);
			die("Failed to update %s", argv[2]);
		}
	}
	ret = (result.clean == 0);
cleanup:
	strbuf_release(&reflog_msg);
	strbuf_release(&branch_name);
	release_revisions(&revs);
	return ret;
}
