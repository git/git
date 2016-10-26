#include "cache.h"
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

/*
 * read 'path_to_ref' into 'ref'.  Also if is_detached is not NULL,
 * set is_detached to 1 (0) if the ref is detached (is not detached).
 *
 * $GIT_COMMON_DIR/$symref (e.g. HEAD) is practically outside $GIT_DIR so
 * for linked worktrees, `resolve_ref_unsafe()` won't work (it uses
 * git_path). Parse the ref ourselves.
 *
 * return -1 if the ref is not a proper ref, 0 otherwise (success)
 */
static int parse_ref(char *path_to_ref, struct strbuf *ref, int *is_detached)
{
	if (is_detached)
		*is_detached = 0;
	if (!strbuf_readlink(ref, path_to_ref, 0)) {
		/* HEAD is symbolic link */
		if (!starts_with(ref->buf, "refs/") ||
				check_refname_format(ref->buf, 0))
			return -1;
	} else if (strbuf_read_file(ref, path_to_ref, 0) >= 0) {
		/* textual symref or detached */
		if (!starts_with(ref->buf, "ref:")) {
			if (is_detached)
				*is_detached = 1;
		} else {
			strbuf_remove(ref, 0, strlen("ref:"));
			strbuf_trim(ref);
			if (check_refname_format(ref->buf, 0))
				return -1;
		}
	} else
		return -1;
	return 0;
}

/**
 * Add the head_sha1 and head_ref (if not detached) to the given worktree
 */
static void add_head_info(struct strbuf *head_ref, struct worktree *worktree)
{
	if (head_ref->len) {
		if (worktree->is_detached) {
			get_sha1_hex(head_ref->buf, worktree->head_sha1);
		} else {
			resolve_ref_unsafe(head_ref->buf, 0, worktree->head_sha1, NULL);
			worktree->head_ref = strbuf_detach(head_ref, NULL);
		}
	}
}

/**
 * get the main worktree
 */
static struct worktree *get_main_worktree(void)
{
	struct worktree *worktree = NULL;
	struct strbuf path = STRBUF_INIT;
	struct strbuf worktree_path = STRBUF_INIT;
	struct strbuf head_ref = STRBUF_INIT;
	int is_bare = 0;
	int is_detached = 0;

	strbuf_add_absolute_path(&worktree_path, get_git_common_dir());
	is_bare = !strbuf_strip_suffix(&worktree_path, "/.git");
	if (is_bare)
		strbuf_strip_suffix(&worktree_path, "/.");

	strbuf_addf(&path, "%s/HEAD", get_git_common_dir());

	if (parse_ref(path.buf, &head_ref, &is_detached) < 0)
		goto done;

	worktree = xmalloc(sizeof(struct worktree));
	worktree->path = strbuf_detach(&worktree_path, NULL);
	worktree->id = NULL;
	worktree->is_bare = is_bare;
	worktree->head_ref = NULL;
	worktree->is_detached = is_detached;
	worktree->is_current = 0;
	add_head_info(&head_ref, worktree);
	worktree->lock_reason = NULL;
	worktree->lock_reason_valid = 0;

done:
	strbuf_release(&path);
	strbuf_release(&worktree_path);
	strbuf_release(&head_ref);
	return worktree;
}

static struct worktree *get_linked_worktree(const char *id)
{
	struct worktree *worktree = NULL;
	struct strbuf path = STRBUF_INIT;
	struct strbuf worktree_path = STRBUF_INIT;
	struct strbuf head_ref = STRBUF_INIT;
	int is_detached = 0;

	if (!id)
		die("Missing linked worktree name");

	strbuf_git_common_path(&path, "worktrees/%s/gitdir", id);
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

	if (parse_ref(path.buf, &head_ref, &is_detached) < 0)
		goto done;

	worktree = xmalloc(sizeof(struct worktree));
	worktree->path = strbuf_detach(&worktree_path, NULL);
	worktree->id = xstrdup(id);
	worktree->is_bare = 0;
	worktree->head_ref = NULL;
	worktree->is_detached = is_detached;
	worktree->is_current = 0;
	add_head_info(&head_ref, worktree);
	worktree->lock_reason = NULL;
	worktree->lock_reason_valid = 0;

done:
	strbuf_release(&path);
	strbuf_release(&worktree_path);
	strbuf_release(&head_ref);
	return worktree;
}

static void mark_current_worktree(struct worktree **worktrees)
{
	char *git_dir = xstrdup(absolute_path(get_git_dir()));
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

struct worktree **get_worktrees(void)
{
	struct worktree **list = NULL;
	struct strbuf path = STRBUF_INIT;
	DIR *dir;
	struct dirent *d;
	int counter = 0, alloc = 2;

	list = xmalloc(alloc * sizeof(struct worktree *));

	if ((list[counter] = get_main_worktree()))
		counter++;

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
	char *path;

	if ((wt = find_worktree_by_suffix(list, arg)))
		return wt;

	arg = prefix_filename(prefix, strlen(prefix), arg);
	path = xstrdup(real_path(arg));
	for (; *list; list++)
		if (!fspathcmp(path, real_path((*list)->path)))
			break;
	free(path);
	return *list;
}

int is_main_worktree(const struct worktree *wt)
{
	return !wt->id;
}

const char *is_worktree_locked(struct worktree *wt)
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
	struct strbuf path = STRBUF_INIT;
	struct strbuf sb = STRBUF_INIT;
	static struct worktree **worktrees;
	int i = 0;

	if (worktrees)
		free_worktrees(worktrees);
	worktrees = get_worktrees();

	for (i = 0; worktrees[i]; i++) {
		struct worktree *wt = worktrees[i];
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

		strbuf_reset(&path);
		strbuf_reset(&sb);
		strbuf_addf(&path, "%s/%s",
			    get_worktree_git_dir(wt),
			    symref);

		if (parse_ref(path.buf, &sb, NULL)) {
			continue;
		}

		if (!strcmp(sb.buf, target)) {
			existing = wt;
			break;
		}
	}

	strbuf_release(&path);
	strbuf_release(&sb);

	return existing;
}
