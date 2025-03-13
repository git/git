#include "git-compat-util.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "lockfile.h"
#include "merge-ort.h"
#include "merge-ort-wrappers.h"
#include "read-cache-ll.h"
#include "repository.h"
#include "tag.h"
#include "tree.h"

#include "commit.h"

static int unclean(struct merge_options *opt, struct tree *head)
{
	/* Sanity check on repo state; index must match head */
	struct strbuf sb = STRBUF_INIT;

	if (head && repo_index_has_changes(opt->repo, head, &sb)) {
		error(_("Your local changes to the following files would be overwritten by merge:\n  %s"),
		      sb.buf);
		strbuf_release(&sb);
		return -1;
	}

	return 0;
}

int merge_ort_nonrecursive(struct merge_options *opt,
			   struct tree *head,
			   struct tree *merge,
			   struct tree *merge_base)
{
	struct merge_result result;
	int show_msgs;

	if (unclean(opt, head))
		return -1;

	if (oideq(&merge_base->object.oid, &merge->object.oid)) {
		printf_ln(_("Already up to date."));
		return 1;
	}

	show_msgs = !!opt->verbosity;
	memset(&result, 0, sizeof(result));
	merge_incore_nonrecursive(opt, merge_base, head, merge, &result);
	merge_switch_to_result(opt, head, &result, 1, show_msgs);

	return result.clean;
}

int merge_ort_recursive(struct merge_options *opt,
			struct commit *side1,
			struct commit *side2,
			const struct commit_list *merge_bases,
			struct commit **result)
{
	struct tree *head = repo_get_commit_tree(opt->repo, side1);
	struct merge_result tmp;
	int show_msgs;

	if (unclean(opt, head))
		return -1;

	show_msgs = !!opt->verbosity;
	memset(&tmp, 0, sizeof(tmp));
	merge_incore_recursive(opt, merge_bases, side1, side2, &tmp);
	merge_switch_to_result(opt, head, &tmp, 1, show_msgs);
	*result = NULL;

	return tmp.clean;
}

static struct commit *get_ref(struct repository *repo,
			      const struct object_id *oid,
			      const char *name)
{
	struct object *object;

	object = deref_tag(repo, parse_object(repo, oid),
			   name, strlen(name));
	if (!object)
		return NULL;
	if (object->type == OBJ_TREE)
		return make_virtual_commit(repo, (struct tree*)object, name);
	if (object->type != OBJ_COMMIT)
		return NULL;
	if (repo_parse_commit(repo, (struct commit *)object))
		return NULL;
	return (struct commit *)object;
}

int merge_ort_generic(struct merge_options *opt,
		      const struct object_id *head,
		      const struct object_id *merge,
		      int num_merge_bases,
		      const struct object_id *merge_bases,
		      struct commit **result)
{
	int clean;
	struct lock_file lock = LOCK_INIT;
	struct commit *head_commit = get_ref(opt->repo, head, opt->branch1);
	struct commit *next_commit = get_ref(opt->repo, merge, opt->branch2);
	struct commit_list *ca = NULL;

	if (merge_bases) {
		int i;
		for (i = 0; i < num_merge_bases; ++i) {
			struct commit *base;
			if (!(base = get_ref(opt->repo, &merge_bases[i],
					     oid_to_hex(&merge_bases[i]))))
				return error(_("Could not parse object '%s'"),
					     oid_to_hex(&merge_bases[i]));
			commit_list_insert(base, &ca);
		}
	}

	repo_hold_locked_index(opt->repo, &lock, LOCK_DIE_ON_ERROR);
	clean = merge_ort_recursive(opt, head_commit, next_commit, ca,
				    result);
	free_commit_list(ca);
	if (clean < 0) {
		rollback_lock_file(&lock);
		return clean;
	}

	if (write_locked_index(opt->repo->index, &lock,
			       COMMIT_LOCK | SKIP_IF_UNCHANGED))
		return error(_("Unable to write index."));

	return clean ? 0 : 1;
}
