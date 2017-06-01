#include "cache.h"
#include "blob.h"
#include "dir.h"
#include "streaming.h"
#include "submodule.h"

static void create_directories(const char *path, int path_len,
			       const struct checkout *state)
{
	char *buf = xmallocz(path_len);
	int len = 0;

	while (len < path_len) {
		do {
			buf[len] = path[len];
			len++;
		} while (len < path_len && path[len] != '/');
		if (len >= path_len)
			break;
		buf[len] = 0;

		/*
		 * For 'checkout-index --prefix=<dir>', <dir> is
		 * allowed to be a symlink to an existing directory,
		 * and we set 'state->base_dir_len' below, such that
		 * we test the path components of the prefix with the
		 * stat() function instead of the lstat() function.
		 */
		if (has_dirs_only_path(buf, len, state->base_dir_len))
			continue; /* ok, it is already a directory. */

		/*
		 * If this mkdir() would fail, it could be that there
		 * is already a symlink or something else exists
		 * there, therefore we then try to unlink it and try
		 * one more time to create the directory.
		 */
		if (mkdir(buf, 0777)) {
			if (errno == EEXIST && state->force &&
			    !unlink_or_warn(buf) && !mkdir(buf, 0777))
				continue;
			die_errno("cannot create directory at '%s'", buf);
		}
	}
	free(buf);
}

static void remove_subtree(struct strbuf *path)
{
	DIR *dir = opendir(path->buf);
	struct dirent *de;
	int origlen = path->len;

	if (!dir)
		die_errno("cannot opendir '%s'", path->buf);
	while ((de = readdir(dir)) != NULL) {
		struct stat st;

		if (is_dot_or_dotdot(de->d_name))
			continue;

		strbuf_addch(path, '/');
		strbuf_addstr(path, de->d_name);
		if (lstat(path->buf, &st))
			die_errno("cannot lstat '%s'", path->buf);
		if (S_ISDIR(st.st_mode))
			remove_subtree(path);
		else if (unlink(path->buf))
			die_errno("cannot unlink '%s'", path->buf);
		strbuf_setlen(path, origlen);
	}
	closedir(dir);
	if (rmdir(path->buf))
		die_errno("cannot rmdir '%s'", path->buf);
}

static int create_file(const char *path, unsigned int mode)
{
	mode = (mode & 0100) ? 0777 : 0666;
	return open(path, O_WRONLY | O_CREAT | O_EXCL, mode);
}

static void *read_blob_entry(const struct cache_entry *ce, unsigned long *size)
{
	enum object_type type;
	void *new = read_sha1_file(ce->oid.hash, &type, size);

	if (new) {
		if (type == OBJ_BLOB)
			return new;
		free(new);
	}
	return NULL;
}

static int open_output_fd(char *path, const struct cache_entry *ce, int to_tempfile)
{
	int symlink = (ce->ce_mode & S_IFMT) != S_IFREG;
	if (to_tempfile) {
		xsnprintf(path, TEMPORARY_FILENAME_LENGTH, "%s",
			  symlink ? ".merge_link_XXXXXX" : ".merge_file_XXXXXX");
		return mkstemp(path);
	} else {
		return create_file(path, !symlink ? ce->ce_mode : 0666);
	}
}

static int fstat_output(int fd, const struct checkout *state, struct stat *st)
{
	/* use fstat() only when path == ce->name */
	if (fstat_is_reliable() &&
	    state->refresh_cache && !state->base_dir_len) {
		fstat(fd, st);
		return 1;
	}
	return 0;
}

static int streaming_write_entry(const struct cache_entry *ce, char *path,
				 struct stream_filter *filter,
				 const struct checkout *state, int to_tempfile,
				 int *fstat_done, struct stat *statbuf)
{
	int result = 0;
	int fd;

	fd = open_output_fd(path, ce, to_tempfile);
	if (fd < 0)
		return -1;

	result |= stream_blob_to_fd(fd, &ce->oid, filter, 1);
	*fstat_done = fstat_output(fd, state, statbuf);
	result |= close(fd);

	if (result)
		unlink(path);
	return result;
}

void enable_delayed_checkout(struct checkout *state)
{
	if (!state->delayed_checkout) {
		state->delayed_checkout = xmalloc(sizeof(*state->delayed_checkout));
		state->delayed_checkout->state = CE_CAN_DELAY;
		string_list_init(&state->delayed_checkout->filters, 0);
		string_list_init(&state->delayed_checkout->paths, 0);
	}
}

static int remove_available_paths(struct string_list_item *item, void *cb_data)
{
	struct string_list *available_paths = cb_data;
	return !string_list_has_string(available_paths, item->string);
}

int finish_delayed_checkout(struct checkout *state)
{
	int errs = 0;
	struct string_list_item *filter, *path;
	struct delayed_checkout *dco = state->delayed_checkout;

	if (!state->delayed_checkout) {
		return errs;
	}

	while (dco->filters.nr > 0) {
		for_each_string_list_item(filter, &dco->filters) {
			struct string_list available_paths;
			string_list_init(&available_paths, 0);

			if (!async_query_available_blobs(filter->string, &available_paths)) {
				/* Filter reported an error */
				errs = 1;
				filter->string = "";
				continue;
			}
			if (available_paths.nr <= 0) {
				/* Filter responded with no entries. That means
				   the filter is done and we can remove the
				   filter from the list (see
				   "string_list_remove_empty_items" call below).
				*/
				filter->string = "";
				continue;
			}

			/* In dco->paths we store a list of all delayed paths.
			   The filter just send us a list of available paths.
			   Remove them from the list.
			*/
			filter_string_list(&dco->paths, 0,
				&remove_available_paths, &available_paths);

			for_each_string_list_item(path, &available_paths) {
				struct cache_entry* ce = index_file_exists(
					state->istate, path->string,
					strlen(path->string), 0);
				dco->state = CE_RETRY;
				errs |= (ce ? checkout_entry(ce, state, NULL) : 1);
			}
		}
		string_list_remove_empty_items(&dco->filters, 0);
	}
	string_list_clear(&dco->filters, 0);

	/* At this point we should not have any delayed paths anymore. */
	errs |= dco->paths.nr;

	free(dco);
	state->delayed_checkout = NULL;

	return errs;
}

static int write_entry(struct cache_entry *ce,
		       char *path, const struct checkout *state, int to_tempfile)
{
	unsigned int ce_mode_s_ifmt = ce->ce_mode & S_IFMT;
	int fd, ret, fstat_done = 0;
	char *new;
	struct strbuf buf = STRBUF_INIT;
	unsigned long size;
	size_t wrote, newsize = 0;
	struct stat st;
	const struct submodule *sub;

	if (ce_mode_s_ifmt == S_IFREG) {
		struct stream_filter *filter = get_stream_filter(ce->name,
								 ce->oid.hash);
		if (filter &&
		    !streaming_write_entry(ce, path, filter,
					   state, to_tempfile,
					   &fstat_done, &st))
			goto finish;
	}

	switch (ce_mode_s_ifmt) {
	case S_IFREG:
	case S_IFLNK:
		new = read_blob_entry(ce, &size);
		if (!new)
			return error("unable to read sha1 file of %s (%s)",
				path, oid_to_hex(&ce->oid));

		if (ce_mode_s_ifmt == S_IFLNK && has_symlinks && !to_tempfile) {
			ret = symlink(new, path);
			free(new);
			if (ret)
				return error_errno("unable to create symlink %s",
						   path);
			break;
		}

		/*
		 * Convert from git internal format to working tree format
		 */
		if (ce_mode_s_ifmt == S_IFREG) {
			struct delayed_checkout *dco = state->delayed_checkout;
			if (dco && dco->state != CE_NO_DELAY) {
				/* Do not send the blob in case of a retry. */
				if (dco->state == CE_RETRY) {
					new = NULL;
					size = 0;
				}
				ret = async_convert_to_working_tree(
					ce->name, new, size, &buf, dco);
				if (ret && dco->state == CE_DELAYED) {
					free(new);
					/* Reset the state of the next blob */
					dco->state = CE_CAN_DELAY;
					goto finish;
				}
			} else
				ret = convert_to_working_tree(
					ce->name, new, size, &buf);

			if (ret) {
				free(new);
				new = strbuf_detach(&buf, &newsize);
				size = newsize;
			}
			/*
			 * No "else" here as errors from convert are OK at this
			 * point. If the error would have been fatal (e.g.
			 * filter is required), then we would have died already.
			 */
		}

		fd = open_output_fd(path, ce, to_tempfile);
		if (fd < 0) {
			free(new);
			return error_errno("unable to create file %s", path);
		}

		wrote = write_in_full(fd, new, size);
		if (!to_tempfile)
			fstat_done = fstat_output(fd, state, &st);
		close(fd);
		free(new);
		if (wrote != size)
			return error("unable to write file %s", path);
		break;
	case S_IFGITLINK:
		if (to_tempfile)
			return error("cannot create temporary submodule %s", path);
		if (mkdir(path, 0777) < 0)
			return error("cannot create submodule directory %s", path);
		sub = submodule_from_ce(ce);
		if (sub)
			return submodule_move_head(ce->name,
				NULL, oid_to_hex(&ce->oid),
				state->force ? SUBMODULE_MOVE_HEAD_FORCE : 0);
		break;
	default:
		return error("unknown file mode for %s in index", path);
	}

finish:
	if (state->refresh_cache) {
		assert(state->istate);
		if (!fstat_done)
			lstat(ce->name, &st);
		fill_stat_cache_info(ce, &st);
		ce->ce_flags |= CE_UPDATE_IN_BASE;
		state->istate->cache_changed |= CE_ENTRY_CHANGED;
	}
	return 0;
}

/*
 * This is like 'lstat()', except it refuses to follow symlinks
 * in the path, after skipping "skiplen".
 */
static int check_path(const char *path, int len, struct stat *st, int skiplen)
{
	const char *slash = path + len;

	while (path < slash && *slash != '/')
		slash--;
	if (!has_dirs_only_path(path, slash - path, skiplen)) {
		errno = ENOENT;
		return -1;
	}
	return lstat(path, st);
}

/*
 * Write the contents from ce out to the working tree.
 *
 * When topath[] is not NULL, instead of writing to the working tree
 * file named by ce, a temporary file is created by this function and
 * its name is returned in topath[], which must be able to hold at
 * least TEMPORARY_FILENAME_LENGTH bytes long.
 */
int checkout_entry(struct cache_entry *ce,
		   const struct checkout *state, char *topath)
{
	static struct strbuf path = STRBUF_INIT;
	struct stat st;

	if (topath)
		return write_entry(ce, topath, state, 1);

	strbuf_reset(&path);
	strbuf_add(&path, state->base_dir, state->base_dir_len);
	strbuf_add(&path, ce->name, ce_namelen(ce));

	if (!check_path(path.buf, path.len, &st, state->base_dir_len)) {
		const struct submodule *sub;
		unsigned changed = ce_match_stat(ce, &st, CE_MATCH_IGNORE_VALID|CE_MATCH_IGNORE_SKIP_WORKTREE);
		/*
		 * Needs to be checked before !changed returns early,
		 * as the possibly empty directory was not changed
		 */
		sub = submodule_from_ce(ce);
		if (sub) {
			int err;
			if (!is_submodule_populated_gently(ce->name, &err)) {
				struct stat sb;
				if (lstat(ce->name, &sb))
					die(_("could not stat file '%s'"), ce->name);
				if (!(st.st_mode & S_IFDIR))
					unlink_or_warn(ce->name);

				return submodule_move_head(ce->name,
					NULL, oid_to_hex(&ce->oid), 0);
			} else
				return submodule_move_head(ce->name,
					"HEAD", oid_to_hex(&ce->oid),
					state->force ? SUBMODULE_MOVE_HEAD_FORCE : 0);
		}

		if (!changed)
			return 0;
		if (!state->force) {
			if (!state->quiet)
				fprintf(stderr,
					"%s already exists, no checkout\n",
					path.buf);
			return -1;
		}

		/*
		 * We unlink the old file, to get the new one with the
		 * right permissions (including umask, which is nasty
		 * to emulate by hand - much easier to let the system
		 * just do the right thing)
		 */
		if (S_ISDIR(st.st_mode)) {
			/* If it is a gitlink, leave it alone! */
			if (S_ISGITLINK(ce->ce_mode))
				return 0;
			if (!state->force)
				return error("%s is a directory", path.buf);
			remove_subtree(&path);
		} else if (unlink(path.buf))
			return error_errno("unable to unlink old '%s'", path.buf);
	} else if (state->not_new)
		return 0;

	create_directories(path.buf, path.len, state);
	return write_entry(ce, path.buf, state, 0);
}
