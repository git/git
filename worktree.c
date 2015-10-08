#include "cache.h"
#include "refs.h"
#include "strbuf.h"
#include "worktree.h"

/*
 * read 'path_to_ref' into 'ref'.  Also if is_detached is not NULL,
 * set is_detached to 1 (0) if the ref is detatched (is not detached).
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

static char *find_main_symref(const char *symref, const char *branch)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	struct strbuf gitdir = STRBUF_INIT;
	char *existing = NULL;

	strbuf_addf(&path, "%s/%s", get_git_common_dir(), symref);
	if (parse_ref(path.buf, &sb, NULL) < 0)
		goto done;
	if (strcmp(sb.buf, branch))
		goto done;
	strbuf_addstr(&gitdir, get_git_common_dir());
	strbuf_strip_suffix(&gitdir, ".git");
	existing = strbuf_detach(&gitdir, NULL);
done:
	strbuf_release(&path);
	strbuf_release(&sb);
	strbuf_release(&gitdir);

	return existing;
}

static char *find_linked_symref(const char *symref, const char *branch,
				const char *id)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	struct strbuf gitdir = STRBUF_INIT;
	char *existing = NULL;

	if (!id)
		die("Missing linked worktree name");

	strbuf_addf(&path, "%s/worktrees/%s/%s", get_git_common_dir(), id, symref);

	if (parse_ref(path.buf, &sb, NULL) < 0)
		goto done;
	if (strcmp(sb.buf, branch))
		goto done;
	strbuf_reset(&path);
	strbuf_addf(&path, "%s/worktrees/%s/gitdir", get_git_common_dir(), id);
	if (strbuf_read_file(&gitdir, path.buf, 0) <= 0)
		goto done;
	strbuf_rtrim(&gitdir);
	strbuf_strip_suffix(&gitdir, ".git");

	existing = strbuf_detach(&gitdir, NULL);
done:
	strbuf_release(&path);
	strbuf_release(&sb);
	strbuf_release(&gitdir);

	return existing;
}

char *find_shared_symref(const char *symref, const char *target)
{
	struct strbuf path = STRBUF_INIT;
	DIR *dir;
	struct dirent *d;
	char *existing;

	if ((existing = find_main_symref(symref, target)))
		return existing;

	strbuf_addf(&path, "%s/worktrees", get_git_common_dir());
	dir = opendir(path.buf);
	strbuf_release(&path);
	if (!dir)
		return NULL;

	while ((d = readdir(dir)) != NULL) {
		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;
		existing = find_linked_symref(symref, target, d->d_name);
		if (existing)
			goto done;
	}
done:
	closedir(dir);

	return existing;
}
