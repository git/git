/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "stack.h"

#include "system.h"
#include "constants.h"
#include "merged.h"
#include "reader.h"
#include "reftable-error.h"
#include "reftable-record.h"
#include "reftable-merged.h"
#include "writer.h"

static int stack_try_add(struct reftable_stack *st,
			 int (*write_table)(struct reftable_writer *wr,
					    void *arg),
			 void *arg);
static int stack_write_compact(struct reftable_stack *st,
			       struct reftable_writer *wr,
			       size_t first, size_t last,
			       struct reftable_log_expiry_config *config);
static void reftable_addition_close(struct reftable_addition *add);
static int reftable_stack_reload_maybe_reuse(struct reftable_stack *st,
					     int reuse_open);

static int stack_filename(struct reftable_buf *dest, struct reftable_stack *st,
			  const char *name)
{
	int err;
	reftable_buf_reset(dest);
	if ((err = reftable_buf_addstr(dest, st->reftable_dir)) < 0 ||
	    (err = reftable_buf_addstr(dest, "/")) < 0 ||
	    (err = reftable_buf_addstr(dest, name)) < 0)
		return err;
	return 0;
}

static int stack_fsync(const struct reftable_write_options *opts, int fd)
{
	if (opts->fsync)
		return opts->fsync(fd);
	return fsync(fd);
}

struct fd_writer {
	const struct reftable_write_options *opts;
	int fd;
};

static ssize_t fd_writer_write(void *arg, const void *data, size_t sz)
{
	struct fd_writer *writer = arg;
	return write_in_full(writer->fd, data, sz);
}

static int fd_writer_flush(void *arg)
{
	struct fd_writer *writer = arg;
	return stack_fsync(writer->opts, writer->fd);
}

int reftable_new_stack(struct reftable_stack **dest, const char *dir,
		       const struct reftable_write_options *_opts)
{
	struct reftable_buf list_file_name = REFTABLE_BUF_INIT;
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *p;
	int err;

	p = reftable_calloc(1, sizeof(*p));
	if (!p) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto out;
	}

	if (_opts)
		opts = *_opts;
	if (opts.hash_id == 0)
		opts.hash_id = REFTABLE_HASH_SHA1;

	*dest = NULL;

	reftable_buf_reset(&list_file_name);
	if ((err = reftable_buf_addstr(&list_file_name, dir)) < 0 ||
	    (err = reftable_buf_addstr(&list_file_name, "/tables.list")) < 0)
		goto out;

	p->list_file = reftable_buf_detach(&list_file_name);
	p->list_fd = -1;
	p->opts = opts;
	p->reftable_dir = reftable_strdup(dir);
	if (!p->reftable_dir) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto out;
	}

	err = reftable_stack_reload_maybe_reuse(p, 1);
	if (err < 0)
		goto out;

	*dest = p;
	err = 0;

out:
	if (err < 0)
		reftable_stack_destroy(p);
	return err;
}

static int fd_read_lines(int fd, char ***namesp)
{
	off_t size = lseek(fd, 0, SEEK_END);
	char *buf = NULL;
	int err = 0;
	if (size < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}
	err = lseek(fd, 0, SEEK_SET);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	REFTABLE_ALLOC_ARRAY(buf, size + 1);
	if (!buf) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto done;
	}

	if (read_in_full(fd, buf, size) != size) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}
	buf[size] = 0;

	*namesp = parse_names(buf, size);
	if (!*namesp) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto done;
	}

done:
	reftable_free(buf);
	return err;
}

int read_lines(const char *filename, char ***namesp)
{
	int fd = open(filename, O_RDONLY);
	int err = 0;
	if (fd < 0) {
		if (errno == ENOENT) {
			REFTABLE_CALLOC_ARRAY(*namesp, 1);
			if (!*namesp)
				return REFTABLE_OUT_OF_MEMORY_ERROR;
			return 0;
		}

		return REFTABLE_IO_ERROR;
	}
	err = fd_read_lines(fd, namesp);
	close(fd);
	return err;
}

int reftable_stack_init_ref_iterator(struct reftable_stack *st,
				      struct reftable_iterator *it)
{
	return merged_table_init_iter(reftable_stack_merged_table(st),
				      it, BLOCK_TYPE_REF);
}

int reftable_stack_init_log_iterator(struct reftable_stack *st,
				     struct reftable_iterator *it)
{
	return merged_table_init_iter(reftable_stack_merged_table(st),
				      it, BLOCK_TYPE_LOG);
}

struct reftable_merged_table *
reftable_stack_merged_table(struct reftable_stack *st)
{
	return st->merged;
}

static int has_name(char **names, const char *name)
{
	while (*names) {
		if (!strcmp(*names, name))
			return 1;
		names++;
	}
	return 0;
}

/* Close and free the stack */
void reftable_stack_destroy(struct reftable_stack *st)
{
	char **names = NULL;
	int err = 0;

	if (!st)
		return;

	if (st->merged) {
		reftable_merged_table_free(st->merged);
		st->merged = NULL;
	}

	err = read_lines(st->list_file, &names);
	if (err < 0) {
		REFTABLE_FREE_AND_NULL(names);
	}

	if (st->readers) {
		int i = 0;
		struct reftable_buf filename = REFTABLE_BUF_INIT;
		for (i = 0; i < st->readers_len; i++) {
			const char *name = reader_name(st->readers[i]);
			int try_unlinking = 1;

			reftable_buf_reset(&filename);
			if (names && !has_name(names, name)) {
				if (stack_filename(&filename, st, name) < 0)
					try_unlinking = 0;
			}
			reftable_reader_decref(st->readers[i]);

			if (try_unlinking && filename.len) {
				/* On Windows, can only unlink after closing. */
				unlink(filename.buf);
			}
		}
		reftable_buf_release(&filename);
		st->readers_len = 0;
		REFTABLE_FREE_AND_NULL(st->readers);
	}

	if (st->list_fd >= 0) {
		close(st->list_fd);
		st->list_fd = -1;
	}

	REFTABLE_FREE_AND_NULL(st->list_file);
	REFTABLE_FREE_AND_NULL(st->reftable_dir);
	reftable_free(st);
	free_names(names);
}

static struct reftable_reader **stack_copy_readers(struct reftable_stack *st,
						   size_t cur_len)
{
	struct reftable_reader **cur = reftable_calloc(cur_len, sizeof(*cur));
	if (!cur)
		return NULL;
	for (size_t i = 0; i < cur_len; i++)
		cur[i] = st->readers[i];
	return cur;
}

static int reftable_stack_reload_once(struct reftable_stack *st,
				      const char **names,
				      int reuse_open)
{
	size_t cur_len = !st->merged ? 0 : st->merged->readers_len;
	struct reftable_reader **cur = NULL;
	struct reftable_reader **reused = NULL;
	struct reftable_reader **new_readers = NULL;
	size_t reused_len = 0, reused_alloc = 0, names_len;
	size_t new_readers_len = 0;
	struct reftable_merged_table *new_merged = NULL;
	struct reftable_buf table_path = REFTABLE_BUF_INIT;
	int err = 0;
	size_t i;

	if (cur_len) {
		cur = stack_copy_readers(st, cur_len);
		if (!cur) {
			err = REFTABLE_OUT_OF_MEMORY_ERROR;
			goto done;
		}
	}

	names_len = names_length(names);

	if (names_len) {
		new_readers = reftable_calloc(names_len, sizeof(*new_readers));
		if (!new_readers) {
			err = REFTABLE_OUT_OF_MEMORY_ERROR;
			goto done;
		}
	}

	while (*names) {
		struct reftable_reader *rd = NULL;
		const char *name = *names++;

		/* this is linear; we assume compaction keeps the number of
		   tables under control so this is not quadratic. */
		for (i = 0; reuse_open && i < cur_len; i++) {
			if (cur[i] && 0 == strcmp(cur[i]->name, name)) {
				rd = cur[i];
				cur[i] = NULL;

				/*
				 * When reloading the stack fails, we end up
				 * releasing all new readers. This also
				 * includes the reused readers, even though
				 * they are still in used by the old stack. We
				 * thus need to keep them alive here, which we
				 * do by bumping their refcount.
				 */
				REFTABLE_ALLOC_GROW_OR_NULL(reused,
							    reused_len + 1,
							    reused_alloc);
				if (!reused) {
					err = REFTABLE_OUT_OF_MEMORY_ERROR;
					goto done;
				}
				reused[reused_len++] = rd;
				reftable_reader_incref(rd);
				break;
			}
		}

		if (!rd) {
			struct reftable_block_source src = { NULL };

			err = stack_filename(&table_path, st, name);
			if (err < 0)
				goto done;

			err = reftable_block_source_from_file(&src,
							      table_path.buf);
			if (err < 0)
				goto done;

			err = reftable_reader_new(&rd, &src, name);
			if (err < 0)
				goto done;
		}

		new_readers[new_readers_len] = rd;
		new_readers_len++;
	}

	/* success! */
	err = reftable_merged_table_new(&new_merged, new_readers,
					new_readers_len, st->opts.hash_id);
	if (err < 0)
		goto done;

	/*
	 * Close the old, non-reused readers and proactively try to unlink
	 * them. This is done for systems like Windows, where the underlying
	 * file of such an open reader wouldn't have been possible to be
	 * unlinked by the compacting process.
	 */
	for (i = 0; i < cur_len; i++) {
		if (cur[i]) {
			const char *name = reader_name(cur[i]);

			err = stack_filename(&table_path, st, name);
			if (err < 0)
				goto done;

			reftable_reader_decref(cur[i]);
			unlink(table_path.buf);
		}
	}

	/* Update the stack to point to the new tables. */
	if (st->merged)
		reftable_merged_table_free(st->merged);
	new_merged->suppress_deletions = 1;
	st->merged = new_merged;

	if (st->readers)
		reftable_free(st->readers);
	st->readers = new_readers;
	st->readers_len = new_readers_len;
	new_readers = NULL;
	new_readers_len = 0;

	/*
	 * Decrement the refcount of reused readers again. This only needs to
	 * happen on the successful case, because on the unsuccessful one we
	 * decrement their refcount via `new_readers`.
	 */
	for (i = 0; i < reused_len; i++)
		reftable_reader_decref(reused[i]);

done:
	for (i = 0; i < new_readers_len; i++)
		reftable_reader_decref(new_readers[i]);
	reftable_free(new_readers);
	reftable_free(reused);
	reftable_free(cur);
	reftable_buf_release(&table_path);
	return err;
}

/* return negative if a before b. */
static int tv_cmp(struct timeval *a, struct timeval *b)
{
	time_t diff = a->tv_sec - b->tv_sec;
	int udiff = a->tv_usec - b->tv_usec;

	if (diff != 0)
		return diff;

	return udiff;
}

static int reftable_stack_reload_maybe_reuse(struct reftable_stack *st,
					     int reuse_open)
{
	char **names = NULL, **names_after = NULL;
	struct timeval deadline;
	int64_t delay = 0;
	int tries = 0, err;
	int fd = -1;

	err = gettimeofday(&deadline, NULL);
	if (err < 0)
		goto out;
	deadline.tv_sec += 3;

	while (1) {
		struct timeval now;

		err = gettimeofday(&now, NULL);
		if (err < 0)
			goto out;

		/*
		 * Only look at deadlines after the first few times. This
		 * simplifies debugging in GDB.
		 */
		tries++;
		if (tries > 3 && tv_cmp(&now, &deadline) >= 0)
			goto out;

		fd = open(st->list_file, O_RDONLY);
		if (fd < 0) {
			if (errno != ENOENT) {
				err = REFTABLE_IO_ERROR;
				goto out;
			}

			REFTABLE_CALLOC_ARRAY(names, 1);
			if (!names) {
				err = REFTABLE_OUT_OF_MEMORY_ERROR;
				goto out;
			}
		} else {
			err = fd_read_lines(fd, &names);
			if (err < 0)
				goto out;
		}

		err = reftable_stack_reload_once(st, (const char **) names, reuse_open);
		if (!err)
			break;
		if (err != REFTABLE_NOT_EXIST_ERROR)
			goto out;

		/*
		 * REFTABLE_NOT_EXIST_ERROR can be caused by a concurrent
		 * writer. Check if there was one by checking if the name list
		 * changed.
		 */
		err = read_lines(st->list_file, &names_after);
		if (err < 0)
			goto out;
		if (names_equal((const char **) names_after,
				(const char **) names)) {
			err = REFTABLE_NOT_EXIST_ERROR;
			goto out;
		}

		free_names(names);
		names = NULL;
		free_names(names_after);
		names_after = NULL;
		close(fd);
		fd = -1;

		delay = delay + (delay * rand()) / RAND_MAX + 1;
		sleep_millisec(delay);
	}

out:
	/*
	 * Invalidate the stat cache. It is sufficient to only close the file
	 * descriptor and keep the cached stat info because we never use the
	 * latter when the former is negative.
	 */
	if (st->list_fd >= 0) {
		close(st->list_fd);
		st->list_fd = -1;
	}

	/*
	 * Cache stat information in case it provides a useful signal to us.
	 * According to POSIX, "The st_ino and st_dev fields taken together
	 * uniquely identify the file within the system." That being said,
	 * Windows is not POSIX compliant and we do not have these fields
	 * available. So the information we have there is insufficient to
	 * determine whether two file descriptors point to the same file.
	 *
	 * While we could fall back to using other signals like the file's
	 * mtime, those are not sufficient to avoid races. We thus refrain from
	 * using the stat cache on such systems and fall back to the secondary
	 * caching mechanism, which is to check whether contents of the file
	 * have changed.
	 *
	 * On other systems which are POSIX compliant we must keep the file
	 * descriptor open. This is to avoid a race condition where two
	 * processes access the reftable stack at the same point in time:
	 *
	 *   1. A reads the reftable stack and caches its stat info.
	 *
	 *   2. B updates the stack, appending a new table to "tables.list".
	 *      This will both use a new inode and result in a different file
	 *      size, thus invalidating A's cache in theory.
	 *
	 *   3. B decides to auto-compact the stack and merges two tables. The
	 *      file size now matches what A has cached again. Furthermore, the
	 *      filesystem may decide to recycle the inode number of the file
	 *      we have replaced in (2) because it is not in use anymore.
	 *
	 *   4. A reloads the reftable stack. Neither the inode number nor the
	 *      file size changed. If the timestamps did not change either then
	 *      we think the cached copy of our stack is up-to-date.
	 *
	 * By keeping the file descriptor open the inode number cannot be
	 * recycled, mitigating the race.
	 */
	if (!err && fd >= 0 && !fstat(fd, &st->list_st) &&
	    st->list_st.st_dev && st->list_st.st_ino) {
		st->list_fd = fd;
		fd = -1;
	}

	if (fd >= 0)
		close(fd);
	free_names(names);
	free_names(names_after);

	if (st->opts.on_reload)
		st->opts.on_reload(st->opts.on_reload_payload);

	return err;
}

/* -1 = error
 0 = up to date
 1 = changed. */
static int stack_uptodate(struct reftable_stack *st)
{
	char **names = NULL;
	int err;
	int i = 0;

	/*
	 * When we have cached stat information available then we use it to
	 * verify whether the file has been rewritten.
	 *
	 * Note that we explicitly do not want to use `stat_validity_check()`
	 * and friends here because they may end up not comparing the `st_dev`
	 * and `st_ino` fields. These functions thus cannot guarantee that we
	 * indeed still have the same file.
	 */
	if (st->list_fd >= 0) {
		struct stat list_st;

		if (stat(st->list_file, &list_st) < 0) {
			/*
			 * It's fine for "tables.list" to not exist. In that
			 * case, we have to refresh when the loaded stack has
			 * any readers.
			 */
			if (errno == ENOENT)
				return !!st->readers_len;
			return REFTABLE_IO_ERROR;
		}

		/*
		 * When "tables.list" refers to the same file we can assume
		 * that it didn't change. This is because we always use
		 * rename(3P) to update the file and never write to it
		 * directly.
		 */
		if (st->list_st.st_dev == list_st.st_dev &&
		    st->list_st.st_ino == list_st.st_ino)
			return 0;
	}

	err = read_lines(st->list_file, &names);
	if (err < 0)
		return err;

	for (i = 0; i < st->readers_len; i++) {
		if (!names[i]) {
			err = 1;
			goto done;
		}

		if (strcmp(st->readers[i]->name, names[i])) {
			err = 1;
			goto done;
		}
	}

	if (names[st->merged->readers_len]) {
		err = 1;
		goto done;
	}

done:
	free_names(names);
	return err;
}

int reftable_stack_reload(struct reftable_stack *st)
{
	int err = stack_uptodate(st);
	if (err > 0)
		return reftable_stack_reload_maybe_reuse(st, 1);
	return err;
}

int reftable_stack_add(struct reftable_stack *st,
		       int (*write)(struct reftable_writer *wr, void *arg),
		       void *arg)
{
	int err = stack_try_add(st, write, arg);
	if (err < 0) {
		if (err == REFTABLE_OUTDATED_ERROR) {
			/* Ignore error return, we want to propagate
			   REFTABLE_OUTDATED_ERROR.
			*/
			reftable_stack_reload(st);
		}
		return err;
	}

	return 0;
}

static int format_name(struct reftable_buf *dest, uint64_t min, uint64_t max)
{
	char buf[100];
	uint32_t rnd = (uint32_t)git_rand();
	snprintf(buf, sizeof(buf), "0x%012" PRIx64 "-0x%012" PRIx64 "-%08x",
		 min, max, rnd);
	reftable_buf_reset(dest);
	return reftable_buf_addstr(dest, buf);
}

struct reftable_addition {
	struct reftable_flock tables_list_lock;
	struct reftable_stack *stack;

	char **new_tables;
	size_t new_tables_len, new_tables_cap;
	uint64_t next_update_index;
};

#define REFTABLE_ADDITION_INIT {0}

static int reftable_stack_init_addition(struct reftable_addition *add,
					struct reftable_stack *st,
					unsigned int flags)
{
	struct reftable_buf lock_file_name = REFTABLE_BUF_INIT;
	int err;

	add->stack = st;

	err = flock_acquire(&add->tables_list_lock, st->list_file,
			    st->opts.lock_timeout_ms);
	if (err < 0) {
		if (errno == EEXIST) {
			err = REFTABLE_LOCK_ERROR;
		} else {
			err = REFTABLE_IO_ERROR;
		}
		goto done;
	}
	if (st->opts.default_permissions) {
		if (chmod(add->tables_list_lock.path,
			  st->opts.default_permissions) < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	err = stack_uptodate(st);
	if (err < 0)
		goto done;
	if (err > 0 && flags & REFTABLE_STACK_NEW_ADDITION_RELOAD) {
		err = reftable_stack_reload_maybe_reuse(add->stack, 1);
		if (err)
			goto done;
	}
	if (err > 0) {
		err = REFTABLE_OUTDATED_ERROR;
		goto done;
	}

	add->next_update_index = reftable_stack_next_update_index(st);
done:
	if (err)
		reftable_addition_close(add);
	reftable_buf_release(&lock_file_name);
	return err;
}

static void reftable_addition_close(struct reftable_addition *add)
{
	struct reftable_buf nm = REFTABLE_BUF_INIT;
	size_t i;

	for (i = 0; i < add->new_tables_len; i++) {
		if (!stack_filename(&nm, add->stack, add->new_tables[i]))
			unlink(nm.buf);
		reftable_free(add->new_tables[i]);
		add->new_tables[i] = NULL;
	}
	reftable_free(add->new_tables);
	add->new_tables = NULL;
	add->new_tables_len = 0;
	add->new_tables_cap = 0;

	flock_release(&add->tables_list_lock);
	reftable_buf_release(&nm);
}

void reftable_addition_destroy(struct reftable_addition *add)
{
	if (!add) {
		return;
	}
	reftable_addition_close(add);
	reftable_free(add);
}

int reftable_addition_commit(struct reftable_addition *add)
{
	struct reftable_buf table_list = REFTABLE_BUF_INIT;
	int err = 0;
	size_t i;

	if (add->new_tables_len == 0)
		goto done;

	for (i = 0; i < add->stack->merged->readers_len; i++) {
		if ((err = reftable_buf_addstr(&table_list, add->stack->readers[i]->name)) < 0 ||
		    (err = reftable_buf_addstr(&table_list, "\n")) < 0)
			goto done;
	}
	for (i = 0; i < add->new_tables_len; i++) {
		if ((err = reftable_buf_addstr(&table_list, add->new_tables[i])) < 0 ||
		    (err = reftable_buf_addstr(&table_list, "\n")) < 0)
			goto done;
	}

	err = write_in_full(add->tables_list_lock.fd, table_list.buf, table_list.len);
	reftable_buf_release(&table_list);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	err = stack_fsync(&add->stack->opts, add->tables_list_lock.fd);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	err = flock_commit(&add->tables_list_lock);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	/* success, no more state to clean up. */
	for (i = 0; i < add->new_tables_len; i++)
		reftable_free(add->new_tables[i]);
	reftable_free(add->new_tables);
	add->new_tables = NULL;
	add->new_tables_len = 0;
	add->new_tables_cap = 0;

	err = reftable_stack_reload_maybe_reuse(add->stack, 1);
	if (err)
		goto done;

	if (!add->stack->opts.disable_auto_compact) {
		/*
		 * Auto-compact the stack to keep the number of tables in
		 * control. It is possible that a concurrent writer is already
		 * trying to compact parts of the stack, which would lead to a
		 * `REFTABLE_LOCK_ERROR` because parts of the stack are locked
		 * already. This is a benign error though, so we ignore it.
		 */
		err = reftable_stack_auto_compact(add->stack);
		if (err < 0 && err != REFTABLE_LOCK_ERROR)
			goto done;
		err = 0;
	}

done:
	reftable_addition_close(add);
	return err;
}

int reftable_stack_new_addition(struct reftable_addition **dest,
				struct reftable_stack *st,
				unsigned int flags)
{
	int err = 0;
	struct reftable_addition empty = REFTABLE_ADDITION_INIT;

	REFTABLE_CALLOC_ARRAY(*dest, 1);
	if (!*dest)
		return REFTABLE_OUT_OF_MEMORY_ERROR;

	**dest = empty;
	err = reftable_stack_init_addition(*dest, st, flags);
	if (err) {
		reftable_free(*dest);
		*dest = NULL;
	}
	return err;
}

static int stack_try_add(struct reftable_stack *st,
			 int (*write_table)(struct reftable_writer *wr,
					    void *arg),
			 void *arg)
{
	struct reftable_addition add = REFTABLE_ADDITION_INIT;
	int err = reftable_stack_init_addition(&add, st, 0);
	if (err < 0)
		goto done;

	err = reftable_addition_add(&add, write_table, arg);
	if (err < 0)
		goto done;

	err = reftable_addition_commit(&add);
done:
	reftable_addition_close(&add);
	return err;
}

int reftable_addition_add(struct reftable_addition *add,
			  int (*write_table)(struct reftable_writer *wr,
					     void *arg),
			  void *arg)
{
	struct reftable_buf temp_tab_file_name = REFTABLE_BUF_INIT;
	struct reftable_buf tab_file_name = REFTABLE_BUF_INIT;
	struct reftable_buf next_name = REFTABLE_BUF_INIT;
	struct reftable_writer *wr = NULL;
	struct reftable_tmpfile tab_file = REFTABLE_TMPFILE_INIT;
	struct fd_writer writer = {
		.opts = &add->stack->opts,
	};
	int err = 0;

	reftable_buf_reset(&next_name);

	err = format_name(&next_name, add->next_update_index, add->next_update_index);
	if (err < 0)
		goto done;

	err = stack_filename(&temp_tab_file_name, add->stack, next_name.buf);
	if (err < 0)
		goto done;

	err = reftable_buf_addstr(&temp_tab_file_name, ".temp.XXXXXX");
	if (err < 0)
		goto done;

	err = tmpfile_from_pattern(&tab_file, temp_tab_file_name.buf);
	if (err < 0)
		goto done;
	if (add->stack->opts.default_permissions) {
		if (chmod(tab_file.path,
			  add->stack->opts.default_permissions)) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	writer.fd = tab_file.fd;
	err = reftable_writer_new(&wr, fd_writer_write, fd_writer_flush,
				  &writer, &add->stack->opts);
	if (err < 0)
		goto done;

	err = write_table(wr, arg);
	if (err < 0)
		goto done;

	err = reftable_writer_close(wr);
	if (err == REFTABLE_EMPTY_TABLE_ERROR) {
		err = 0;
		goto done;
	}
	if (err < 0)
		goto done;

	err = tmpfile_close(&tab_file);
	if (err < 0)
		goto done;

	if (wr->min_update_index < add->next_update_index) {
		err = REFTABLE_API_ERROR;
		goto done;
	}

	err = format_name(&next_name, wr->min_update_index, wr->max_update_index);
	if (err < 0)
		goto done;

	err = reftable_buf_addstr(&next_name, ".ref");
	if (err < 0)
		goto done;

	err = stack_filename(&tab_file_name, add->stack, next_name.buf);
	if (err < 0)
		goto done;

	/*
	  On windows, this relies on rand() picking a unique destination name.
	  Maybe we should do retry loop as well?
	 */
	err = tmpfile_rename(&tab_file, tab_file_name.buf);
	if (err < 0)
		goto done;

	REFTABLE_ALLOC_GROW_OR_NULL(add->new_tables, add->new_tables_len + 1,
				    add->new_tables_cap);
	if (!add->new_tables) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto done;
	}
	add->new_tables[add->new_tables_len++] = reftable_buf_detach(&next_name);

done:
	tmpfile_delete(&tab_file);
	reftable_buf_release(&temp_tab_file_name);
	reftable_buf_release(&tab_file_name);
	reftable_buf_release(&next_name);
	reftable_writer_free(wr);
	return err;
}

uint64_t reftable_stack_next_update_index(struct reftable_stack *st)
{
	int sz = st->merged->readers_len;
	if (sz > 0)
		return reftable_reader_max_update_index(st->readers[sz - 1]) +
		       1;
	return 1;
}

static int stack_compact_locked(struct reftable_stack *st,
				size_t first, size_t last,
				struct reftable_log_expiry_config *config,
				struct reftable_tmpfile *tab_file_out)
{
	struct reftable_buf next_name = REFTABLE_BUF_INIT;
	struct reftable_buf tab_file_path = REFTABLE_BUF_INIT;
	struct reftable_writer *wr = NULL;
	struct fd_writer writer=  {
		.opts = &st->opts,
	};
	struct reftable_tmpfile tab_file = REFTABLE_TMPFILE_INIT;
	int err = 0;

	err = format_name(&next_name, reftable_reader_min_update_index(st->readers[first]),
			  reftable_reader_max_update_index(st->readers[last]));
	if (err < 0)
		goto done;

	err = stack_filename(&tab_file_path, st, next_name.buf);
	if (err < 0)
		goto done;

	err = reftable_buf_addstr(&tab_file_path, ".temp.XXXXXX");
	if (err < 0)
		goto done;

	err = tmpfile_from_pattern(&tab_file, tab_file_path.buf);
	if (err < 0)
		goto done;

	if (st->opts.default_permissions &&
	    chmod(tab_file.path, st->opts.default_permissions) < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	writer.fd = tab_file.fd;
	err = reftable_writer_new(&wr, fd_writer_write, fd_writer_flush,
				  &writer, &st->opts);
	if (err < 0)
		goto done;

	err = stack_write_compact(st, wr, first, last, config);
	if (err < 0)
		goto done;

	err = reftable_writer_close(wr);
	if (err < 0)
		goto done;

	err = tmpfile_close(&tab_file);
	if (err < 0)
		goto done;

	*tab_file_out = tab_file;
	tab_file = REFTABLE_TMPFILE_INIT;

done:
	tmpfile_delete(&tab_file);
	reftable_writer_free(wr);
	reftable_buf_release(&next_name);
	reftable_buf_release(&tab_file_path);
	return err;
}

static int stack_write_compact(struct reftable_stack *st,
			       struct reftable_writer *wr,
			       size_t first, size_t last,
			       struct reftable_log_expiry_config *config)
{
	struct reftable_merged_table *mt = NULL;
	struct reftable_iterator it = { NULL };
	struct reftable_ref_record ref = { NULL };
	struct reftable_log_record log = { NULL };
	size_t subtabs_len = last - first + 1;
	uint64_t entries = 0;
	int err = 0;

	for (size_t i = first; i <= last; i++)
		st->stats.bytes += st->readers[i]->size;
	reftable_writer_set_limits(wr, st->readers[first]->min_update_index,
				   st->readers[last]->max_update_index);

	err = reftable_merged_table_new(&mt, st->readers + first, subtabs_len,
					st->opts.hash_id);
	if (err < 0)
		goto done;

	err = merged_table_init_iter(mt, &it, BLOCK_TYPE_REF);
	if (err < 0)
		goto done;

	err = reftable_iterator_seek_ref(&it, "");
	if (err < 0)
		goto done;

	while (1) {
		err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0) {
			err = 0;
			break;
		}
		if (err < 0)
			goto done;

		if (first == 0 && reftable_ref_record_is_deletion(&ref)) {
			continue;
		}

		err = reftable_writer_add_ref(wr, &ref);
		if (err < 0)
			goto done;
		entries++;
	}
	reftable_iterator_destroy(&it);

	err = merged_table_init_iter(mt, &it, BLOCK_TYPE_LOG);
	if (err < 0)
		goto done;

	err = reftable_iterator_seek_log(&it, "");
	if (err < 0)
		goto done;

	while (1) {
		err = reftable_iterator_next_log(&it, &log);
		if (err > 0) {
			err = 0;
			break;
		}
		if (err < 0)
			goto done;
		if (first == 0 && reftable_log_record_is_deletion(&log)) {
			continue;
		}

		if (config && config->min_update_index > 0 &&
		    log.update_index < config->min_update_index) {
			continue;
		}

		if (config && config->time > 0 &&
		    log.value.update.time < config->time) {
			continue;
		}

		err = reftable_writer_add_log(wr, &log);
		if (err < 0)
			goto done;
		entries++;
	}

done:
	reftable_iterator_destroy(&it);
	if (mt)
		reftable_merged_table_free(mt);
	reftable_ref_record_release(&ref);
	reftable_log_record_release(&log);
	st->stats.entries_written += entries;
	return err;
}

enum stack_compact_range_flags {
	/*
	 * Perform a best-effort compaction. That is, even if we cannot lock
	 * all tables in the specified range, we will try to compact the
	 * remaining slice.
	 */
	STACK_COMPACT_RANGE_BEST_EFFORT = (1 << 0),
};

/*
 * Compact all tables in the range `[first, last)` into a single new table.
 *
 * This function returns `0` on success or a code `< 0` on failure. When the
 * stack or any of the tables in the specified range are already locked then
 * this function returns `REFTABLE_LOCK_ERROR`. This is a benign error that
 * callers can either ignore, or they may choose to retry compaction after some
 * amount of time.
 */
static int stack_compact_range(struct reftable_stack *st,
			       size_t first, size_t last,
			       struct reftable_log_expiry_config *expiry,
			       unsigned int flags)
{
	struct reftable_buf tables_list_buf = REFTABLE_BUF_INIT;
	struct reftable_buf new_table_name = REFTABLE_BUF_INIT;
	struct reftable_buf new_table_path = REFTABLE_BUF_INIT;
	struct reftable_buf table_name = REFTABLE_BUF_INIT;
	struct reftable_flock tables_list_lock = REFTABLE_FLOCK_INIT;
	struct reftable_flock *table_locks = NULL;
	struct reftable_tmpfile new_table = REFTABLE_TMPFILE_INIT;
	int is_empty_table = 0, err = 0;
	size_t first_to_replace, last_to_replace;
	size_t i, nlocks = 0;
	char **names = NULL;

	if (first > last || (!expiry && first == last)) {
		err = 0;
		goto done;
	}

	st->stats.attempts++;

	/*
	 * Hold the lock so that we can read "tables.list" and lock all tables
	 * which are part of the user-specified range.
	 */
	err = flock_acquire(&tables_list_lock, st->list_file, st->opts.lock_timeout_ms);
	if (err < 0) {
		if (errno == EEXIST)
			err = REFTABLE_LOCK_ERROR;
		else
			err = REFTABLE_IO_ERROR;
		goto done;
	}

	err = stack_uptodate(st);
	if (err)
		goto done;

	/*
	 * Lock all tables in the user-provided range. This is the slice of our
	 * stack which we'll compact.
	 *
	 * Note that we lock tables in reverse order from last to first. The
	 * intent behind this is to allow a newer process to perform best
	 * effort compaction of tables that it has added in the case where an
	 * older process is still busy compacting tables which are preexisting
	 * from the point of view of the newer process.
	 */
	REFTABLE_ALLOC_ARRAY(table_locks, last - first + 1);
	if (!table_locks) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto done;
	}
	for (i = 0; i < last - first + 1; i++)
		table_locks[i] = REFTABLE_FLOCK_INIT;

	for (i = last + 1; i > first; i--) {
		err = stack_filename(&table_name, st, reader_name(st->readers[i - 1]));
		if (err < 0)
			goto done;

		err = flock_acquire(&table_locks[nlocks], table_name.buf, 0);
		if (err < 0) {
			/*
			 * When the table is locked already we may do a
			 * best-effort compaction and compact only the tables
			 * that we have managed to lock so far. This of course
			 * requires that we have been able to lock at least two
			 * tables, otherwise there would be nothing to compact.
			 * In that case, we return a lock error to our caller.
			 */
			if (errno == EEXIST && last - (i - 1) >= 2 &&
			    flags & STACK_COMPACT_RANGE_BEST_EFFORT) {
				err = 0;
				/*
				 * The subtraction is to offset the index, the
				 * addition is to only compact up to the table
				 * of the preceding iteration. They obviously
				 * cancel each other out, but that may be
				 * non-obvious when it was omitted.
				 */
				first = (i - 1) + 1;
				break;
			} else if (errno == EEXIST) {
				err = REFTABLE_LOCK_ERROR;
				goto done;
			} else {
				err = REFTABLE_IO_ERROR;
				goto done;
			}
		}

		/*
		 * We need to close the lockfiles as we might otherwise easily
		 * run into file descriptor exhaustion when we compress a lot
		 * of tables.
		 */
		err = flock_close(&table_locks[nlocks++]);
		if (err < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	/*
	 * We have locked all tables in our range and can thus release the
	 * "tables.list" lock while compacting the locked tables. This allows
	 * concurrent updates to the stack to proceed.
	 */
	err = flock_release(&tables_list_lock);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	/*
	 * Compact the now-locked tables into a new table. Note that compacting
	 * these tables may end up with an empty new table in case tombstones
	 * end up cancelling out all refs in that range.
	 */
	err = stack_compact_locked(st, first, last, expiry, &new_table);
	if (err < 0) {
		if (err != REFTABLE_EMPTY_TABLE_ERROR)
			goto done;
		is_empty_table = 1;
	}

	/*
	 * Now that we have written the new, compacted table we need to re-lock
	 * "tables.list". We'll then replace the compacted range of tables with
	 * the new table.
	 */
	err = flock_acquire(&tables_list_lock, st->list_file, st->opts.lock_timeout_ms);
	if (err < 0) {
		if (errno == EEXIST)
			err = REFTABLE_LOCK_ERROR;
		else
			err = REFTABLE_IO_ERROR;
		goto done;
	}

	if (st->opts.default_permissions) {
		if (chmod(tables_list_lock.path,
			  st->opts.default_permissions) < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	/*
	 * As we have unlocked the stack while compacting our slice of tables
	 * it may have happened that a concurrently running process has updated
	 * the stack while we were compacting. In that case, we need to check
	 * whether the tables that we have just compacted still exist in the
	 * stack in the exact same order as we have compacted them.
	 *
	 * If they do exist, then it is fine to continue and replace those
	 * tables with our compacted version. If they don't, then we need to
	 * abort.
	 */
	err = stack_uptodate(st);
	if (err < 0)
		goto done;
	if (err > 0) {
		ssize_t new_offset = -1;
		int fd;

		fd = open(st->list_file, O_RDONLY);
		if (fd < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}

		err = fd_read_lines(fd, &names);
		close(fd);
		if (err < 0)
			goto done;

		/*
		 * Search for the offset of the first table that we have
		 * compacted in the updated "tables.list" file.
		 */
		for (size_t i = 0; names[i]; i++) {
			if (strcmp(names[i], st->readers[first]->name))
				continue;

			/*
			 * We have found the first entry. Verify that all the
			 * subsequent tables we have compacted still exist in
			 * the modified stack in the exact same order as we
			 * have compacted them.
			 */
			for (size_t j = 1; j < last - first + 1; j++) {
				const char *old = first + j < st->merged->readers_len ?
					st->readers[first + j]->name : NULL;
				const char *new = names[i + j];

				/*
				 * If some entries are missing or in case the tables
				 * have changed then we need to bail out. Again, this
				 * shouldn't ever happen because we have locked the
				 * tables we are compacting.
				 */
				if (!old || !new || strcmp(old, new)) {
					err = REFTABLE_OUTDATED_ERROR;
					goto done;
				}
			}

			new_offset = i;
			break;
		}

		/*
		 * In case we didn't find our compacted tables in the stack we
		 * need to bail out. In theory, this should have never happened
		 * because we locked the tables we are compacting.
		 */
		if (new_offset < 0) {
			err = REFTABLE_OUTDATED_ERROR;
			goto done;
		}

		/*
		 * We have found the new range that we want to replace, so
		 * let's update the range of tables that we want to replace.
		 */
		first_to_replace = new_offset;
		last_to_replace = last + (new_offset - first);
	} else {
		/*
		 * `fd_read_lines()` uses a `NULL` sentinel to indicate that
		 * the array is at its end. As we use `free_names()` to free
		 * the array, we need to include this sentinel value here and
		 * thus have to allocate `readers_len + 1` many entries.
		 */
		REFTABLE_CALLOC_ARRAY(names, st->merged->readers_len + 1);
		if (!names) {
			err = REFTABLE_OUT_OF_MEMORY_ERROR;
			goto done;
		}

		for (size_t i = 0; i < st->merged->readers_len; i++) {
			names[i] = reftable_strdup(st->readers[i]->name);
			if (!names[i]) {
				err = REFTABLE_OUT_OF_MEMORY_ERROR;
				goto done;
			}
		}
		first_to_replace = first;
		last_to_replace = last;
	}

	/*
	 * If the resulting compacted table is not empty, then we need to move
	 * it into place now.
	 */
	if (!is_empty_table) {
		err = format_name(&new_table_name, st->readers[first]->min_update_index,
				  st->readers[last]->max_update_index);
		if (err < 0)
			goto done;

		err = reftable_buf_addstr(&new_table_name, ".ref");
		if (err < 0)
			goto done;

		err = stack_filename(&new_table_path, st, new_table_name.buf);
		if (err < 0)
			goto done;

		err = tmpfile_rename(&new_table, new_table_path.buf);
		if (err < 0)
			goto done;
	}

	/*
	 * Write the new "tables.list" contents with the compacted table we
	 * have just written. In case the compacted table became empty we
	 * simply skip writing it.
	 */
	for (i = 0; i < first_to_replace; i++) {
		if ((err = reftable_buf_addstr(&tables_list_buf, names[i])) < 0 ||
		    (err = reftable_buf_addstr(&tables_list_buf, "\n")) < 0)
		      goto done;
	}
	if (!is_empty_table) {
		if ((err = reftable_buf_addstr(&tables_list_buf, new_table_name.buf)) < 0 ||
		    (err = reftable_buf_addstr(&tables_list_buf, "\n")) < 0)
			goto done;
	}
	for (i = last_to_replace + 1; names[i]; i++) {
		if ((err = reftable_buf_addstr(&tables_list_buf, names[i])) < 0 ||
		    (err = reftable_buf_addstr(&tables_list_buf, "\n")) < 0)
			goto done;
	}

	err = write_in_full(tables_list_lock.fd,
			    tables_list_buf.buf, tables_list_buf.len);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		unlink(new_table_path.buf);
		goto done;
	}

	err = stack_fsync(&st->opts, tables_list_lock.fd);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		unlink(new_table_path.buf);
		goto done;
	}

	err = flock_commit(&tables_list_lock);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		unlink(new_table_path.buf);
		goto done;
	}

	/*
	 * Reload the stack before deleting the compacted tables. We can only
	 * delete the files after we closed them on Windows, so this needs to
	 * happen first.
	 */
	err = reftable_stack_reload_maybe_reuse(st, first < last);
	if (err < 0)
		goto done;

	/*
	 * Delete the old tables. They may still be in use by concurrent
	 * readers, so it is expected that unlinking tables may fail.
	 */
	for (i = 0; i < nlocks; i++) {
		struct reftable_flock *table_lock = &table_locks[i];

		reftable_buf_reset(&table_name);
		err = reftable_buf_add(&table_name, table_lock->path,
				       strlen(table_lock->path) - strlen(".lock"));
		if (err)
			continue;

		unlink(table_name.buf);
	}

done:
	flock_release(&tables_list_lock);
	for (i = 0; table_locks && i < nlocks; i++)
		flock_release(&table_locks[i]);
	reftable_free(table_locks);

	tmpfile_delete(&new_table);
	reftable_buf_release(&new_table_name);
	reftable_buf_release(&new_table_path);
	reftable_buf_release(&tables_list_buf);
	reftable_buf_release(&table_name);
	free_names(names);

	if (err == REFTABLE_LOCK_ERROR)
		st->stats.failures++;

	return err;
}

int reftable_stack_compact_all(struct reftable_stack *st,
			       struct reftable_log_expiry_config *config)
{
	size_t last = st->merged->readers_len ? st->merged->readers_len - 1 : 0;
	return stack_compact_range(st, 0, last, config, 0);
}

static int segment_size(struct segment *s)
{
	return s->end - s->start;
}

struct segment suggest_compaction_segment(uint64_t *sizes, size_t n,
					  uint8_t factor)
{
	struct segment seg = { 0 };
	uint64_t bytes;
	size_t i;

	if (!factor)
		factor = DEFAULT_GEOMETRIC_FACTOR;

	/*
	 * If there are no tables or only a single one then we don't have to
	 * compact anything. The sequence is geometric by definition already.
	 */
	if (n <= 1)
		return seg;

	/*
	 * Find the ending table of the compaction segment needed to restore the
	 * geometric sequence. Note that the segment end is exclusive.
	 *
	 * To do so, we iterate backwards starting from the most recent table
	 * until a valid segment end is found. If the preceding table is smaller
	 * than the current table multiplied by the geometric factor (2), the
	 * compaction segment end has been identified.
	 *
	 * Tables after the ending point are not added to the byte count because
	 * they are already valid members of the geometric sequence. Due to the
	 * properties of a geometric sequence, it is not possible for the sum of
	 * these tables to exceed the value of the ending point table.
	 *
	 * Example table size sequence requiring no compaction:
	 * 	64, 32, 16, 8, 4, 2, 1
	 *
	 * Example table size sequence where compaction segment end is set to
	 * the last table. Since the segment end is exclusive, the last table is
	 * excluded during subsequent compaction and the table with size 3 is
	 * the final table included:
	 * 	64, 32, 16, 8, 4, 3, 1
	 */
	for (i = n - 1; i > 0; i--) {
		if (sizes[i - 1] < sizes[i] * factor) {
			seg.end = i + 1;
			bytes = sizes[i];
			break;
		}
	}

	/*
	 * Find the starting table of the compaction segment by iterating
	 * through the remaining tables and keeping track of the accumulated
	 * size of all tables seen from the segment end table. The previous
	 * table is compared to the accumulated size because the tables from the
	 * segment end are merged backwards recursively.
	 *
	 * Note that we keep iterating even after we have found the first
	 * starting point. This is because there may be tables in the stack
	 * preceding that first starting point which violate the geometric
	 * sequence.
	 *
	 * Example compaction segment start set to table with size 32:
	 * 	128, 32, 16, 8, 4, 3, 1
	 */
	for (; i > 0; i--) {
		uint64_t curr = bytes;
		bytes += sizes[i - 1];

		if (sizes[i - 1] < curr * factor) {
			seg.start = i - 1;
			seg.bytes = bytes;
		}
	}

	return seg;
}

static uint64_t *stack_table_sizes_for_compaction(struct reftable_stack *st)
{
	int version = (st->opts.hash_id == REFTABLE_HASH_SHA1) ? 1 : 2;
	int overhead = header_size(version) - 1;
	uint64_t *sizes;

	REFTABLE_CALLOC_ARRAY(sizes, st->merged->readers_len);
	if (!sizes)
		return NULL;

	for (size_t i = 0; i < st->merged->readers_len; i++)
		sizes[i] = st->readers[i]->size - overhead;

	return sizes;
}

int reftable_stack_auto_compact(struct reftable_stack *st)
{
	struct segment seg;
	uint64_t *sizes;

	if (st->merged->readers_len < 2)
		return 0;

	sizes = stack_table_sizes_for_compaction(st);
	if (!sizes)
		return REFTABLE_OUT_OF_MEMORY_ERROR;

	seg = suggest_compaction_segment(sizes, st->merged->readers_len,
					 st->opts.auto_compaction_factor);
	reftable_free(sizes);

	if (segment_size(&seg) > 0)
		return stack_compact_range(st, seg.start, seg.end - 1,
					   NULL, STACK_COMPACT_RANGE_BEST_EFFORT);

	return 0;
}

struct reftable_compaction_stats *
reftable_stack_compaction_stats(struct reftable_stack *st)
{
	return &st->stats;
}

int reftable_stack_read_ref(struct reftable_stack *st, const char *refname,
			    struct reftable_ref_record *ref)
{
	struct reftable_iterator it = { 0 };
	int ret;

	ret = reftable_merged_table_init_ref_iterator(st->merged, &it);
	if (ret)
		goto out;

	ret = reftable_iterator_seek_ref(&it, refname);
	if (ret)
		goto out;

	ret = reftable_iterator_next_ref(&it, ref);
	if (ret)
		goto out;

	if (strcmp(ref->refname, refname) ||
	    reftable_ref_record_is_deletion(ref)) {
		reftable_ref_record_release(ref);
		ret = 1;
		goto out;
	}

out:
	reftable_iterator_destroy(&it);
	return ret;
}

int reftable_stack_read_log(struct reftable_stack *st, const char *refname,
			    struct reftable_log_record *log)
{
	struct reftable_iterator it = {0};
	int err;

	err = reftable_stack_init_log_iterator(st, &it);
	if (err)
		goto done;

	err = reftable_iterator_seek_log(&it, refname);
	if (err)
		goto done;

	err = reftable_iterator_next_log(&it, log);
	if (err)
		goto done;

	if (strcmp(log->refname, refname) ||
	    reftable_log_record_is_deletion(log)) {
		err = 1;
		goto done;
	}

done:
	if (err) {
		reftable_log_record_release(log);
	}
	reftable_iterator_destroy(&it);
	return err;
}

static int is_table_name(const char *s)
{
	const char *dot = strrchr(s, '.');
	return dot && !strcmp(dot, ".ref");
}

static void remove_maybe_stale_table(struct reftable_stack *st, uint64_t max,
				     const char *name)
{
	int err = 0;
	uint64_t update_idx = 0;
	struct reftable_block_source src = { NULL };
	struct reftable_reader *rd = NULL;
	struct reftable_buf table_path = REFTABLE_BUF_INIT;

	err = stack_filename(&table_path, st, name);
	if (err < 0)
		goto done;

	err = reftable_block_source_from_file(&src, table_path.buf);
	if (err < 0)
		goto done;

	err = reftable_reader_new(&rd, &src, name);
	if (err < 0)
		goto done;

	update_idx = reftable_reader_max_update_index(rd);
	reftable_reader_decref(rd);

	if (update_idx <= max) {
		unlink(table_path.buf);
	}
done:
	reftable_buf_release(&table_path);
}

static int reftable_stack_clean_locked(struct reftable_stack *st)
{
	uint64_t max = reftable_merged_table_max_update_index(
		reftable_stack_merged_table(st));
	DIR *dir = opendir(st->reftable_dir);
	struct dirent *d = NULL;
	if (!dir) {
		return REFTABLE_IO_ERROR;
	}

	while ((d = readdir(dir))) {
		int i = 0;
		int found = 0;
		if (!is_table_name(d->d_name))
			continue;

		for (i = 0; !found && i < st->readers_len; i++) {
			found = !strcmp(reader_name(st->readers[i]), d->d_name);
		}
		if (found)
			continue;

		remove_maybe_stale_table(st, max, d->d_name);
	}

	closedir(dir);
	return 0;
}

int reftable_stack_clean(struct reftable_stack *st)
{
	struct reftable_addition *add = NULL;
	int err = reftable_stack_new_addition(&add, st, 0);
	if (err < 0) {
		goto done;
	}

	err = reftable_stack_reload(st);
	if (err < 0) {
		goto done;
	}

	err = reftable_stack_clean_locked(st);

done:
	reftable_addition_destroy(add);
	return err;
}

enum reftable_hash reftable_stack_hash_id(struct reftable_stack *st)
{
	return reftable_merged_table_hash_id(st->merged);
}
