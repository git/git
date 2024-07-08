#include "git-compat-util.h"
#include "gettext.h"
#include "hash.h"
#include "merge-ort.h"
#include "merge-ort-wrappers.h"
#include "read-cache-ll.h"
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

	if (unclean(opt, head))
		return -1;

	if (oideq(&merge_base->object.oid, &merge->object.oid)) {
		printf_ln(_("Already up to date."));
		return 1;
	}

	memset(&result, 0, sizeof(result));
	merge_incore_nonrecursive(opt, merge_base, head, merge, &result);
	merge_switch_to_result(opt, head, &result, 1, 1);

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

	if (unclean(opt, head))
		return -1;

	memset(&tmp, 0, sizeof(tmp));
	merge_incore_recursive(opt, merge_bases, side1, side2, &tmp);
	merge_switch_to_result(opt, head, &tmp, 1, 1);
	*result = NULL;

	return tmp.clean;
}
