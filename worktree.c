#include "cache.h"
#include "repository.h"
#include "refs.h"
#include "strbuf.h"
#include "worktree.h"
#include "dir.h"
#include "wt-status.h"

void free_worktrees(struct worktree **worktrees)
{
	int i = 0;

	for (i = 0; worktrees[i]; i++) {
		free(worktrees[i]->path);
		free(worktrees[i]->id);
		free(worktrees[i]->head_ref);
		free(worktrees[i]->lock_reason);
		free(worktrees[i]);
	}
	free (worktrees);
}

/**
 * Update head_sha1, head_ref and is_detached of the given worktree
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

/**
 * get the main worktree
 */
static struct worktree *get_main_worktree(void)
{
	struct worktree *worktree = NULL;
	struct strbuf worktree_path = STRBUF_INIT;

	strbuf_add_absolute_path(&worktree_path, get_git_common_dir());
	strbuf_strip_suffix(&worktree_path, "/.");
	if (!strbuf_strip_suffix(&worktree_path, "/.git"))
		strbuf_strip_suffix(&worktree_path, "/.");

	worktree = xcalloc(1, sizeof(*worktree));
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
	add_head_info(worktree);

	strbuf_release(&worktree_path);
	return worktree;
}

static struct worktree *get_linked_worktree(const char *id)
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
	if (!strbuf_strip_suffix(&worktree_path, "/.git")) {
		strbuf_reset(&worktree_path);
		strbuf_add_absolute_path(&worktree_path, ".");
		strbuf_strip_suffix(&worktree_path, "/.");
	}

	strbuf_reset(&path);
	strbuf_addf(&path, "%s/worktrees/%s/HEAD", get_git_common_dir(), id);

	worktree = xcalloc(1, sizeof(*worktree));
	worktree->path = strbuf_detach(&worktree_path, NULL);
	worktree->id = xstrdup(id);
	add_head_info(worktree);

done:
	strbuf_release(&path);
	strbuf_release(&worktree_path);
	return worktree;
}

static void mark_current_worktree(struct worktree **worktrees)
{
	char *git_dir = absolute_pathdup(get_git_dir());
	int i;

	for (i = 0; worktrees[i]; i++) {
		struct worktree *wt = worktrees[i];
		const char *wt_git_dir = get_worktree_git_dir(wt);

		if (!fspathcmp(git_dir, absolute_path(wt_git_dir))) {
			wt->is_current = 1;
			break;
		}
	}
	free(git_dir);
}

static int compare_worktree(const void *a_, const void *b_)
{
	const struct worktree *const *a = a_;
	const struct worktree *const *b = b_;
	return fspathcmp((*a)->path, (*b)->path);
}

struct worktree **get_worktrees(unsigned flags)
{
	struct worktree **list = NULL;
	struct strbuf path = STRBUF_INIT;
	DIR *dir;
	struct dirent *d;
	int counter = 0, alloc = 2;

	ALLOC_ARRAY(list, alloc);

	list[counter++] = get_main_worktree();

	strbuf_addf(&path, "%s/worktrees", get_git_common_dir());
	dir = opendir(path.buf);
	strbuf_release(&path);
	if (dir) {
		while ((d = readdir(dir)) != NULL) {
			struct worktree *linked = NULL;
			if (is_dot_or_dotdot(d->d_name))
				continue;

			if ((linked = get_linked_worktree(d->d_name))) {
				ALLOC_GROW(list, counter + 1, alloc);
				list[counter++] = linked;
			}
		}
		closedir(dir);
	}
	ALLOC_GROW(list, counter + 1, alloc);
	list[counter] = NULL;

	if (flags & GWT_SORT_LINKED)
		/*
		 * don't sort the first item (main worktree), which will
		 * always be the first
		 */
		QSORT(list + 1, counter - 1, compare_worktree);

	mark_current_worktree(list);
	return list;
}

const char *get_worktree_git_dir(const struct worktree *wt)
{
	if (!wt)
		return get_git_dir();
	else if (!wt->id)
		return get_git_common_dir();
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
	assert(!is_main_worktree(wt));

	if (!wt->lock_reason_valid) {
		struct strbuf path = STRBUF_INIT;

		strbuf_addstr(&path, worktree_git_path(wt, "locked"));
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

/* convenient wrapper to deal with NULL strbuf */
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
		((state.rebase_in_progress ||
		  state.rebase_interactive_in_progress) &&
		 state.branch &&
		 starts_with(target, "refs/heads/") &&
		 !strcmp(state.branch, target + strlen("refs/heads/")));
	free(state.branch);
	free(state.onto);
	return found_rebase;
}

int is_worktree_being_bisected(const struct worktree *wt,
			       const char *target)
{
	struct wt_status_state state;
	int found_rebase;

	memset(&state, 0, sizeof(state));
	found_rebase = wt_status_check_bisect(wt, &state) &&
		state.branch &&
		starts_with(target, "refs/heads/") &&
		!strcmp(state.branch, target + strlen("refs/heads/"));
	free(state.branch);
	return found_rebase;
}

/*
 * note: this function should be able to detect shared symref even if
 * HEAD is temporarily detached (e.g. in the middle of rebase or
 * bisect). New commands that do similar things should update this
 * function as well.
 */
const struct worktree *find_shared_symref(const char *symref,
					  const char *target)
{
	const struct worktree *existing = NULL;
	static struct worktree **worktrees;
	int i = 0;

	if (worktrees)
		free_worktrees(worktrees);
	worktrees = get_worktrees(0);

	for (i = 0; worktrees[i]; i++) {
		struct worktree *wt = worktrees[i];
		const char *symref_target;
		struct ref_store *refs;
		int flags;

		if (wt->is_bare)
			continue;

		if (wt->is_detached && !strcmp(symref, "HEAD")) {
			if (is_worktree_being_rebased(wt, target)) {
				existing = wt;
				break;
			}
			if (is_worktree_being_bisected(wt, target)) {
				existing = wt;
				break;
			}
		}

		refs = get_worktree_ref_store(wt);
		symref_target = refs_resolve_ref_unsafe(refs, symref, 0,
							NULL, &flags);
		if ((flags & REF_ISSYMREF) &&
		    symref_target && !strcmp(symref_target, target)) {
			existing = wt;
			break;
		}
	}

	return existing;
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

	while ((d = readdir(dir)) != NULL) {
		if (is_dot_or_dotdot(d->d_name))
			continue;

		ret = 1;
		break;
	}
	closedir(dir);
	return ret;
}

int parse_worktree_ref(const char *worktree_ref, const char **name,
		       int *name_length, const char **ref)
{
	if (skip_prefix(worktree_ref, "main-worktree/", &worktree_ref)) {
		if (!*worktree_ref)
			return -1;
		if (name)
			*name = NULL;
		if (name_length)
			*name_length = 0;
		if (ref)
			*ref = worktree_ref;
		return 0;
	}
	if (skip_prefix(worktree_ref, "worktrees/", &worktree_ref)) {
		const char *slash = strchr(worktree_ref, '/');

		if (!slash || slash == worktree_ref || !slash[1])
			return -1;
		if (name)
			*name = worktree_ref;
		if (name_length)
			*name_length = slash - worktree_ref;
		if (ref)
			*ref = slash + 1;
		return 0;
	}
	return -1;
}

void strbuf_worktree_ref(const struct worktree *wt,
			 struct strbuf *sb,
			 const char *refname)
{
	switch (ref_type(refname)) {
	case REF_TYPE_PSEUDOREF:
	case REF_TYPE_PER_WORKTREE:
		if (wt && !wt->is_current) {
			if (is_main_worktree(wt))
				strbuf_addstr(sb, "main-worktree/");
			else
				strbuf_addf(sb, "worktrees/%s/", wt->id);
		}
		break;

	case REF_TYPE_MAIN_PSEUDOREF:
	case REF_TYPE_OTHER_PSEUDOREF:
		break;

	case REF_TYPE_NORMAL:
		/*
		 * For shared refs, don't prefix worktrees/ or
		 * main-worktree/. It's not necessary and
		 * files-backend.c can't handle it anyway.
		 */
		break;
	}
	strbuf_addstr(sb, refname);
}

const char *worktree_ref(const struct worktree *wt, const char *refname)
{
	static struct strbuf sb = STRBUF_INIT;

	strbuf_reset(&sb);
	strbuf_worktree_ref(wt, &sb, refname);
	return sb.buf;
}

int other_head_refs(each_ref_fn fn, void *cb_data)
{
	struct worktree **worktrees, **p;
	int ret = 0;

	worktrees = get_worktrees(0);
	for (p = worktrees; *p; p++) {
		struct worktree *wt = *p;
		struct object_id oid;
		int flag;

		if (wt->is_current)
			continue;

		if (!refs_read_ref_full(get_main_ref_store(the_repository),
					worktree_ref(wt, "HEAD"),
					RESOLVE_REF_READING,
					&oid, &flag))
			ret = fn(worktree_ref(wt, "HEAD"), &oid, flag, cb_data);
		if (ret)
			break;
	}
	free_worktrees(worktrees);
	return ret;
}
