#include "git-compat-util.h"
#include "cache-tree.h"
#include "dir.h"
#include "lockfile.h"
#include "refs.h"
#include "reset.h"
#include "run-command.h"
#include "tree-walk.h"
#include "tree.h"
#include "unpack-trees.h"

int reset_head(struct repository *r, struct object_id *oid, const char *action,
	       const char *switch_to_branch, unsigned flags,
	       const char *reflog_orig_head, const char *reflog_head,
	       const char *default_reflog_action)
{
	unsigned detach_head = flags & RESET_HEAD_DETACH;
	unsigned reset_hard = flags & RESET_HEAD_HARD;
	unsigned run_hook = flags & RESET_HEAD_RUN_POST_CHECKOUT_HOOK;
	unsigned refs_only = flags & RESET_HEAD_REFS_ONLY;
	unsigned update_orig_head = flags & RESET_ORIG_HEAD;
	struct object_id head_oid;
	struct tree_desc desc[2] = { { NULL }, { NULL } };
	struct lock_file lock = LOCK_INIT;
	struct unpack_trees_options unpack_tree_opts = { 0 };
	struct tree *tree;
	const char *reflog_action;
	struct strbuf msg = STRBUF_INIT;
	size_t prefix_len;
	struct object_id *orig = NULL, oid_orig,
		*old_orig = NULL, oid_old_orig;
	int ret = 0, nr = 0;

	if (switch_to_branch && !starts_with(switch_to_branch, "refs/"))
		BUG("Not a fully qualified branch: '%s'", switch_to_branch);

	if (!refs_only && repo_hold_locked_index(r, &lock, LOCK_REPORT_ON_ERROR) < 0) {
		ret = -1;
		goto leave_reset_head;
	}

	if ((!oid || !reset_hard) && get_oid("HEAD", &head_oid)) {
		ret = error(_("could not determine HEAD revision"));
		goto leave_reset_head;
	}

	if (!oid)
		oid = &head_oid;

	if (refs_only)
		goto reset_head_refs;

	setup_unpack_trees_porcelain(&unpack_tree_opts, action);
	unpack_tree_opts.head_idx = 1;
	unpack_tree_opts.src_index = r->index;
	unpack_tree_opts.dst_index = r->index;
	unpack_tree_opts.fn = reset_hard ? oneway_merge : twoway_merge;
	unpack_tree_opts.update = 1;
	unpack_tree_opts.merge = 1;
	init_checkout_metadata(&unpack_tree_opts.meta, switch_to_branch, oid, NULL);
	if (!detach_head) {
		unpack_tree_opts.reset_keep_untracked = 1;
		unpack_tree_opts.dir = xcalloc(1, sizeof(*unpack_tree_opts.dir));
		unpack_tree_opts.dir->flags |= DIR_SHOW_IGNORED;
		setup_standard_excludes(unpack_tree_opts.dir);
	}

	if (repo_read_index_unmerged(r) < 0) {
		ret = error(_("could not read index"));
		goto leave_reset_head;
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

	tree = parse_tree_indirect(oid);
	prime_cache_tree(r, r->index, tree);

	if (write_locked_index(r->index, &lock, COMMIT_LOCK) < 0) {
		ret = error(_("could not write index"));
		goto leave_reset_head;
	}

reset_head_refs:
	reflog_action = getenv(GIT_REFLOG_ACTION_ENVIRONMENT);
	strbuf_addf(&msg, "%s: ", reflog_action ? reflog_action : default_reflog_action);
	prefix_len = msg.len;

	if (update_orig_head) {
		if (!get_oid("ORIG_HEAD", &oid_old_orig))
			old_orig = &oid_old_orig;
		if (!get_oid("HEAD", &oid_orig)) {
			orig = &oid_orig;
			if (!reflog_orig_head) {
				strbuf_addstr(&msg, "updating ORIG_HEAD");
				reflog_orig_head = msg.buf;
			}
			update_ref(reflog_orig_head, "ORIG_HEAD", orig,
				   old_orig, 0, UPDATE_REFS_MSG_ON_ERR);
		} else if (old_orig)
			delete_ref(NULL, "ORIG_HEAD", old_orig, 0);
	}

	if (!reflog_head) {
		strbuf_setlen(&msg, prefix_len);
		strbuf_addstr(&msg, "updating HEAD");
		reflog_head = msg.buf;
	}
	if (!switch_to_branch)
		ret = update_ref(reflog_head, "HEAD", oid, orig,
				 detach_head ? REF_NO_DEREF : 0,
				 UPDATE_REFS_MSG_ON_ERR);
	else {
		ret = update_ref(reflog_head, switch_to_branch, oid,
				 NULL, 0, UPDATE_REFS_MSG_ON_ERR);
		if (!ret)
			ret = create_symref("HEAD", switch_to_branch,
					    reflog_head);
	}
	if (run_hook)
		run_hook_le(NULL, "post-checkout",
			    oid_to_hex(orig ? orig : null_oid()),
			    oid_to_hex(oid), "1", NULL);

leave_reset_head:
	strbuf_release(&msg);
	rollback_lock_file(&lock);
	clear_unpack_trees_porcelain(&unpack_tree_opts);
	while (nr)
		free((void *)desc[--nr].buffer);
	return ret;

}
