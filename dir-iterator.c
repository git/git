#include "git-compat-util.h"
#include "dir.h"
#include "iterator.h"
#include "dir-iterator.h"
#include "string-list.h"

struct dir_iterator_level {
	DIR *dir;

	/*
	 * The directory entries of the current level. This list will only be
	 * populated when the iterator is ordered. In that case, `dir` will be
	 * set to `NULL`.
	 */
	struct string_list entries;
	size_t entries_idx;

	/*
	 * The length of the directory part of path at this level
	 * (including a trailing '/'):
	 */
	size_t prefix_len;
};

/*
 * The full data structure used to manage the internal directory
 * iteration state. It includes members that are not part of the
 * public interface.
 */
struct dir_iterator_int {
	struct dir_iterator base;

	/*
	 * The number of levels currently on the stack. After the first
	 * call to dir_iterator_begin(), if it succeeds to open the
	 * first level's dir, this will always be at least 1. Then,
	 * when it comes to zero the iteration is ended and this
	 * struct is freed.
	 */
	size_t levels_nr;

	/* The number of levels that have been allocated on the stack */
	size_t levels_alloc;

	/*
	 * A stack of levels. levels[0] is the uppermost directory
	 * that will be included in this iteration.
	 */
	struct dir_iterator_level *levels;

	/* Combination of flags for this dir-iterator */
	unsigned int flags;
};

static int next_directory_entry(DIR *dir, const char *path,
				struct dirent **out)
{
	struct dirent *de;

repeat:
	errno = 0;
	de = readdir(dir);
	if (!de) {
		if (errno) {
			warning_errno("error reading directory '%s'",
				      path);
			return -1;
		}

		return 1;
	}

	if (is_dot_or_dotdot(de->d_name))
		goto repeat;

	*out = de;
	return 0;
}

/*
 * Push a level in the iter stack and initialize it with information from
 * the directory pointed by iter->base->path. It is assumed that this
 * strbuf points to a valid directory path. Return 0 on success and -1
 * otherwise, setting errno accordingly and leaving the stack unchanged.
 */
static int push_level(struct dir_iterator_int *iter)
{
	struct dir_iterator_level *level;

	ALLOC_GROW(iter->levels, iter->levels_nr + 1, iter->levels_alloc);
	level = &iter->levels[iter->levels_nr++];

	if (!is_dir_sep(iter->base.path.buf[iter->base.path.len - 1]))
		strbuf_addch(&iter->base.path, '/');
	level->prefix_len = iter->base.path.len;

	level->dir = opendir(iter->base.path.buf);
	if (!level->dir) {
		int saved_errno = errno;
		if (errno != ENOENT) {
			warning_errno("error opening directory '%s'",
				      iter->base.path.buf);
		}
		iter->levels_nr--;
		errno = saved_errno;
		return -1;
	}

	string_list_init_dup(&level->entries);
	level->entries_idx = 0;

	/*
	 * When the iterator is sorted we read and sort all directory entries
	 * directly.
	 */
	if (iter->flags & DIR_ITERATOR_SORTED) {
		struct dirent *de;

		while (1) {
			int ret = next_directory_entry(level->dir, iter->base.path.buf, &de);
			if (ret < 0) {
				if (errno != ENOENT &&
				    iter->flags & DIR_ITERATOR_PEDANTIC)
					return -1;
				continue;
			} else if (ret > 0) {
				break;
			}

			string_list_append(&level->entries, de->d_name);
		}
		string_list_sort(&level->entries);

		closedir(level->dir);
		level->dir = NULL;
	}

	return 0;
}

/*
 * Pop the top level on the iter stack, releasing any resources associated
 * with it. Return the new value of iter->levels_nr.
 */
static int pop_level(struct dir_iterator_int *iter)
{
	struct dir_iterator_level *level =
		&iter->levels[iter->levels_nr - 1];

	if (level->dir && closedir(level->dir))
		warning_errno("error closing directory '%s'",
			      iter->base.path.buf);
	level->dir = NULL;
	string_list_clear(&level->entries, 0);

	return --iter->levels_nr;
}

/*
 * Populate iter->base with the necessary information on the next iteration
 * entry, represented by the given name. Return 0 on success and -1
 * otherwise, setting errno accordingly.
 */
static int prepare_next_entry_data(struct dir_iterator_int *iter,
				   const char *name)
{
	int err, saved_errno;

	strbuf_addstr(&iter->base.path, name);
	/*
	 * We have to reset these because the path strbuf might have
	 * been realloc()ed at the previous strbuf_addstr().
	 */
	iter->base.relative_path = iter->base.path.buf +
				   iter->levels[0].prefix_len;
	iter->base.basename = iter->base.path.buf +
			      iter->levels[iter->levels_nr - 1].prefix_len;

	err = lstat(iter->base.path.buf, &iter->base.st);

	saved_errno = errno;
	if (err && errno != ENOENT)
		warning_errno("failed to stat '%s'", iter->base.path.buf);

	errno = saved_errno;
	return err;
}

int dir_iterator_advance(struct dir_iterator *dir_iterator)
{
	struct dir_iterator_int *iter =
		(struct dir_iterator_int *)dir_iterator;

	if (S_ISDIR(iter->base.st.st_mode) && push_level(iter)) {
		if (errno != ENOENT && iter->flags & DIR_ITERATOR_PEDANTIC)
			return ITER_ERROR;
		if (iter->levels_nr == 0)
			return ITER_ERROR;
	}

	/* Loop until we find an entry that we can give back to the caller. */
	while (1) {
		struct dirent *de;
		struct dir_iterator_level *level =
			&iter->levels[iter->levels_nr - 1];
		const char *name;

		strbuf_setlen(&iter->base.path, level->prefix_len);

		if (level->dir) {
			int ret = next_directory_entry(level->dir, iter->base.path.buf, &de);
			if (ret < 0) {
				if (iter->flags & DIR_ITERATOR_PEDANTIC)
					return ITER_ERROR;
				continue;
			} else if (ret > 0) {
				if (pop_level(iter) == 0)
					return ITER_DONE;
				continue;
			}

			name = de->d_name;
		} else {
			if (level->entries_idx >= level->entries.nr) {
				if (pop_level(iter) == 0)
					return ITER_DONE;
				continue;
			}

			name = level->entries.items[level->entries_idx++].string;
		}

		if (prepare_next_entry_data(iter, name)) {
			if (errno != ENOENT && iter->flags & DIR_ITERATOR_PEDANTIC)
				return ITER_ERROR;
			continue;
		}

		return ITER_OK;
	}
}

void dir_iterator_free(struct dir_iterator *dir_iterator)
{
	struct dir_iterator_int *iter = (struct dir_iterator_int *)dir_iterator;

	if (!iter)
		return;

	for (; iter->levels_nr; iter->levels_nr--) {
		struct dir_iterator_level *level =
			&iter->levels[iter->levels_nr - 1];

		if (level->dir && closedir(level->dir)) {
			int saved_errno = errno;
			strbuf_setlen(&iter->base.path, level->prefix_len);
			errno = saved_errno;
			warning_errno("error closing directory '%s'",
				      iter->base.path.buf);
		}

		string_list_clear(&level->entries, 0);
	}

	free(iter->levels);
	strbuf_release(&iter->base.path);
	free(iter);
}

struct dir_iterator *dir_iterator_begin(const char *path, unsigned int flags)
{
	struct dir_iterator_int *iter = xcalloc(1, sizeof(*iter));
	struct dir_iterator *dir_iterator = &iter->base;
	int saved_errno, err;

	strbuf_init(&iter->base.path, PATH_MAX);
	strbuf_addstr(&iter->base.path, path);

	ALLOC_GROW(iter->levels, 10, iter->levels_alloc);
	iter->levels_nr = 0;
	iter->flags = flags;

	/*
	 * Note: lstat already checks for NULL or empty strings and
	 * nonexistent paths.
	 */
	err = lstat(iter->base.path.buf, &iter->base.st);

	if (err < 0) {
		saved_errno = errno;
		goto error_out;
	}

	if (!S_ISDIR(iter->base.st.st_mode)) {
		saved_errno = ENOTDIR;
		goto error_out;
	}

	return dir_iterator;

error_out:
	dir_iterator_free(dir_iterator);
	errno = saved_errno;
	return NULL;
}
