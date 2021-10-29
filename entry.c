#include "cache.h"
#include "blob.h"
#include "object-store.h"
#include "dir.h"
#include "streaming.h"
#include "submodule.h"
#include "progress.h"
#include "fsmonitor.h"
#include "entry.h"
#include "parallel-checkout.h"

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
	while ((de = readdir_skip_dot_and_dotdot(dir)) != NULL) {
		struct stat st;

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

void *read_blob_entry(const struct cache_entry *ce, size_t *size)
{
	enum object_type type;
	unsigned long ul;
	void *blob_data = read_object_file(&ce->oid, &type, &ul);

	*size = ul;
	if (blob_data) {
		if (type == OBJ_BLOB)
			return blob_data;
		free(blob_data);
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

int fstat_checkout_output(int fd, const struct checkout *state, struct stat *st)
{
	/* use fstat() only when path == ce->name */
	if (fstat_is_reliable() &&
	    state->refresh_cache && !state->base_dir_len) {
		return !fstat(fd, st);
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
	*fstat_done = fstat_checkout_output(fd, state, statbuf);
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
		string_list_init_nodup(&state->delayed_checkout->filters);
		string_list_init_nodup(&state->delayed_checkout->paths);
	}
}

static int remove_available_paths(struct string_list_item *item, void *cb_data)
{
	struct string_list *available_paths = cb_data;
	struct string_list_item *available;

	available = string_list_lookup(available_paths, item->string);
	if (available)
		available->util = (void *)item->string;
	return !available;
}

int finish_delayed_checkout(struct checkout *state, int *nr_checkouts,
			    int show_progress)
{
	int errs = 0;
	unsigned processed_paths = 0;
	off_t filtered_bytes = 0;
	struct string_list_item *filter, *path;
	struct progress *progress = NULL;
	struct delayed_checkout *dco = state->delayed_checkout;

	if (!state->delayed_checkout)
		return errs;

	dco->state = CE_RETRY;
	if (show_progress)
		progress = start_delayed_progress(_("Filtering content"), dco->paths.nr);
	while (dco->filters.nr > 0) {
		for_each_string_list_item(filter, &dco->filters) {
			struct string_list available_paths = STRING_LIST_INIT_NODUP;

			if (!async_query_available_blobs(filter->string, &available_paths)) {
				/* Filter reported an error */
				errs = 1;
				filter->string = "";
				continue;
			}
			if (available_paths.nr <= 0) {
				/*
				 * Filter responded with no entries. That means
				 * the filter is done and we can remove the
				 * filter from the list (see
				 * "string_list_remove_empty_items" call below).
				 */
				filter->string = "";
				continue;
			}

			/*
			 * In dco->paths we store a list of all delayed paths.
			 * The filter just send us a list of available paths.
			 * Remove them from the list.
			 */
			filter_string_list(&dco->paths, 0,
				&remove_available_paths, &available_paths);

			for_each_string_list_item(path, &available_paths) {
				struct cache_entry* ce;

				if (!path->util) {
					error("external filter '%s' signaled that '%s' "
					      "is now available although it has not been "
					      "delayed earlier",
					      filter->string, path->string);
					errs |= 1;

					/*
					 * Do not ask the filter for available blobs,
					 * again, as the filter is likely buggy.
					 */
					filter->string = "";
					continue;
				}
				ce = index_file_exists(state->istate, path->string,
						       strlen(path->string), 0);
				if (ce) {
					display_progress(progress, ++processed_paths);
					errs |= checkout_entry(ce, state, NULL, nr_checkouts);
					filtered_bytes += ce->ce_stat_data.sd_size;
					display_throughput(progress, filtered_bytes);
				} else
					errs = 1;
			}
		}
		string_list_remove_empty_items(&dco->filters, 0);
	}
	stop_progress(&progress);
	string_list_clear(&dco->filters, 0);

	/* At this point we should not have any delayed paths anymore. */
	errs |= dco->paths.nr;
	for_each_string_list_item(path, &dco->paths) {
		error("'%s' was not filtered properly", path->string);
	}
	string_list_clear(&dco->paths, 0);

	free(dco);
	state->delayed_checkout = NULL;

	return errs;
}

void update_ce_after_write(const struct checkout *state, struct cache_entry *ce,
			   struct stat *st)
{
	if (state->refresh_cache) {
		assert(state->istate);
		fill_stat_cache_info(state->istate, ce, st);
		ce->ce_flags |= CE_UPDATE_IN_BASE;
		mark_fsmonitor_invalid(state->istate, ce);
		state->istate->cache_changed |= CE_ENTRY_CHANGED;
	}
}

/* Note: ca is used (and required) iff the entry refers to a regular file. */
static int write_entry(struct cache_entry *ce, char *path, struct conv_attrs *ca,
		       const struct checkout *state, int to_tempfile)
{
	unsigned int ce_mode_s_ifmt = ce->ce_mode & S_IFMT;
	struct delayed_checkout *dco = state->delayed_checkout;
	int fd, ret, fstat_done = 0;
	char *new_blob;
	struct strbuf buf = STRBUF_INIT;
	size_t size;
	ssize_t wrote;
	size_t newsize = 0;
	struct stat st;
	const struct submodule *sub;
	struct checkout_metadata meta;

	clone_checkout_metadata(&meta, &state->meta, &ce->oid);

	if (ce_mode_s_ifmt == S_IFREG) {
		struct stream_filter *filter = get_stream_filter_ca(ca, &ce->oid);
		if (filter &&
		    !streaming_write_entry(ce, path, filter,
					   state, to_tempfile,
					   &fstat_done, &st))
			goto finish;
	}

	switch (ce_mode_s_ifmt) {
	case S_IFLNK:
		new_blob = read_blob_entry(ce, &size);
		if (!new_blob)
			return error("unable to read sha1 file of %s (%s)",
				     ce->name, oid_to_hex(&ce->oid));

		/*
		 * We can't make a real symlink; write out a regular file entry
		 * with the symlink destination as its contents.
		 */
		if (!has_symlinks || to_tempfile)
			goto write_file_entry;

		ret = create_symlink(state->istate, new_blob, path);
		free(new_blob);
		if (ret)
			return error_errno("unable to create symlink %s", path);
		break;

	case S_IFREG:
		/*
		 * We do not send the blob in case of a retry, so do not
		 * bother reading it at all.
		 */
		if (dco && dco->state == CE_RETRY) {
			new_blob = NULL;
			size = 0;
		} else {
			new_blob = read_blob_entry(ce, &size);
			if (!new_blob)
				return error("unable to read sha1 file of %s (%s)",
					     ce->name, oid_to_hex(&ce->oid));
		}

		/*
		 * Convert from git internal format to working tree format
		 */
		if (dco && dco->state != CE_NO_DELAY) {
			ret = async_convert_to_working_tree_ca(ca, ce->name,
							       new_blob, size,
							       &buf, &meta, dco);
			if (ret && string_list_has_string(&dco->paths, ce->name)) {
				free(new_blob);
				goto delayed;
			}
		} else {
			ret = convert_to_working_tree_ca(ca, ce->name, new_blob,
							 size, &buf, &meta);
		}

		if (ret) {
			free(new_blob);
			new_blob = strbuf_detach(&buf, &newsize);
			size = newsize;
		}
		/*
		 * No "else" here as errors from convert are OK at this
		 * point. If the error would have been fatal (e.g.
		 * filter is required), then we would have died already.
		 */

	write_file_entry:
		fd = open_output_fd(path, ce, to_tempfile);
		if (fd < 0) {
			free(new_blob);
			return error_errno("unable to create file %s", path);
		}

		wrote = write_in_full(fd, new_blob, size);
		if (!to_tempfile)
			fstat_done = fstat_checkout_output(fd, state, &st);
		close(fd);
		free(new_blob);
		if (wrote < 0)
			return error("unable to write file %s", path);
		break;

	case S_IFGITLINK:
		if (to_tempfile)
			return error("cannot create temporary submodule %s", ce->name);
		if (mkdir(path, 0777) < 0)
			return error("cannot create submodule directory %s", path);
		sub = submodule_from_ce(ce);
		if (sub)
			return submodule_move_head(ce->name,
				NULL, oid_to_hex(&ce->oid),
				state->force ? SUBMODULE_MOVE_HEAD_FORCE : 0);
		break;

	default:
		return error("unknown file mode for %s in index", ce->name);
	}

finish:
	/* Flush cached lstat in fscache after writing to disk. */
	flush_fscache();

	if (state->refresh_cache) {
		if (!fstat_done && lstat(ce->name, &st) < 0)
			return error_errno("unable to stat just-written file %s",
					   ce->name);
		update_ce_after_write(state, ce , &st);
	}
delayed:
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

static void mark_colliding_entries(const struct checkout *state,
				   struct cache_entry *ce, struct stat *st)
{
	int i, trust_ino = check_stat;

#if defined(GIT_WINDOWS_NATIVE) || defined(__CYGWIN__)
	trust_ino = 0;
#endif

	ce->ce_flags |= CE_MATCHED;

	/* TODO: audit for interaction with sparse-index. */
	ensure_full_index(state->istate);
	for (i = 0; i < state->istate->cache_nr; i++) {
		struct cache_entry *dup = state->istate->cache[i];

		if (dup == ce) {
			/*
			 * Parallel checkout doesn't create the files in index
			 * order. So the other side of the collision may appear
			 * after the given cache_entry in the array.
			 */
			if (parallel_checkout_status() == PC_RUNNING)
				continue;
			else
				break;
		}

		if (dup->ce_flags & (CE_MATCHED | CE_VALID | CE_SKIP_WORKTREE))
			continue;

		if ((trust_ino && !match_stat_data(&dup->ce_stat_data, st)) ||
		    (!trust_ino && !fspathcmp(ce->name, dup->name))) {
			dup->ce_flags |= CE_MATCHED;
			break;
		}
	}
}

int checkout_entry_ca(struct cache_entry *ce, struct conv_attrs *ca,
		      const struct checkout *state, char *topath,
		      int *nr_checkouts)
{
	static struct strbuf path = STRBUF_INIT;
	struct stat st;
	struct conv_attrs ca_buf;

	if (ce->ce_flags & CE_WT_REMOVE) {
		if (topath)
			/*
			 * No content and thus no path to create, so we have
			 * no pathname to return.
			 */
			BUG("Can't remove entry to a path");
		unlink_entry(ce);
		return 0;
	}

	if (topath) {
		if (S_ISREG(ce->ce_mode) && !ca) {
			convert_attrs(state->istate, &ca_buf, ce->name);
			ca = &ca_buf;
		}
		return write_entry(ce, topath, ca, state, 1);
	}

	strbuf_reset(&path);
	strbuf_add(&path, state->base_dir, state->base_dir_len);
	strbuf_add(&path, ce->name, ce_namelen(ce));

	if (!check_path(path.buf, path.len, &st, state->base_dir_len)) {
		const struct submodule *sub;
		unsigned changed = ie_match_stat(state->istate, ce, &st,
						 CE_MATCH_IGNORE_VALID | CE_MATCH_IGNORE_SKIP_WORKTREE);
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

		if (state->clone)
			mark_colliding_entries(state, ce, &st);

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
			remove_subtree(&path);
		} else if (unlink(path.buf))
			return error_errno("unable to unlink old '%s'", path.buf);
	} else if (state->not_new)
		return 0;

	create_directories(path.buf, path.len, state);

	if (nr_checkouts)
		(*nr_checkouts)++;

	if (S_ISREG(ce->ce_mode) && !ca) {
		convert_attrs(state->istate, &ca_buf, ce->name);
		ca = &ca_buf;
	}

	if (!enqueue_checkout(ce, ca))
		return 0;

	return write_entry(ce, path.buf, ca, state, 0);
}

void unlink_entry(const struct cache_entry *ce)
{
	const struct submodule *sub = submodule_from_ce(ce);
	if (sub) {
		/* state.force is set at the caller. */
		submodule_move_head(ce->name, "HEAD", NULL,
				    SUBMODULE_MOVE_HEAD_FORCE);
	}
	if (check_leading_path(ce->name, ce_namelen(ce), 1) >= 0)
		return;
	if (remove_or_warn(ce->ce_mode, ce->name))
		return;
	schedule_dir_for_removal(ce->name, ce_namelen(ce));
}
