#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "abspath.h"
#include "environment.h"
#include "gettext.h"
#include "path.h"
#include "repository.h"
#include "refs.h"
#include "setup.h"
#include "strbuf.h"
#include "worktree.h"
#include "dir.h"
#include "wt-status.h"
#include "config.h"

void free_worktree(struct worktree *worktree)
{
	if (!worktree)
		return;
	free(worktree->path);
	free(worktree->id);
	free(worktree->head_ref);
	free(worktree->lock_reason);
	free(worktree->prune_reason);
	free(worktree);
}

void free_worktrees(struct worktree **worktrees)
{
	int i = 0;
	for (i = 0; worktrees[i]; i++)
		free_worktree(worktrees[i]);
	free (worktrees);
}

/**
 * Update head_oid, head_ref and is_detached of the given worktree
 */
static void add_head_info(struct worktree *wt)
{
	int flags;
	const char *target;

	target = refs_resolve_ref_unsafe(get_worktree_ref_store(wt),
					 "HEAD",
					 0,
					 &wt->head_oid, &flags);
	if (!target)
		return;

	if (flags & REF_ISSYMREF)
		wt->head_ref = xstrdup(target);
	else
		wt->is_detached = 1;
}

static int is_current_worktree(struct worktree *wt)
{
	char *git_dir = absolute_pathdup(repo_get_git_dir(the_repository));
	const char *wt_git_dir = get_worktree_git_dir(wt);
	int is_current = !fspathcmp(git_dir, absolute_path(wt_git_dir));
	free(git_dir);
	return is_current;
}

/**
 * get the main worktree
 */
static struct worktree *get_main_worktree(int skip_reading_head)
{
	struct worktree *worktree = NULL;
	struct strbuf worktree_path = STRBUF_INIT;

	strbuf_add_real_path(&worktree_path, repo_get_common_dir(the_repository));
	strbuf_strip_suffix(&worktree_path, "/.git");

	CALLOC_ARRAY(worktree, 1);
	worktree->repo = the_repository;
	worktree->path = strbuf_detach(&worktree_path, NULL);
	/*
	 * NEEDSWORK: If this function is called from a secondary worktree and
	 * config.worktree is present, is_bare_repository_cfg will reflect the
	 * contents of config.worktree, not the contents of the main worktree.
	 * This means that worktree->is_bare may be set to 0 even if the main
	 * worktree is configured to be bare.
	 */
	worktree->is_bare = (is_bare_repository_cfg == 1) ||
		is_bare_repository();
	worktree->is_current = is_current_worktree(worktree);
	if (!skip_reading_head)
		add_head_info(worktree);
	return worktree;
}

struct worktree *get_linked_worktree(const char *id,
				     int skip_reading_head)
{
	struct worktree *worktree = NULL;
	struct strbuf path = STRBUF_INIT;
	struct strbuf worktree_path = STRBUF_INIT;

	if (!id)
		die("Missing linked worktree name");

	strbuf_git_common_path(&path, the_repository, "worktrees/%s/gitdir", id);
	if (strbuf_read_file(&worktree_path, path.buf, 0) <= 0)
		/* invalid gitdir file */
		goto done;
	strbuf_rtrim(&worktree_path);
	strbuf_strip_suffix(&worktree_path, "/.git");

	CALLOC_ARRAY(worktree, 1);
	worktree->repo = the_repository;
	worktree->path = strbuf_detach(&worktree_path, NULL);
	worktree->id = xstrdup(id);
	worktree->is_current = is_current_worktree(worktree);
	if (!skip_reading_head)
		add_head_info(worktree);

done:
	strbuf_release(&path);
	strbuf_release(&worktree_path);
	return worktree;
}

/*
 * NEEDSWORK: This function exists so that we can look up metadata of a
 * worktree without trying to access any of its internals like the refdb. It
 * would be preferable to instead have a corruption-tolerant function for
 * retrieving worktree metadata that could be used when the worktree is known
 * to not be in a healthy state, e.g. when creating or repairing it.
 */
static struct worktree **get_worktrees_internal(int skip_reading_head)
{
	struct worktree **list = NULL;
	struct strbuf path = STRBUF_INIT;
	DIR *dir;
	struct dirent *d;
	int counter = 0, alloc = 2;

	ALLOC_ARRAY(list, alloc);

	list[counter++] = get_main_worktree(skip_reading_head);

	strbuf_addf(&path, "%s/worktrees", repo_get_common_dir(the_repository));
	dir = opendir(path.buf);
	strbuf_release(&path);
	if (dir) {
		while ((d = readdir_skip_dot_and_dotdot(dir)) != NULL) {
			struct worktree *linked = NULL;

			if ((linked = get_linked_worktree(d->d_name, skip_reading_head))) {
				ALLOC_GROW(list, counter + 1, alloc);
				list[counter++] = linked;
			}
		}
		closedir(dir);
	}
	ALLOC_GROW(list, counter + 1, alloc);
	list[counter] = NULL;

	return list;
}

struct worktree **get_worktrees(void)
{
	return get_worktrees_internal(0);
}

const char *get_worktree_git_dir(const struct worktree *wt)
{
	if (!wt)
		return repo_get_git_dir(the_repository);
	else if (!wt->id)
		return repo_get_common_dir(the_repository);
	else
		return git_common_path("worktrees/%s", wt->id);
}

static struct worktree *find_worktree_by_suffix(struct worktree **list,
						const char *suffix)
{
	struct worktree *found = NULL;
	int nr_found = 0, suffixlen;

	suffixlen = strlen(suffix);
	if (!suffixlen)
		return NULL;

	for (; *list && nr_found < 2; list++) {
		const char	*path	 = (*list)->path;
		int		 pathlen = strlen(path);
		int		 start	 = pathlen - suffixlen;

		/* suffix must start at directory boundary */
		if ((!start || (start > 0 && is_dir_sep(path[start - 1]))) &&
		    !fspathcmp(suffix, path + start)) {
			found = *list;
			nr_found++;
		}
	}
	return nr_found == 1 ? found : NULL;
}

struct worktree *find_worktree(struct worktree **list,
			       const char *prefix,
			       const char *arg)
{
	struct worktree *wt;
	char *to_free = NULL;

	if ((wt = find_worktree_by_suffix(list, arg)))
		return wt;

	if (prefix)
		arg = to_free = prefix_filename(prefix, arg);
	wt = find_worktree_by_path(list, arg);
	free(to_free);
	return wt;
}

struct worktree *find_worktree_by_path(struct worktree **list, const char *p)
{
	struct strbuf wt_path = STRBUF_INIT;
	char *path = real_pathdup(p, 0);

	if (!path)
		return NULL;
	for (; *list; list++) {
		if (!strbuf_realpath(&wt_path, (*list)->path, 0))
			continue;

		if (!fspathcmp(path, wt_path.buf))
			break;
	}
	free(path);
	strbuf_release(&wt_path);
	return *list;
}

int is_main_worktree(const struct worktree *wt)
{
	return !wt->id;
}

const char *worktree_lock_reason(struct worktree *wt)
{
	if (is_main_worktree(wt))
		return NULL;

	if (!wt->lock_reason_valid) {
		struct strbuf path = STRBUF_INIT;

		strbuf_addstr(&path, worktree_git_path(the_repository, wt, "locked"));
		if (file_exists(path.buf)) {
			struct strbuf lock_reason = STRBUF_INIT;
			if (strbuf_read_file(&lock_reason, path.buf, 0) < 0)
				die_errno(_("failed to read '%s'"), path.buf);
			strbuf_trim(&lock_reason);
			wt->lock_reason = strbuf_detach(&lock_reason, NULL);
		} else
			wt->lock_reason = NULL;
		wt->lock_reason_valid = 1;
		strbuf_release(&path);
	}

	return wt->lock_reason;
}

const char *worktree_prune_reason(struct worktree *wt, timestamp_t expire)
{
	struct strbuf reason = STRBUF_INIT;
	char *path = NULL;

	if (is_main_worktree(wt))
		return NULL;
	if (wt->prune_reason_valid)
		return wt->prune_reason;

	if (should_prune_worktree(wt->id, &reason, &path, expire))
		wt->prune_reason = strbuf_detach(&reason, NULL);
	wt->prune_reason_valid = 1;

	strbuf_release(&reason);
	free(path);
	return wt->prune_reason;
}

/* convenient wrapper to deal with NULL strbuf */
__attribute__((format (printf, 2, 3)))
static void strbuf_addf_gently(struct strbuf *buf, const char *fmt, ...)
{
	va_list params;

	if (!buf)
		return;

	va_start(params, fmt);
	strbuf_vaddf(buf, fmt, params);
	va_end(params);
}

int validate_worktree(const struct worktree *wt, struct strbuf *errmsg,
		      unsigned flags)
{
	struct strbuf wt_path = STRBUF_INIT;
	struct strbuf realpath = STRBUF_INIT;
	char *path = NULL;
	int err, ret = -1;

	strbuf_addf(&wt_path, "%s/.git", wt->path);

	if (is_main_worktree(wt)) {
		if (is_directory(wt_path.buf)) {
			ret = 0;
			goto done;
		}
		/*
		 * Main worktree using .git file to point to the
		 * repository would make it impossible to know where
		 * the actual worktree is if this function is executed
		 * from another worktree. No .git file support for now.
		 */
		strbuf_addf_gently(errmsg,
				   _("'%s' at main working tree is not the repository directory"),
				   wt_path.buf);
		goto done;
	}

	/*
	 * Make sure "gitdir" file points to a real .git file and that
	 * file points back here.
	 */
	if (!is_absolute_path(wt->path)) {
		strbuf_addf_gently(errmsg,
				   _("'%s' file does not contain absolute path to the working tree location"),
				   git_common_path("worktrees/%s/gitdir", wt->id));
		goto done;
	}

	if (flags & WT_VALIDATE_WORKTREE_MISSING_OK &&
	    !file_exists(wt->path)) {
		ret = 0;
		goto done;
	}

	if (!file_exists(wt_path.buf)) {
		strbuf_addf_gently(errmsg, _("'%s' does not exist"), wt_path.buf);
		goto done;
	}

	path = xstrdup_or_null(read_gitfile_gently(wt_path.buf, &err));
	if (!path) {
		strbuf_addf_gently(errmsg, _("'%s' is not a .git file, error code %d"),
				   wt_path.buf, err);
		goto done;
	}

	strbuf_realpath(&realpath, git_common_path("worktrees/%s", wt->id), 1);
	ret = fspathcmp(path, realpath.buf);

	if (ret)
		strbuf_addf_gently(errmsg, _("'%s' does not point back to '%s'"),
				   wt->path, git_common_path("worktrees/%s", wt->id));
done:
	free(path);
	strbuf_release(&wt_path);
	strbuf_release(&realpath);
	return ret;
}

void update_worktree_location(struct worktree *wt, const char *path_)
{
	struct strbuf path = STRBUF_INIT;

	if (is_main_worktree(wt))
		BUG("can't relocate main worktree");

	strbuf_realpath(&path, path_, 1);
	if (fspathcmp(wt->path, path.buf)) {
		write_file(git_common_path("worktrees/%s/gitdir", wt->id),
			   "%s/.git", path.buf);
		free(wt->path);
		wt->path = strbuf_detach(&path, NULL);
	}
	strbuf_release(&path);
}

int is_worktree_being_rebased(const struct worktree *wt,
			      const char *target)
{
	struct wt_status_state state;
	int found_rebase;

	memset(&state, 0, sizeof(state));
	found_rebase = wt_status_check_rebase(wt, &state) &&
		       (state.rebase_in_progress ||
			state.rebase_interactive_in_progress) &&
		       state.branch &&
		       skip_prefix(target, "refs/heads/", &target) &&
		       !strcmp(state.branch, target);
	wt_status_state_free_buffers(&state);
	return found_rebase;
}

int is_worktree_being_bisected(const struct worktree *wt,
			       const char *target)
{
	struct wt_status_state state;
	int found_bisect;

	memset(&state, 0, sizeof(state));
	found_bisect = wt_status_check_bisect(wt, &state) &&
		       state.bisecting_from &&
		       skip_prefix(target, "refs/heads/", &target) &&
		       !strcmp(state.bisecting_from, target);
	wt_status_state_free_buffers(&state);
	return found_bisect;
}

/*
 * note: this function should be able to detect shared symref even if
 * HEAD is temporarily detached (e.g. in the middle of rebase or
 * bisect). New commands that do similar things should update this
 * function as well.
 */
int is_shared_symref(const struct worktree *wt, const char *symref,
		     const char *target)
{
	const char *symref_target;
	struct ref_store *refs;
	int flags;

	if (wt->is_bare)
		return 0;

	if (wt->is_detached && !strcmp(symref, "HEAD")) {
		if (is_worktree_being_rebased(wt, target))
			return 1;
		if (is_worktree_being_bisected(wt, target))
			return 1;
	}

	refs = get_worktree_ref_store(wt);
	symref_target = refs_resolve_ref_unsafe(refs, symref, 0,
						NULL, &flags);
	if ((flags & REF_ISSYMREF) &&
	    symref_target && !strcmp(symref_target, target))
		return 1;

	return 0;
}

const struct worktree *find_shared_symref(struct worktree **worktrees,
					  const char *symref,
					  const char *target)
{

	for (int i = 0; worktrees[i]; i++)
		if (is_shared_symref(worktrees[i], symref, target))
			return worktrees[i];

	return NULL;
}

int submodule_uses_worktrees(const char *path)
{
	char *submodule_gitdir;
	struct strbuf sb = STRBUF_INIT, err = STRBUF_INIT;
	DIR *dir;
	struct dirent *d;
	int ret = 0;
	struct repository_format format = REPOSITORY_FORMAT_INIT;

	submodule_gitdir = git_pathdup_submodule(path, "%s", "");
	if (!submodule_gitdir)
		return 0;

	/* The env would be set for the superproject. */
	get_common_dir_noenv(&sb, submodule_gitdir);
	free(submodule_gitdir);

	strbuf_addstr(&sb, "/config");
	read_repository_format(&format, sb.buf);
	if (verify_repository_format(&format, &err)) {
		strbuf_release(&err);
		strbuf_release(&sb);
		clear_repository_format(&format);
		return 1;
	}
	clear_repository_format(&format);
	strbuf_release(&err);

	/* Replace config by worktrees. */
	strbuf_setlen(&sb, sb.len - strlen("config"));
	strbuf_addstr(&sb, "worktrees");

	/* See if there is any file inside the worktrees directory. */
	dir = opendir(sb.buf);
	strbuf_release(&sb);

	if (!dir)
		return 0;

	d = readdir_skip_dot_and_dotdot(dir);
	if (d)
		ret = 1;
	closedir(dir);
	return ret;
}

void strbuf_worktree_ref(const struct worktree *wt,
			 struct strbuf *sb,
			 const char *refname)
{
	if (parse_worktree_ref(refname, NULL, NULL, NULL) ==
		    REF_WORKTREE_CURRENT &&
	    wt && !wt->is_current) {
		if (is_main_worktree(wt))
			strbuf_addstr(sb, "main-worktree/");
		else
			strbuf_addf(sb, "worktrees/%s/", wt->id);
	}
	strbuf_addstr(sb, refname);
}

int other_head_refs(each_ref_fn fn, void *cb_data)
{
	struct worktree **worktrees, **p;
	struct strbuf refname = STRBUF_INIT;
	int ret = 0;

	worktrees = get_worktrees();
	for (p = worktrees; *p; p++) {
		struct worktree *wt = *p;
		struct object_id oid;
		int flag;

		if (wt->is_current)
			continue;

		strbuf_reset(&refname);
		strbuf_worktree_ref(wt, &refname, "HEAD");
		if (refs_resolve_ref_unsafe(get_main_ref_store(the_repository),
					    refname.buf,
					    RESOLVE_REF_READING,
					    &oid, &flag))
			ret = fn(refname.buf, NULL, &oid, flag, cb_data);
		if (ret)
			break;
	}
	free_worktrees(worktrees);
	strbuf_release(&refname);
	return ret;
}

/*
 * Repair worktree's /path/to/worktree/.git file if missing, corrupt, or not
 * pointing at <repo>/worktrees/<id>.
 */
static void repair_gitfile(struct worktree *wt,
			   worktree_repair_fn fn, void *cb_data)
{
	struct strbuf dotgit = STRBUF_INIT;
	struct strbuf repo = STRBUF_INIT;
	char *backlink;
	const char *repair = NULL;
	int err;

	/* missing worktree can't be repaired */
	if (!file_exists(wt->path))
		return;

	if (!is_directory(wt->path)) {
		fn(1, wt->path, _("not a directory"), cb_data);
		return;
	}

	strbuf_realpath(&repo, git_common_path("worktrees/%s", wt->id), 1);
	strbuf_addf(&dotgit, "%s/.git", wt->path);
	backlink = xstrdup_or_null(read_gitfile_gently(dotgit.buf, &err));

	if (err == READ_GITFILE_ERR_NOT_A_FILE)
		fn(1, wt->path, _(".git is not a file"), cb_data);
	else if (err)
		repair = _(".git file broken");
	else if (fspathcmp(backlink, repo.buf))
		repair = _(".git file incorrect");

	if (repair) {
		fn(0, wt->path, repair, cb_data);
		write_file(dotgit.buf, "gitdir: %s", repo.buf);
	}

	free(backlink);
	strbuf_release(&repo);
	strbuf_release(&dotgit);
}

static void repair_noop(int iserr UNUSED,
			const char *path UNUSED,
			const char *msg UNUSED,
			void *cb_data UNUSED)
{
	/* nothing */
}

void repair_worktrees(worktree_repair_fn fn, void *cb_data)
{
	struct worktree **worktrees = get_worktrees_internal(1);
	struct worktree **wt = worktrees + 1; /* +1 skips main worktree */

	if (!fn)
		fn = repair_noop;
	for (; *wt; wt++)
		repair_gitfile(*wt, fn, cb_data);
	free_worktrees(worktrees);
}

static int is_main_worktree_path(const char *path)
{
	struct strbuf target = STRBUF_INIT;
	struct strbuf maindir = STRBUF_INIT;
	int cmp;

	strbuf_add_real_path(&target, path);
	strbuf_strip_suffix(&target, "/.git");
	strbuf_add_real_path(&maindir, repo_get_common_dir(the_repository));
	strbuf_strip_suffix(&maindir, "/.git");
	cmp = fspathcmp(maindir.buf, target.buf);

	strbuf_release(&maindir);
	strbuf_release(&target);
	return !cmp;
}

/*
 * If both the main worktree and linked worktree have been moved, then the
 * gitfile /path/to/worktree/.git won't point into the repository, thus we
 * won't know which <repo>/worktrees/<id>/gitdir to repair. However, we may
 * be able to infer the gitdir by manually reading /path/to/worktree/.git,
 * extracting the <id>, and checking if <repo>/worktrees/<id> exists.
 */
static char *infer_backlink(const char *gitfile)
{
	struct strbuf actual = STRBUF_INIT;
	struct strbuf inferred = STRBUF_INIT;
	const char *id;

	if (strbuf_read_file(&actual, gitfile, 0) < 0)
		goto error;
	if (!starts_with(actual.buf, "gitdir:"))
		goto error;
	if (!(id = find_last_dir_sep(actual.buf)))
		goto error;
	strbuf_trim(&actual);
	id++; /* advance past '/' to point at <id> */
	if (!*id)
		goto error;
	strbuf_git_common_path(&inferred, the_repository, "worktrees/%s", id);
	if (!is_directory(inferred.buf))
		goto error;

	strbuf_release(&actual);
	return strbuf_detach(&inferred, NULL);

error:
	strbuf_release(&actual);
	strbuf_release(&inferred);
	return NULL;
}

/*
 * Repair <repo>/worktrees/<id>/gitdir if missing, corrupt, or not pointing at
 * the worktree's path.
 */
void repair_worktree_at_path(const char *path,
			     worktree_repair_fn fn, void *cb_data)
{
	struct strbuf dotgit = STRBUF_INIT;
	struct strbuf realdotgit = STRBUF_INIT;
	struct strbuf gitdir = STRBUF_INIT;
	struct strbuf olddotgit = STRBUF_INIT;
	char *backlink = NULL;
	const char *repair = NULL;
	int err;

	if (!fn)
		fn = repair_noop;

	if (is_main_worktree_path(path))
		goto done;

	strbuf_addf(&dotgit, "%s/.git", path);
	if (!strbuf_realpath(&realdotgit, dotgit.buf, 0)) {
		fn(1, path, _("not a valid path"), cb_data);
		goto done;
	}

	backlink = xstrdup_or_null(read_gitfile_gently(realdotgit.buf, &err));
	if (err == READ_GITFILE_ERR_NOT_A_FILE) {
		fn(1, realdotgit.buf, _("unable to locate repository; .git is not a file"), cb_data);
		goto done;
	} else if (err == READ_GITFILE_ERR_NOT_A_REPO) {
		if (!(backlink = infer_backlink(realdotgit.buf))) {
			fn(1, realdotgit.buf, _("unable to locate repository; .git file does not reference a repository"), cb_data);
			goto done;
		}
	} else if (err) {
		fn(1, realdotgit.buf, _("unable to locate repository; .git file broken"), cb_data);
		goto done;
	}

	strbuf_addf(&gitdir, "%s/gitdir", backlink);
	if (strbuf_read_file(&olddotgit, gitdir.buf, 0) < 0)
		repair = _("gitdir unreadable");
	else {
		strbuf_rtrim(&olddotgit);
		if (fspathcmp(olddotgit.buf, realdotgit.buf))
			repair = _("gitdir incorrect");
	}

	if (repair) {
		fn(0, gitdir.buf, repair, cb_data);
		write_file(gitdir.buf, "%s", realdotgit.buf);
	}
done:
	free(backlink);
	strbuf_release(&olddotgit);
	strbuf_release(&gitdir);
	strbuf_release(&realdotgit);
	strbuf_release(&dotgit);
}

int should_prune_worktree(const char *id, struct strbuf *reason, char **wtpath, timestamp_t expire)
{
	struct stat st;
	char *path;
	int fd;
	size_t len;
	ssize_t read_result;

	*wtpath = NULL;
	if (!is_directory(git_path("worktrees/%s", id))) {
		strbuf_addstr(reason, _("not a valid directory"));
		return 1;
	}
	if (file_exists(git_path("worktrees/%s/locked", id)))
		return 0;
	if (stat(git_path("worktrees/%s/gitdir", id), &st)) {
		strbuf_addstr(reason, _("gitdir file does not exist"));
		return 1;
	}
	fd = open(git_path("worktrees/%s/gitdir", id), O_RDONLY);
	if (fd < 0) {
		strbuf_addf(reason, _("unable to read gitdir file (%s)"),
			    strerror(errno));
		return 1;
	}
	len = xsize_t(st.st_size);
	path = xmallocz(len);

	read_result = read_in_full(fd, path, len);
	if (read_result < 0) {
		strbuf_addf(reason, _("unable to read gitdir file (%s)"),
			    strerror(errno));
		close(fd);
		free(path);
		return 1;
	}
	close(fd);

	if (read_result != len) {
		strbuf_addf(reason,
			    _("short read (expected %"PRIuMAX" bytes, read %"PRIuMAX")"),
			    (uintmax_t)len, (uintmax_t)read_result);
		free(path);
		return 1;
	}
	while (len && (path[len - 1] == '\n' || path[len - 1] == '\r'))
		len--;
	if (!len) {
		strbuf_addstr(reason, _("invalid gitdir file"));
		free(path);
		return 1;
	}
	path[len] = '\0';
	if (!file_exists(path)) {
		if (stat(git_path("worktrees/%s/index", id), &st) ||
		    st.st_mtime <= expire) {
			strbuf_addstr(reason, _("gitdir file points to non-existent location"));
			free(path);
			return 1;
		} else {
			*wtpath = path;
			return 0;
		}
	}
	*wtpath = path;
	return 0;
}

static int move_config_setting(const char *key, const char *value,
			       const char *from_file, const char *to_file)
{
	if (git_config_set_in_file_gently(to_file, key, NULL, value))
		return error(_("unable to set %s in '%s'"), key, to_file);
	if (git_config_set_in_file_gently(from_file, key, NULL, NULL))
		return error(_("unable to unset %s in '%s'"), key, from_file);
	return 0;
}

int init_worktree_config(struct repository *r)
{
	int res = 0;
	int bare = 0;
	struct config_set cs = { { 0 } };
	const char *core_worktree;
	char *common_config_file;
	char *main_worktree_file;

	/*
	 * If the extension is already enabled, then we can skip the
	 * upgrade process.
	 */
	if (r->repository_format_worktree_config)
		return 0;
	if ((res = git_config_set_gently("extensions.worktreeConfig", "true")))
		return error(_("failed to set extensions.worktreeConfig setting"));

	common_config_file = xstrfmt("%s/config", r->commondir);
	main_worktree_file = xstrfmt("%s/config.worktree", r->commondir);

	git_configset_init(&cs);
	git_configset_add_file(&cs, common_config_file);

	/*
	 * If core.bare is true in the common config file, then we need to
	 * move it to the main worktree's config file or it will break all
	 * worktrees. If it is false, then leave it in place because it
	 * _could_ be negating a global core.bare=true.
	 */
	if (!git_configset_get_bool(&cs, "core.bare", &bare) && bare) {
		if ((res = move_config_setting("core.bare", "true",
					       common_config_file,
					       main_worktree_file)))
			goto cleanup;
	}
	/*
	 * If core.worktree is set, then the main worktree is located
	 * somewhere different than the parent of the common Git dir.
	 * Relocate that value to avoid breaking all worktrees with this
	 * upgrade to worktree config.
	 */
	if (!git_configset_get_value(&cs, "core.worktree", &core_worktree, NULL)) {
		if ((res = move_config_setting("core.worktree", core_worktree,
					       common_config_file,
					       main_worktree_file)))
			goto cleanup;
	}

	/*
	 * Ensure that we use worktree config for the remaining lifetime
	 * of the current process.
	 */
	r->repository_format_worktree_config = 1;

cleanup:
	git_configset_clear(&cs);
	free(common_config_file);
	free(main_worktree_file);
	return res;
}
