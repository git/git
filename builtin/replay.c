/*
 * "git replay" builtin command
 */

#include "git-compat-util.h"

#include "builtin.h"
#include "environment.h"
#include "hex.h"
#include "lockfile.h"
#include "merge-ort.h"
#include "object-name.h"
#include "parse-options.h"
#include "refs.h"
#include "revision.h"
#include "strmap.h"
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

struct ref_info {
	struct commit *onto;
	struct strset positive_refs;
	struct strset negative_refs;
	int positive_refexprs;
	int negative_refexprs;
};

static void get_ref_information(struct rev_cmdline_info *cmd_info,
				struct ref_info *ref_info)
{
	int i;

	ref_info->onto = NULL;
	strset_init(&ref_info->positive_refs);
	strset_init(&ref_info->negative_refs);
	ref_info->positive_refexprs = 0;
	ref_info->negative_refexprs = 0;

	/*
	 * When the user specifies e.g.
	 *   git replay origin/main..mybranch
	 *   git replay ^origin/next mybranch1 mybranch2
	 * we want to be able to determine where to replay the commits.  In
	 * these examples, the branches are probably based on an old version
	 * of either origin/main or origin/next, so we want to replay on the
	 * newest version of that branch.  In contrast we would want to error
	 * out if they ran
	 *   git replay ^origin/master ^origin/next mybranch
	 *   git replay mybranch~2..mybranch
	 * the first of those because there's no unique base to choose, and
	 * the second because they'd likely just be replaying commits on top
	 * of the same commit and not making any difference.
	 */
	for (i = 0; i < cmd_info->nr; i++) {
		struct rev_cmdline_entry *e = cmd_info->rev + i;
		struct object_id oid;
		const char *refexpr = e->name;
		char *fullname = NULL;
		int can_uniquely_dwim = 1;

		if (*refexpr == '^')
			refexpr++;
		if (repo_dwim_ref(the_repository, refexpr, strlen(refexpr), &oid, &fullname, 0) != 1)
			can_uniquely_dwim = 0;

		if (e->flags & BOTTOM) {
			if (can_uniquely_dwim)
				strset_add(&ref_info->negative_refs, fullname);
			if (!ref_info->negative_refexprs)
				ref_info->onto = lookup_commit_reference_gently(the_repository,
										&e->item->oid, 1);
			ref_info->negative_refexprs++;
		} else {
			if (can_uniquely_dwim)
				strset_add(&ref_info->positive_refs, fullname);
			ref_info->positive_refexprs++;
		}

		free(fullname);
	}
}

static void determine_replay_mode(struct rev_cmdline_info *cmd_info,
				  const char *onto_name,
				  const char **advance_name,
				  struct commit **onto,
				  struct strset **update_refs)
{
	struct ref_info rinfo;

	get_ref_information(cmd_info, &rinfo);
	if (!rinfo.positive_refexprs)
		die(_("need some commits to replay"));
	if (onto_name && *advance_name)
		die(_("--onto and --advance are incompatible"));
	else if (onto_name) {
		*onto = peel_committish(onto_name);
		if (rinfo.positive_refexprs <
		    strset_get_size(&rinfo.positive_refs))
			die(_("all positive revisions given must be references"));
	} else if (*advance_name) {
		struct object_id oid;
		char *fullname = NULL;

		*onto = peel_committish(*advance_name);
		if (repo_dwim_ref(the_repository, *advance_name, strlen(*advance_name),
			     &oid, &fullname, 0) == 1) {
			*advance_name = fullname;
		} else {
			die(_("argument to --advance must be a reference"));
		}
		if (rinfo.positive_refexprs > 1)
			die(_("cannot advance target with multiple sources because ordering would be ill-defined"));
	} else {
		int positive_refs_complete = (
			rinfo.positive_refexprs ==
			strset_get_size(&rinfo.positive_refs));
		int negative_refs_complete = (
			rinfo.negative_refexprs ==
			strset_get_size(&rinfo.negative_refs));
		/*
		 * We need either positive_refs_complete or
		 * negative_refs_complete, but not both.
		 */
		if (rinfo.negative_refexprs > 0 &&
		    positive_refs_complete == negative_refs_complete)
			die(_("cannot implicitly determine whether this is an --advance or --onto operation"));
		if (negative_refs_complete) {
			struct hashmap_iter iter;
			struct strmap_entry *entry;

			if (rinfo.negative_refexprs == 0)
				die(_("all positive revisions given must be references"));
			else if (rinfo.negative_refexprs > 1)
				die(_("cannot implicitly determine whether this is an --advance or --onto operation"));
			else if (rinfo.positive_refexprs > 1)
				die(_("cannot advance target with multiple source branches because ordering would be ill-defined"));

			/* Only one entry, but we have to loop to get it */
			strset_for_each_entry(&rinfo.negative_refs,
					      &iter, entry) {
				*advance_name = entry->key;
			}
		} else { /* positive_refs_complete */
			if (rinfo.negative_refexprs > 1)
				die(_("cannot implicitly determine correct base for --onto"));
			if (rinfo.negative_refexprs == 1)
				*onto = rinfo.onto;
		}
	}
	if (!*advance_name) {
		*update_refs = xcalloc(1, sizeof(**update_refs));
		**update_refs = rinfo.positive_refs;
		memset(&rinfo.positive_refs, 0, sizeof(**update_refs));
	}
	strset_clear(&rinfo.negative_refs);
	strset_clear(&rinfo.positive_refs);
}

static struct commit *mapped_commit(kh_oid_map_t *replayed_commits,
				    struct commit *commit,
				    struct commit *fallback)
{
	khint_t pos = kh_get_oid_map(replayed_commits, commit->object.oid);
	if (pos == kh_end(replayed_commits))
		return fallback;
	return kh_value(replayed_commits, pos);
}

static struct commit *pick_regular_commit(struct commit *pickme,
					  kh_oid_map_t *replayed_commits,
					  struct commit *onto,
					  struct merge_options *merge_opt,
					  struct merge_result *result)
{
	struct commit *base, *replayed_base;
	struct tree *pickme_tree, *base_tree;

	base = pickme->parents->item;
	replayed_base = mapped_commit(replayed_commits, base, onto);

	result->tree = repo_get_commit_tree(the_repository, replayed_base);
	pickme_tree = repo_get_commit_tree(the_repository, pickme);
	base_tree = repo_get_commit_tree(the_repository, base);

	merge_opt->branch1 = short_commit_name(replayed_base);
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
	return create_commit(result->tree, pickme, replayed_base);
}

int cmd_replay(int argc, const char **argv, const char *prefix)
{
	const char *advance_name = NULL;
	struct commit *onto = NULL;
	const char *onto_name = NULL;
	int contained = 0;

	struct rev_info revs;
	struct commit *last_commit = NULL;
	struct commit *commit;
	struct merge_options merge_opt;
	struct merge_result result;
	struct strset *update_refs = NULL;
	kh_oid_map_t *replayed_commits;
	int ret = 0;

	const char * const replay_usage[] = {
		N_("(EXPERIMENTAL!) git replay "
		   "([--contained] --onto <newbase> | --advance <branch>) "
		   "<revision-range>..."),
		NULL
	};
	struct option replay_options[] = {
		OPT_STRING(0, "advance", &advance_name,
			   N_("branch"),
			   N_("make replay advance given branch")),
		OPT_STRING(0, "onto", &onto_name,
			   N_("revision"),
			   N_("replay onto given commit")),
		OPT_BOOL(0, "contained", &contained,
			 N_("advance all branches contained in revision-range")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, replay_options, replay_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN_OPT);

	if (!onto_name && !advance_name) {
		error(_("option --onto or --advance is mandatory"));
		usage_with_options(replay_usage, replay_options);
	}

	if (advance_name && contained)
		die(_("options '%s' and '%s' cannot be used together"),
		    "--advance", "--contained");

	repo_init_revisions(the_repository, &revs, prefix);

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

	argc = setup_revisions(argc, argv, &revs, NULL);
	if (argc > 1) {
		ret = error(_("unrecognized argument: %s"), argv[1]);
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

	determine_replay_mode(&revs.cmdline, onto_name, &advance_name,
			      &onto, &update_refs);

	if (!onto) /* FIXME: Should handle replaying down to root commit */
		die("Replaying down to root commit is not supported yet!");

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto cleanup;
	}

	init_merge_options(&merge_opt, the_repository);
	memset(&result, 0, sizeof(result));
	merge_opt.show_rename_progress = 0;
	last_commit = onto;
	replayed_commits = kh_init_oid_map();
	while ((commit = get_revision(&revs))) {
		const struct name_decoration *decoration;
		khint_t pos;
		int hr;

		if (!commit->parents)
			die(_("replaying down to root commit is not supported yet!"));
		if (commit->parents->next)
			die(_("replaying merge commits is not supported yet!"));

		last_commit = pick_regular_commit(commit, replayed_commits, onto,
						  &merge_opt, &result);
		if (!last_commit)
			break;

		/* Record commit -> last_commit mapping */
		pos = kh_put_oid_map(replayed_commits, commit->object.oid, &hr);
		if (hr == 0)
			BUG("Duplicate rewritten commit: %s\n",
			    oid_to_hex(&commit->object.oid));
		kh_value(replayed_commits, pos) = last_commit;

		/* Update any necessary branches */
		if (advance_name)
			continue;
		decoration = get_name_decoration(&commit->object);
		if (!decoration)
			continue;
		while (decoration) {
			if (decoration->type == DECORATION_REF_LOCAL &&
			    (contained || strset_contains(update_refs,
							  decoration->name))) {
				printf("update %s %s %s\n",
				       decoration->name,
				       oid_to_hex(&last_commit->object.oid),
				       oid_to_hex(&commit->object.oid));
			}
			decoration = decoration->next;
		}
	}

	/* In --advance mode, advance the target ref */
	if (result.clean == 1 && advance_name) {
		printf("update %s %s %s\n",
		       advance_name,
		       oid_to_hex(&last_commit->object.oid),
		       oid_to_hex(&onto->object.oid));
	}

	merge_finalize(&merge_opt, &result);
	kh_destroy_oid_map(replayed_commits);
	if (update_refs) {
		strset_clear(update_refs);
		free(update_refs);
	}
	ret = result.clean;

cleanup:
	release_revisions(&revs);

	/* Return */
	if (ret < 0)
		exit(128);
	return ret ? 0 : 1;
}
