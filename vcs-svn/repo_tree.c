/*
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "git-compat-util.h"

#include "string_pool.h"
#include "repo_tree.h"
#include "obj_pool.h"
#include "fast_export.h"

#include "trp.h"

struct repo_dirent {
	uint32_t name_offset;
	struct trp_node children;
	uint32_t mode;
	uint32_t content_offset;
};

struct repo_dir {
	struct trp_root entries;
};

struct repo_commit {
	uint32_t root_dir_offset;
};

/* Memory pools for commit, dir and dirent */
obj_pool_gen(commit, struct repo_commit, 4096)
obj_pool_gen(dir, struct repo_dir, 4096)
obj_pool_gen(dent, struct repo_dirent, 4096)

static uint32_t active_commit;
static uint32_t mark;

static int repo_dirent_name_cmp(const void *a, const void *b);

/* Treap for directory entries */
trp_gen(static, dent_, struct repo_dirent, children, dent, repo_dirent_name_cmp)

uint32_t next_blob_mark(void)
{
	return mark++;
}

static struct repo_dir *repo_commit_root_dir(struct repo_commit *commit)
{
	return dir_pointer(commit->root_dir_offset);
}

static struct repo_dirent *repo_first_dirent(struct repo_dir *dir)
{
	return dent_first(&dir->entries);
}

static int repo_dirent_name_cmp(const void *a, const void *b)
{
	const struct repo_dirent *dent1 = a, *dent2 = b;
	uint32_t a_offset = dent1->name_offset;
	uint32_t b_offset = dent2->name_offset;
	return (a_offset > b_offset) - (a_offset < b_offset);
}

static int repo_dirent_is_dir(struct repo_dirent *dent)
{
	return dent != NULL && dent->mode == REPO_MODE_DIR;
}

static struct repo_dir *repo_dir_from_dirent(struct repo_dirent *dent)
{
	if (!repo_dirent_is_dir(dent))
		return NULL;
	return dir_pointer(dent->content_offset);
}

static struct repo_dir *repo_clone_dir(struct repo_dir *orig_dir)
{
	uint32_t orig_o, new_o;
	orig_o = dir_offset(orig_dir);
	if (orig_o >= dir_pool.committed)
		return orig_dir;
	new_o = dir_alloc(1);
	orig_dir = dir_pointer(orig_o);
	*dir_pointer(new_o) = *orig_dir;
	return dir_pointer(new_o);
}

static struct repo_dirent *repo_read_dirent(uint32_t revision,
					    const uint32_t *path)
{
	uint32_t name = 0;
	struct repo_dirent *key = dent_pointer(dent_alloc(1));
	struct repo_dir *dir = NULL;
	struct repo_dirent *dent = NULL;
	dir = repo_commit_root_dir(commit_pointer(revision));
	while (~(name = *path++)) {
		key->name_offset = name;
		dent = dent_search(&dir->entries, key);
		if (dent == NULL || !repo_dirent_is_dir(dent))
			break;
		dir = repo_dir_from_dirent(dent);
	}
	dent_free(1);
	return dent;
}

static void repo_write_dirent(const uint32_t *path, uint32_t mode,
			      uint32_t content_offset, uint32_t del)
{
	uint32_t name, revision, dir_o = ~0U, parent_dir_o = ~0U;
	struct repo_dir *dir;
	struct repo_dirent *key;
	struct repo_dirent *dent = NULL;
	revision = active_commit;
	dir = repo_commit_root_dir(commit_pointer(revision));
	dir = repo_clone_dir(dir);
	commit_pointer(revision)->root_dir_offset = dir_offset(dir);
	while (~(name = *path++)) {
		parent_dir_o = dir_offset(dir);

		key = dent_pointer(dent_alloc(1));
		key->name_offset = name;

		dent = dent_search(&dir->entries, key);
		if (dent == NULL)
			dent = key;
		else
			dent_free(1);

		if (dent == key) {
			dent->mode = REPO_MODE_DIR;
			dent->content_offset = 0;
			dent = dent_insert(&dir->entries, dent);
		}

		if (dent_offset(dent) < dent_pool.committed) {
			dir_o = repo_dirent_is_dir(dent) ?
					dent->content_offset : ~0;
			dent_remove(&dir->entries, dent);
			dent = dent_pointer(dent_alloc(1));
			dent->name_offset = name;
			dent->mode = REPO_MODE_DIR;
			dent->content_offset = dir_o;
			dent = dent_insert(&dir->entries, dent);
		}

		dir = repo_dir_from_dirent(dent);
		dir = repo_clone_dir(dir);
		dent->content_offset = dir_offset(dir);
	}
	if (dent == NULL)
		return;
	dent->mode = mode;
	dent->content_offset = content_offset;
	if (del && ~parent_dir_o)
		dent_remove(&dir_pointer(parent_dir_o)->entries, dent);
}

uint32_t repo_read_path(const uint32_t *path)
{
	uint32_t content_offset = 0;
	struct repo_dirent *dent = repo_read_dirent(active_commit, path);
	if (dent != NULL)
		content_offset = dent->content_offset;
	return content_offset;
}

uint32_t repo_read_mode(const uint32_t *path)
{
	struct repo_dirent *dent = repo_read_dirent(active_commit, path);
	if (dent == NULL)
		die("invalid dump: path to be modified is missing");
	return dent->mode;
}

void repo_copy(uint32_t revision, const uint32_t *src, const uint32_t *dst)
{
	uint32_t mode = 0, content_offset = 0;
	struct repo_dirent *src_dent;
	src_dent = repo_read_dirent(revision, src);
	if (src_dent != NULL) {
		mode = src_dent->mode;
		content_offset = src_dent->content_offset;
		repo_write_dirent(dst, mode, content_offset, 0);
	}
}

void repo_add(uint32_t *path, uint32_t mode, uint32_t blob_mark)
{
	repo_write_dirent(path, mode, blob_mark, 0);
}

void repo_delete(uint32_t *path)
{
	repo_write_dirent(path, 0, 0, 1);
}

static void repo_git_add_r(uint32_t depth, uint32_t *path, struct repo_dir *dir);

static void repo_git_add(uint32_t depth, uint32_t *path, struct repo_dirent *dent)
{
	if (repo_dirent_is_dir(dent))
		repo_git_add_r(depth, path, repo_dir_from_dirent(dent));
	else
		fast_export_modify(depth, path,
				   dent->mode, dent->content_offset);
}

static void repo_git_add_r(uint32_t depth, uint32_t *path, struct repo_dir *dir)
{
	struct repo_dirent *de = repo_first_dirent(dir);
	while (de) {
		path[depth] = de->name_offset;
		repo_git_add(depth + 1, path, de);
		de = dent_next(&dir->entries, de);
	}
}

static void repo_diff_r(uint32_t depth, uint32_t *path, struct repo_dir *dir1,
			struct repo_dir *dir2)
{
	struct repo_dirent *de1, *de2;
	de1 = repo_first_dirent(dir1);
	de2 = repo_first_dirent(dir2);

	while (de1 && de2) {
		if (de1->name_offset < de2->name_offset) {
			path[depth] = de1->name_offset;
			fast_export_delete(depth + 1, path);
			de1 = dent_next(&dir1->entries, de1);
			continue;
		}
		if (de1->name_offset > de2->name_offset) {
			path[depth] = de2->name_offset;
			repo_git_add(depth + 1, path, de2);
			de2 = dent_next(&dir2->entries, de2);
			continue;
		}
		path[depth] = de1->name_offset;

		if (de1->mode == de2->mode &&
		    de1->content_offset == de2->content_offset) {
			; /* No change. */
		} else if (repo_dirent_is_dir(de1) && repo_dirent_is_dir(de2)) {
			repo_diff_r(depth + 1, path,
				    repo_dir_from_dirent(de1),
				    repo_dir_from_dirent(de2));
		} else if (!repo_dirent_is_dir(de1) && !repo_dirent_is_dir(de2)) {
			repo_git_add(depth + 1, path, de2);
		} else {
			fast_export_delete(depth + 1, path);
			repo_git_add(depth + 1, path, de2);
		}
		de1 = dent_next(&dir1->entries, de1);
		de2 = dent_next(&dir2->entries, de2);
	}
	while (de1) {
		path[depth] = de1->name_offset;
		fast_export_delete(depth + 1, path);
		de1 = dent_next(&dir1->entries, de1);
	}
	while (de2) {
		path[depth] = de2->name_offset;
		repo_git_add(depth + 1, path, de2);
		de2 = dent_next(&dir2->entries, de2);
	}
}

static uint32_t path_stack[REPO_MAX_PATH_DEPTH];

void repo_diff(uint32_t r1, uint32_t r2)
{
	repo_diff_r(0,
		    path_stack,
		    repo_commit_root_dir(commit_pointer(r1)),
		    repo_commit_root_dir(commit_pointer(r2)));
}

void repo_commit(uint32_t revision, const char *author,
		const struct strbuf *log, const char *uuid, const char *url,
		unsigned long timestamp)
{
	fast_export_commit(revision, author, log, uuid, url, timestamp);
	dent_commit();
	dir_commit();
	active_commit = commit_alloc(1);
	commit_pointer(active_commit)->root_dir_offset =
		commit_pointer(active_commit - 1)->root_dir_offset;
}

static void mark_init(void)
{
	uint32_t i;
	mark = 0;
	for (i = 0; i < dent_pool.size; i++)
		if (!repo_dirent_is_dir(dent_pointer(i)) &&
		    dent_pointer(i)->content_offset > mark)
			mark = dent_pointer(i)->content_offset;
	mark++;
}

void repo_init(void)
{
	mark_init();
	if (commit_pool.size == 0) {
		/* Create empty tree for commit 0. */
		commit_alloc(1);
		commit_pointer(0)->root_dir_offset = dir_alloc(1);
		dir_pointer(0)->entries.trp_root = ~0;
		dir_commit();
	}
	/* Preallocate next commit, ready for changes. */
	active_commit = commit_alloc(1);
	commit_pointer(active_commit)->root_dir_offset =
		commit_pointer(active_commit - 1)->root_dir_offset;
}

void repo_reset(void)
{
	pool_reset();
	commit_reset();
	dir_reset();
	dent_reset();
}
