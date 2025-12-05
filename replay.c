#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "commit.h"
#include "environment.h"
#include "gettext.h"
#include "ident.h"
#include "object.h"
#include "object-name.h"
#include "replay.h"
#include "tree.h"

static const char *short_commit_name(struct repository *repo,
				     struct commit *commit)
{
	return repo_find_unique_abbrev(repo, &commit->object.oid,
				       DEFAULT_ABBREV);
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

struct commit *replay_create_commit(struct repository *repo,
				    struct tree *tree,
				    struct commit *based_on,
				    struct commit *parent)
{
	struct object_id ret;
	struct object *obj = NULL;
	struct commit_list *parents = NULL;
	char *author;
	char *sign_commit = NULL; /* FIXME: cli users might want to sign again */
	struct commit_extra_header *extra = NULL;
	struct strbuf msg = STRBUF_INIT;
	const char *out_enc = get_commit_output_encoding();
	const char *message = repo_logmsg_reencode(repo, based_on,
						   NULL, out_enc);
	const char *orig_message = NULL;
	const char *exclude_gpgsig[] = { "gpgsig", "gpgsig-sha256", NULL };

	commit_list_insert(parent, &parents);
	extra = read_commit_extra_headers(based_on, exclude_gpgsig);
	find_commit_subject(message, &orig_message);
	strbuf_addstr(&msg, orig_message);
	author = get_author(message);
	reset_ident_date();
	if (commit_tree_extended(msg.buf, msg.len, &tree->object.oid, parents,
				 &ret, author, NULL, sign_commit, extra)) {
		error(_("failed to write commit object"));
		goto out;
	}

	obj = parse_object(repo, &ret);

out:
	repo_unuse_commit_buffer(repo, based_on, message);
	free_commit_extra_headers(extra);
	free_commit_list(parents);
	strbuf_release(&msg);
	free(author);
	return (struct commit *)obj;
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

struct commit *replay_pick_regular_commit(struct repository *repo,
					  struct commit *pickme,
					  kh_oid_map_t *replayed_commits,
					  struct commit *onto,
					  struct merge_options *merge_opt,
					  struct merge_result *result)
{
	struct commit *base, *replayed_base;
	struct tree *pickme_tree, *base_tree, *replayed_base_tree;

	base = pickme->parents->item;
	replayed_base = mapped_commit(replayed_commits, base, onto);

	replayed_base_tree = repo_get_commit_tree(repo, replayed_base);
	pickme_tree = repo_get_commit_tree(repo, pickme);
	base_tree = repo_get_commit_tree(repo, base);

	merge_opt->branch1 = short_commit_name(repo, replayed_base);
	merge_opt->branch2 = short_commit_name(repo, pickme);
	merge_opt->ancestor = xstrfmt("parent of %s", merge_opt->branch2);

	merge_incore_nonrecursive(merge_opt,
				  base_tree,
				  replayed_base_tree,
				  pickme_tree,
				  result);

	free((char*)merge_opt->ancestor);
	merge_opt->ancestor = NULL;
	if (!result->clean)
		return NULL;
	/* Drop commits that become empty */
	if (oideq(&replayed_base_tree->object.oid, &result->tree->object.oid) &&
	    !oideq(&pickme_tree->object.oid, &base_tree->object.oid))
		return replayed_base;
	return replay_create_commit(repo, result->tree, pickme, replayed_base);
}
