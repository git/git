#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "copy.h"
#include "dir.h"
#include "dir-iterator.h"
#include "iterator.h"
#include "gettext.h"
#include "parse-options.h"
#include "path.h"
#include "refs.h"
#include "run-command.h"
#include "string-list.h"
#include "wt-status.h"

static const char * const builtin_branch2_usage[] = {
	N_("git branch2 <branch-name>"),
	N_("git branch2 <branch-name> --sync"),
	N_("git branch2 <branch-name> --fetch"),
	NULL
};

static void branch2_checkout_branch(struct repository *repo,
				    const char *name,
				    const char *refname);

static int branch2_remove_existing_path(const char *path)
{
	struct stat st;
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;

	if (lstat(path, &st)) {
		if (errno == ENOENT)
			return 0;
		return error_errno(_("could not inspect '%s'"), path);
	}

	if (S_ISDIR(st.st_mode)) {
		strbuf_addstr(&buf, path);
		if (remove_dir_recursively(&buf, 0))
			ret = error_errno(_("could not remove directory '%s'"), path);
	} else if (unlink(path) && errno != ENOENT) {
		ret = error_errno(_("could not remove path '%s'"), path);
	}

	strbuf_release(&buf);
	return ret;
}

static int branch2_ensure_directory(struct repository *repo, const char *path)
{
	struct stat st;

	if (!lstat(path, &st)) {
		if (S_ISDIR(st.st_mode))
			return 0;
		if (branch2_remove_existing_path(path))
			return -1;
	} else if (errno != ENOENT) {
		return error_errno(_("could not inspect '%s'"), path);
	}

	safe_create_dir(repo, path, 1);
	return 0;
}

static int branch2_copy_symlink(const char *src, const char *dst,
				const struct stat *st)
{
	struct strbuf target = STRBUF_INIT;

	if (safe_create_leading_directories_const(the_repository, dst))
		return error_errno(_("could not create leading directories of '%s'"),
				   dst);
	if (strbuf_readlink(&target, src, st->st_size) < 0) {
		strbuf_release(&target);
		return error_errno(_("could not read symlink '%s'"), src);
	}
	if (branch2_remove_existing_path(dst)) {
		strbuf_release(&target);
		return -1;
	}
	if (symlink(target.buf, dst) < 0) {
		strbuf_release(&target);
		return error_errno(_("could not create symlink '%s'"), dst);
	}
	strbuf_release(&target);
	return 0;
}

static int branch2_copy_tree(struct repository *repo, const char *src_root,
			     const char *dst_root, int skip_git)
{
	struct dir_iterator *iter;
	struct strbuf dst = STRBUF_INIT;
	int iter_status, ret = 0;

	iter = dir_iterator_begin(src_root, DIR_ITERATOR_PEDANTIC);
	if (!iter)
		return error_errno(_("could not iterate tree '%s'"), src_root);

	while ((iter_status = dir_iterator_advance(iter)) == ITER_OK) {
		if (skip_git &&
		    (!strcmp(iter->relative_path, ".git") ||
		     starts_with(iter->relative_path, ".git/")))
			continue;

		strbuf_reset(&dst);
		strbuf_addf(&dst, "%s/%s", dst_root, iter->relative_path);

		if (S_ISDIR(iter->st.st_mode)) {
			if (branch2_ensure_directory(repo, dst.buf)) {
				ret = -1;
				break;
			}
			continue;
		}

		if (S_ISREG(iter->st.st_mode)) {
			if (safe_create_leading_directories_const(repo, dst.buf) ||
			    branch2_remove_existing_path(dst.buf) ||
			    copy_file(dst.buf, iter->path.buf, iter->st.st_mode)) {
				ret = error_errno(_("could not copy '%s' to '%s'"),
						  iter->path.buf, dst.buf);
				break;
			}
			continue;
		}

		if (S_ISLNK(iter->st.st_mode)) {
			ret = branch2_copy_symlink(iter->path.buf, dst.buf, &iter->st);
			if (ret)
				break;
			continue;
		}

		ret = error(_("unsupported path type for '%s'"), iter->relative_path);
		break;
	}

	if (iter_status != ITER_DONE && !ret)
		ret = error(_("error while iterating over the worktree"));

	dir_iterator_free(iter);
	strbuf_release(&dst);
	return ret;
}

static int branch2_copy_worktree(struct repository *repo, const char *dst_root)
{
	return branch2_copy_tree(repo, repo->worktree, dst_root, 1);
}

static int branch2_path_has_removed_ancestor(const struct string_list *paths,
					     const char *relative_path)
{
	size_t i;

	for (i = 0; i < paths->nr; i++) {
		const char *candidate = paths->items[i].string;
		size_t len = strlen(candidate);

		if (strncmp(relative_path, candidate, len))
			continue;
		if (relative_path[len] == '/')
			return 1;
	}

	return 0;
}

static int branch2_collect_missing_paths(struct repository *repo,
					 const char *src_root,
					 struct string_list *paths)
{
	struct dir_iterator *iter;
	struct strbuf src = STRBUF_INIT;
	int iter_status, ret = 0;

	iter = dir_iterator_begin(repo->worktree, DIR_ITERATOR_PEDANTIC);
	if (!iter)
		return error_errno(_("could not iterate worktree '%s'"),
				   repo->worktree);

	while ((iter_status = dir_iterator_advance(iter)) == ITER_OK) {
		struct stat st;

		if (!strcmp(iter->relative_path, ".git") ||
		    starts_with(iter->relative_path, ".git/"))
			continue;
		if (branch2_path_has_removed_ancestor(paths, iter->relative_path))
			continue;

		strbuf_reset(&src);
		strbuf_addf(&src, "%s/%s", src_root, iter->relative_path);
		if (!lstat(src.buf, &st))
			continue;
		if (errno != ENOENT) {
			ret = error_errno(_("could not inspect '%s'"), src.buf);
			break;
		}

		string_list_append(paths, iter->relative_path);
	}

	if (iter_status != ITER_DONE && !ret)
		ret = error(_("error while iterating over the worktree"));

	dir_iterator_free(iter);
	strbuf_release(&src);
	return ret;
}

static int branch2_remove_paths_from_worktree(const struct string_list *paths)
{
	size_t i;

	for (i = 0; i < paths->nr; i++) {
		struct strbuf path = STRBUF_INIT;
		int ret;

		strbuf_addf(&path, "%s/%s", the_repository->worktree,
			    paths->items[i].string);
		ret = branch2_remove_existing_path(path.buf);
		strbuf_release(&path);
		if (ret)
			return ret;
	}

	return 0;
}

static int branch2_sync_snapshot(struct repository *repo, const char *name,
				 const char *refname, const char *snapshot_root)
{
	struct string_list removals = STRING_LIST_INIT_DUP;
	const char *head_ref;
	int flags;
	int ret;

	branch2_checkout_branch(repo, name, refname);
	head_ref = refs_resolve_ref_unsafe(get_main_ref_store(repo), "HEAD", 0,
					   NULL, &flags);
	if (!head_ref || !(flags & REF_ISSYMREF))
		die(_("branch2 sync requires a checked out branch"));
	if (strcmp(head_ref, refname))
		die(_("branch2 sync requires the current branch to be '%s'"), name);

	if (require_clean_work_tree(repo, "branch2 sync", NULL, 1, 1))
		die(_("branch2 sync requires a clean worktree"));

	ret = branch2_collect_missing_paths(repo, snapshot_root, &removals);
	if (!ret)
		ret = branch2_remove_paths_from_worktree(&removals);
	if (!ret)
		ret = branch2_copy_tree(repo, snapshot_root, repo->worktree, 0);

	string_list_clear(&removals, 0);
	return ret;
}

static void branch2_checkout_branch(struct repository *repo,
				    const char *name,
				    const char *refname)
{
	const char *head_ref;
	int flags;
	struct child_process cp = CHILD_PROCESS_INIT;

	if (!refs_ref_exists(get_main_ref_store(repo), refname))
		die(_("branch '%s' does not exist"), name);

	head_ref = refs_resolve_ref_unsafe(get_main_ref_store(repo), "HEAD", 0,
					   NULL, &flags);
	if (head_ref && (flags & REF_ISSYMREF) && !strcmp(head_ref, refname))
		return;

	if (require_clean_work_tree(repo, "branch2 checkout", NULL, 1, 1))
		die(_("branch2 checkout requires a clean worktree"));

	cp.git_cmd = 1;
	cp.dir = repo->worktree;
	cp.no_stdin = 1;
	strvec_pushl(&cp.args, "checkout", name, NULL);
	if (run_command(&cp))
		die(_("could not check out branch '%s'"), name);

	head_ref = refs_resolve_ref_unsafe(get_main_ref_store(repo), "HEAD", 0,
					   NULL, &flags);
	if (!head_ref || !(flags & REF_ISSYMREF) || strcmp(head_ref, refname))
		die(_("branch2 checkout did not end on branch '%s'"), name);
}

static int branch2_fetch_snapshot(struct repository *repo, const char *name,
				  const char *refname,
				  const char *snapshot_root)
{
	struct stat st;
	int ret = 0;

	branch2_checkout_branch(repo, name, refname);

	if (!lstat(snapshot_root, &st)) {
		if (!S_ISDIR(st.st_mode))
			die(_("branch2 destination '%s' is not a directory"), snapshot_root);
		if (branch2_remove_existing_path(snapshot_root))
			ret = -1;
	} else if (errno != ENOENT) {
		ret = error_errno(_("could not inspect '%s'"), snapshot_root);
	}

	if (!ret) {
		if (safe_create_leading_directories_const(repo, snapshot_root))
			ret = error_errno(_("could not create leading directories of '%s'"),
					  snapshot_root);
		else {
			safe_create_dir(repo, snapshot_root, 1);
			ret = branch2_copy_worktree(repo, snapshot_root);
		}
	}
	return ret;
}

int cmd_branch2(int argc, const char **argv, const char *prefix,
		struct repository *repo)
{
	struct strbuf ref = STRBUF_INIT;
	struct strbuf dst_root = STRBUF_INIT;
	int fetch = 0;
	int sync = 0;
	struct option options[] = {
		OPT_BOOL(0, "fetch", &fetch,
			 N_("refresh branch2/<name> from the checked out Git branch <name>")),
		OPT_BOOL(0, "sync", &sync,
			 N_("sync branch2/<name> into the checked out Git branch <name>")),
		OPT_END(),
	};
	struct stat st;
	const char *name;
	int ret;

	argc = parse_options(argc, argv, prefix, options, builtin_branch2_usage, 0);
	if (argc != 1)
		usage_with_options(builtin_branch2_usage, options);
	if (sync && fetch)
		die(_("options '--sync' and '--fetch' cannot be used together"));

	name = argv[0];
	if (check_branch_ref(&ref, name))
		die(_("'%s' is not a valid branch2 name"), name);

	strbuf_addf(&dst_root, "%s/branch2/%s", repo->gitdir, name);
	if (fetch) {
		ret = branch2_fetch_snapshot(repo, name, ref.buf, dst_root.buf);
	} else if (sync) {
		if (lstat(dst_root.buf, &st)) {
			if (errno == ENOENT)
				die(_("branch2 source '%s' does not exist"), dst_root.buf);
			die_errno(_("could not inspect '%s'"), dst_root.buf);
		}
		if (!S_ISDIR(st.st_mode))
			die(_("branch2 source '%s' is not a directory"), dst_root.buf);
		ret = branch2_sync_snapshot(repo, name, ref.buf, dst_root.buf);
	} else {
		if (!lstat(dst_root.buf, &st))
			die(_("branch2 destination '%s' already exists"), dst_root.buf);
		if (errno != ENOENT)
			die_errno(_("could not inspect '%s'"), dst_root.buf);
		branch2_checkout_branch(repo, name, ref.buf);
		if (safe_create_leading_directories_const(repo, dst_root.buf))
			die_errno(_("could not create leading directories of '%s'"),
				  dst_root.buf);
		safe_create_dir(repo, dst_root.buf, 1);
		ret = branch2_copy_worktree(repo, dst_root.buf);
	}

	strbuf_release(&dst_root);
	strbuf_release(&ref);
	return ret;
}
