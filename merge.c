#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "gettext.h"
#include "hash.h"
#include "hex.h"
#include "lockfile.h"
#include "merge.h"
#include "commit.h"
#include "repository.h"
#include "path.h"
#include "run-command.h"
#include "resolve-undo.h"
#include "tree.h"
#include "tree-walk.h"
#include "unpack-trees.h"

static const char *merge_argument(struct commit *commit)
{
	return oid_to_hex(commit ? &commit->object.oid : the_hash_algo->empty_tree);
}

int try_merge_command(struct repository *r,
		      const char *strategy, size_t xopts_nr,
		      const char **xopts, struct commit_list *common,
		      const char *head_arg, struct commit_list *remotes)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int ret;
	struct commit_list *j;

	strvec_pushf(&cmd.args, "merge-%s", strategy);
	for (size_t i = 0; i < xopts_nr; i++)
		strvec_pushf(&cmd.args, "--%s", xopts[i]);
	for (j = common; j; j = j->next)
		strvec_push(&cmd.args, merge_argument(j->item));
	strvec_push(&cmd.args, "--");
	strvec_push(&cmd.args, head_arg);
	for (j = remotes; j; j = j->next)
		strvec_push(&cmd.args, merge_argument(j->item));

	cmd.git_cmd = 1;
	ret = run_command(&cmd);

	discard_index(r->index);
	if (repo_read_index(r) < 0)
		die(_("failed to read the cache"));
	resolve_undo_clear_index(r->index);

	return ret;
}

int checkout_fast_forward(struct repository *r,
			  const struct object_id *head,
			  const struct object_id *remote,
			  int overwrite_ignore)
{
	struct tree *trees[MAX_UNPACK_TREES];
	struct unpack_trees_options opts;
	struct tree_desc t[MAX_UNPACK_TREES];
	int i, nr_trees = 0;
	struct lock_file lock_file = LOCK_INIT;

	refresh_index(r->index, REFRESH_QUIET, NULL, NULL, NULL);

	if (repo_hold_locked_index(r, &lock_file, LOCK_REPORT_ON_ERROR) < 0)
		return -1;

	memset(&trees, 0, sizeof(trees));
	memset(&t, 0, sizeof(t));

	trees[nr_trees] = repo_parse_tree_indirect(the_repository, head);
	if (!trees[nr_trees++]) {
		rollback_lock_file(&lock_file);
		return -1;
	}
	trees[nr_trees] = repo_parse_tree_indirect(the_repository, remote);
	if (!trees[nr_trees++]) {
		rollback_lock_file(&lock_file);
		return -1;
	}
	for (i = 0; i < nr_trees; i++) {
		if (repo_parse_tree(the_repository, trees[i]) < 0) {
			rollback_lock_file(&lock_file);
			return -1;
		}
		init_tree_desc(t+i, &trees[i]->object.oid,
			       trees[i]->buffer, trees[i]->size);
	}

	memset(&opts, 0, sizeof(opts));
	opts.preserve_ignored = !overwrite_ignore;

	opts.head_idx = 1;
	opts.src_index = r->index;
	opts.dst_index = r->index;
	opts.update = 1;
	opts.verbose_update = 1;
	opts.merge = 1;
	opts.fn = twoway_merge;
	init_checkout_metadata(&opts.meta, NULL, remote, NULL);
	setup_unpack_trees_porcelain(&opts, "merge");

	if (unpack_trees(nr_trees, t, &opts)) {
		rollback_lock_file(&lock_file);
		clear_unpack_trees_porcelain(&opts);
		return -1;
	}
	clear_unpack_trees_porcelain(&opts);

	if (write_locked_index(r->index, &lock_file, COMMIT_LOCK))
		return error(_("unable to write new index file"));
	return 0;
}

int write_merge_labels(struct repository *r, const char *base,
			  const char *ours, const char *theirs)
{
	FILE *f = fopen_or_warn(git_path_merge_labels(r), "w");

	if (!f)
		return -1;

	fprintf(f, "%s\n%s\n%s\n", base, ours, theirs);
	if (fclose(f))
		return error_errno("could not write '%s'",
				   git_path_merge_labels(r));

	return 0;
}

static int parse_merge_label_line(const char **p, char **line)
{
	const char *eol = strchr(*p, '\n');

	if (!eol)
		return -1;

	*line = xmemdupz(*p, eol - *p);
	*p = eol + 1;

	return 0;
}

int read_merge_labels(struct repository *r,
		      char **pbase, char** pours, char** ptheirs)
{
	struct strbuf buf = STRBUF_INIT;
	const char *p;
	char *base = NULL, *ours = NULL, *theirs = NULL;
	int ret = -1;

	if (strbuf_read_file(&buf, git_path_merge_labels(r), 0) < 0)
		return -1;

	p = buf.buf;
	if (parse_merge_label_line(&p, &base))
		goto out;
	if (parse_merge_label_line(&p, &ours))
		goto out;
	if (parse_merge_label_line(&p, &theirs))
		goto out;
	ret = 0;
	*pbase = base;
	*pours = ours;
	*ptheirs = theirs;
out:
	if (ret) {
		free(base);
		free(ours);
		free(theirs);
	}
	strbuf_release(&buf);

	return ret;
}
