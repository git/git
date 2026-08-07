#include "git-compat-util.h"
#include "cache-tree.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "object-name.h"
#include "refs.h"
#include "reset.h"
#include "tree-walk.h"
#include "tree.h"
#include "unpack-trees.h"
#include "hook.h"

static int update_refs(struct repository *repo,
		       const struct reset_working_tree_options *opts,
		       const struct object_id *oid,
		       const struct object_id *head)
{
	unsigned detach_head = opts->flags & RESET_WORKING_TREE_DETACH;
	unsigned run_hook = opts->flags & RESET_WORKING_TREE_RUN_POST_CHECKOUT_HOOK;
	unsigned update_orig_head = opts->flags & RESET_WORKING_TREE_UPDATE_ORIG_HEAD;
	const struct object_id *orig_head = opts->orig_head;
	const char *switch_to_branch = opts->branch;
	const char *reflog_branch = opts->branch_msg;
	const char *reflog_head = opts->head_msg;
	const char *reflog_orig_head = opts->orig_head_msg;
	const char *default_reflog_action = opts->default_reflog_action;
	struct object_id *old_orig = NULL, oid_old_orig;
	struct strbuf msg = STRBUF_INIT;
	const char *reflog_action;
	size_t prefix_len;
	int ret;

	if ((update_orig_head && !reflog_orig_head) || !reflog_head) {
		if (!default_reflog_action)
			BUG("default_reflog_action must be given when reflog messages are omitted");
		reflog_action = getenv(GIT_REFLOG_ACTION_ENVIRONMENT);
		strbuf_addf(&msg, "%s: ", reflog_action ? reflog_action :
							  default_reflog_action);
	}
	prefix_len = msg.len;

	if (update_orig_head) {
		if (!repo_get_oid(repo, "ORIG_HEAD", &oid_old_orig))
			old_orig = &oid_old_orig;
		if (head) {
			if (!reflog_orig_head) {
				strbuf_addstr(&msg, "updating ORIG_HEAD");
				reflog_orig_head = msg.buf;
			}
			refs_update_ref(get_main_ref_store(repo),
					reflog_orig_head, "ORIG_HEAD",
					orig_head ? orig_head : head,
					old_orig, 0, UPDATE_REFS_MSG_ON_ERR);
		} else if (old_orig)
			refs_delete_ref(get_main_ref_store(repo),
					NULL, "ORIG_HEAD", old_orig, 0);
	}

	if (!reflog_head) {
		strbuf_setlen(&msg, prefix_len);
		strbuf_addstr(&msg, "updating HEAD");
		reflog_head = msg.buf;
	}
	if (!switch_to_branch)
		ret = refs_update_ref(get_main_ref_store(repo),
				      reflog_head, "HEAD", oid, head,
				      detach_head ? REF_NO_DEREF : 0,
				      UPDATE_REFS_MSG_ON_ERR);
	else {
		ret = refs_update_ref(get_main_ref_store(repo),
				      reflog_branch ? reflog_branch : reflog_head,
				      switch_to_branch, oid, NULL, 0,
				      UPDATE_REFS_MSG_ON_ERR);
		if (!ret)
			ret = refs_update_symref(get_main_ref_store(repo),
						 "HEAD", switch_to_branch,
						 reflog_head);
	}
	if (!ret && run_hook)
		run_hooks_l(repo, "post-checkout",
			    oid_to_hex(head ? head : null_oid(repo->hash_algo)),
			    oid_to_hex(oid), "1", NULL);
	strbuf_release(&msg);
	return ret;
}

int reset_working_tree(struct repository *r,
		       const struct reset_working_tree_options *opts)
{
	const struct object_id *oid = opts->oid;
	const char *switch_to_branch = opts->branch;
	unsigned reset_hard = opts->flags & RESET_WORKING_TREE_HARD;
	unsigned refs_only = opts->flags & RESET_WORKING_TREE_REFS_ONLY;
	unsigned update_head = opts->flags & RESET_WORKING_TREE_UPDATE_HEAD;
	unsigned update_orig_head = opts->flags & RESET_WORKING_TREE_UPDATE_ORIG_HEAD;
	unsigned dry_run = opts->flags & RESET_WORKING_TREE_DRY_RUN;
	struct object_id *head = NULL, head_oid;
	struct tree_desc desc[2] = { { NULL }, { NULL } };
	struct lock_file lock = LOCK_INIT;
	struct unpack_trees_options unpack_tree_opts = { 0 };
	struct tree *tree;
	struct index_state scratch_index = INDEX_STATE_INIT(r);
	struct index_state *istate;
	const char *action;
	int ret = 0, nr = 0;

	if (switch_to_branch && !starts_with(switch_to_branch, "refs/"))
		BUG("Not a fully qualified branch: '%s'", switch_to_branch);

	if (opts->orig_head_msg && !update_orig_head)
		BUG("ORIG_HEAD reflog message given without updating ORIG_HEAD");

	if (opts->branch_msg && !opts->branch)
		BUG("branch reflog message given without a branch");

	if (update_orig_head && !update_head)
		BUG("cannot update ORIG_HEAD without updating HEAD");

	if (!refs_only && !dry_run && repo_hold_locked_index(r, &lock, LOCK_REPORT_ON_ERROR) < 0) {
		ret = -1;
		goto leave_reset_head;
	}

	if (opts->oid_from) {
		oidcpy(&head_oid, opts->oid_from);
		head = &head_oid;
	} else if (!repo_get_oid(r, "HEAD", &head_oid)) {
		head = &head_oid;
	} else if (!oid || !reset_hard) {
		ret = error(_("could not determine HEAD revision"));
		goto leave_reset_head;
	}

	if (!oid)
		oid = &head_oid;

	if (refs_only) {
		if (!dry_run && update_head)
			return update_refs(r, opts, oid, head);
		return 0;
	}

	if (dry_run) {
		if (read_index_from(&scratch_index, r->index_file, r->gitdir) < 0 ||
		    index_state_unmerged_to_stage0(&scratch_index) < 0) {
			ret = error(_("could not read index"));
			goto leave_reset_head;
		}

		istate = &scratch_index;
	} else {
		if (repo_read_index_unmerged(r) < 0) {
			ret = error(_("could not read index"));
			goto leave_reset_head;
		}
		istate = r->index;
	}

	action = reset_hard ? "reset" : "checkout";
	setup_unpack_trees_porcelain(&unpack_tree_opts, action);
	unpack_tree_opts.head_idx = 1;
	unpack_tree_opts.src_index = istate;
	unpack_tree_opts.dst_index = istate;
	unpack_tree_opts.fn = reset_hard ? oneway_merge : twoway_merge;
	unpack_tree_opts.update = !dry_run;
	unpack_tree_opts.dry_run = dry_run;
	unpack_tree_opts.merge = 1;
	unpack_tree_opts.preserve_ignored = 0; /* FIXME: !overwrite_ignore */
	init_checkout_metadata(&unpack_tree_opts.meta, switch_to_branch, oid, NULL);
	if (reset_hard) {
		unpack_tree_opts.skip_cache_tree_update = 1;
		unpack_tree_opts.reset = UNPACK_RESET_PROTECT_UNTRACKED;
	}

	if (!reset_hard && !fill_tree_descriptor(r, &desc[nr++], &head_oid)) {
		ret = error(_("failed to find tree of %s"),
			    oid_to_hex(&head_oid));
		goto leave_reset_head;
	}

	if (!fill_tree_descriptor(r, &desc[nr++], oid)) {
		ret = error(_("failed to find tree of %s"), oid_to_hex(oid));
		goto leave_reset_head;
	}

	if (unpack_trees(nr, desc, &unpack_tree_opts)) {
		ret = -1;
		goto leave_reset_head;
	}

	if (dry_run)
		goto leave_reset_head;

	tree = repo_parse_tree_indirect(r, oid);
	if (!tree) {
		ret = error(_("unable to read tree (%s)"), oid_to_hex(oid));
		goto leave_reset_head;
	}

	if (reset_hard)
		prime_cache_tree(r, r->index, tree);

	if (write_locked_index(r->index, &lock, COMMIT_LOCK) < 0) {
		ret = error(_("could not write index"));
		goto leave_reset_head;
	}

	if (update_head &&
	    (oid != &head_oid || update_orig_head || switch_to_branch))
		ret = update_refs(r, opts, oid, head);

leave_reset_head:
	rollback_lock_file(&lock);
	clear_unpack_trees_porcelain(&unpack_tree_opts);
	release_index(&scratch_index);
	while (nr)
		free((void *)desc[--nr].buffer);
	return ret;

}
