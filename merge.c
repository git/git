#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "lockfile.h"
#include "commit.h"
#include "run-command.h"
#include "resolve-undo.h"
#include "tree-walk.h"
#include "unpack-trees.h"
#include "dir.h"

static const char *merge_argument(struct commit *commit)
{
	return oid_to_hex(commit ? &commit->object.oid : the_hash_algo->empty_tree);
}

int index_has_changes(struct strbuf *sb)
{
	struct object_id head;
	int i;

	if (!get_oid_tree("HEAD", &head)) {
		struct diff_options opt;

		diff_setup(&opt);
		opt.flags.exit_with_status = 1;
		if (!sb)
			opt.flags.quick = 1;
		do_diff_cache(&head, &opt);
		diffcore_std(&opt);
		for (i = 0; sb && i < diff_queued_diff.nr; i++) {
			if (i)
				strbuf_addch(sb, ' ');
			strbuf_addstr(sb, diff_queued_diff.queue[i]->two->path);
		}
		diff_flush(&opt);
		return opt.flags.has_changes != 0;
	} else {
		for (i = 0; sb && i < active_nr; i++) {
			if (i)
				strbuf_addch(sb, ' ');
			strbuf_addstr(sb, active_cache[i]->name);
		}
		return !!active_nr;
	}
}

int try_merge_command(const char *strategy, size_t xopts_nr,
		      const char **xopts, struct commit_list *common,
		      const char *head_arg, struct commit_list *remotes)
{
	struct argv_array args = ARGV_ARRAY_INIT;
	int i, ret;
	struct commit_list *j;

	argv_array_pushf(&args, "merge-%s", strategy);
	for (i = 0; i < xopts_nr; i++)
		argv_array_pushf(&args, "--%s", xopts[i]);
	for (j = common; j; j = j->next)
		argv_array_push(&args, merge_argument(j->item));
	argv_array_push(&args, "--");
	argv_array_push(&args, head_arg);
	for (j = remotes; j; j = j->next)
		argv_array_push(&args, merge_argument(j->item));

	ret = run_command_v_opt(args.argv, RUN_GIT_CMD);
	argv_array_clear(&args);

	discard_cache();
	if (read_cache() < 0)
		die(_("failed to read the cache"));
	resolve_undo_clear();

	return ret;
}

int checkout_fast_forward(const struct object_id *head,
			  const struct object_id *remote,
			  int overwrite_ignore)
{
	struct tree *trees[MAX_UNPACK_TREES];
	struct unpack_trees_options opts;
	struct tree_desc t[MAX_UNPACK_TREES];
	int i, nr_trees = 0;
	struct dir_struct dir;
	struct lock_file lock_file = LOCK_INIT;

	refresh_cache(REFRESH_QUIET);

	if (hold_locked_index(&lock_file, LOCK_REPORT_ON_ERROR) < 0)
		return -1;

	memset(&trees, 0, sizeof(trees));
	memset(&opts, 0, sizeof(opts));
	memset(&t, 0, sizeof(t));
	if (overwrite_ignore) {
		memset(&dir, 0, sizeof(dir));
		dir.flags |= DIR_SHOW_IGNORED;
		setup_standard_excludes(&dir);
		opts.dir = &dir;
	}

	opts.head_idx = 1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;
	opts.update = 1;
	opts.verbose_update = 1;
	opts.merge = 1;
	opts.fn = twoway_merge;
	setup_unpack_trees_porcelain(&opts, "merge");

	trees[nr_trees] = parse_tree_indirect(head);
	if (!trees[nr_trees++]) {
		rollback_lock_file(&lock_file);
		return -1;
	}
	trees[nr_trees] = parse_tree_indirect(remote);
	if (!trees[nr_trees++]) {
		rollback_lock_file(&lock_file);
		return -1;
	}
	for (i = 0; i < nr_trees; i++) {
		parse_tree(trees[i]);
		init_tree_desc(t+i, trees[i]->buffer, trees[i]->size);
	}
	if (unpack_trees(nr_trees, t, &opts)) {
		rollback_lock_file(&lock_file);
		return -1;
	}
	if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
		return error(_("unable to write new index file"));
	return 0;
}
