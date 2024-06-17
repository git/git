/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "stack.h"

#include "../write-or-die.h"
#include "system.h"
#include "constants.h"
#include "merged.h"
#include "reader.h"
#include "reftable-error.h"
#include "reftable-generic.h"
#include "reftable-record.h"
#include "reftable-merged.h"
#include "writer.h"
#include "tempfile.h"

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

static void stack_filename(struct strbuf *dest, struct reftable_stack *st,
			   const char *name)
{
	strbuf_reset(dest);
	strbuf_addstr(dest, st->reftable_dir);
	strbuf_addstr(dest, "/");
	strbuf_addstr(dest, name);
}

static ssize_t reftable_fd_write(void *arg, const void *data, size_t sz)
{
	int *fdp = (int *)arg;
	return write_in_full(*fdp, data, sz);
}

static int reftable_fd_flush(void *arg)
{
	int *fdp = (int *)arg;

	return fsync_component(FSYNC_COMPONENT_REFERENCE, *fdp);
}

int reftable_new_stack(struct reftable_stack **dest, const char *dir,
		       const struct reftable_write_options *_opts)
{
	struct reftable_stack *p = reftable_calloc(1, sizeof(*p));
	struct strbuf list_file_name = STRBUF_INIT;
	struct reftable_write_options opts = {0};
	int err = 0;

	if (_opts)
		opts = *_opts;
	if (opts.hash_id == 0)
		opts.hash_id = GIT_SHA1_FORMAT_ID;

	*dest = NULL;

	strbuf_reset(&list_file_name);
	strbuf_addstr(&list_file_name, dir);
	strbuf_addstr(&list_file_name, "/tables.list");

	p->list_file = strbuf_detach(&list_file_name, NULL);
	p->list_fd = -1;
	p->reftable_dir = xstrdup(dir);
	p->opts = opts;

	err = reftable_stack_reload_maybe_reuse(p, 1);
	if (err < 0) {
		reftable_stack_destroy(p);
	} else {
		*dest = p;
	}
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
	if (read_in_full(fd, buf, size) != size) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}
	buf[size] = 0;

	parse_names(buf, size, namesp);

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
			return 0;
		}

		return REFTABLE_IO_ERROR;
	}
	err = fd_read_lines(fd, namesp);
	close(fd);
	return err;
}

void reftable_stack_init_ref_iterator(struct reftable_stack *st,
				      struct reftable_iterator *it)
{
	merged_table_init_iter(reftable_stack_merged_table(st),
			       it, BLOCK_TYPE_REF);
}

void reftable_stack_init_log_iterator(struct reftable_stack *st,
				      struct reftable_iterator *it)
{
	merged_table_init_iter(reftable_stack_merged_table(st),
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
	if (st->merged) {
		reftable_merged_table_free(st->merged);
		st->merged = NULL;
	}

	err = read_lines(st->list_file, &names);
	if (err < 0) {
		FREE_AND_NULL(names);
	}

	if (st->readers) {
		int i = 0;
		struct strbuf filename = STRBUF_INIT;
		for (i = 0; i < st->readers_len; i++) {
			const char *name = reader_name(st->readers[i]);
			strbuf_reset(&filename);
			if (names && !has_name(names, name)) {
				stack_filename(&filename, st, name);
			}
			reftable_reader_free(st->readers[i]);

			if (filename.len) {
				/* On Windows, can only unlink after closing. */
				unlink(filename.buf);
			}
		}
		strbuf_release(&filename);
		st->readers_len = 0;
		FREE_AND_NULL(st->readers);
	}

	if (st->list_fd >= 0) {
		close(st->list_fd);
		st->list_fd = -1;
	}

	FREE_AND_NULL(st->list_file);
	FREE_AND_NULL(st->reftable_dir);
	reftable_free(st);
	free_names(names);
}

static struct reftable_reader **stack_copy_readers(struct reftable_stack *st,
						   int cur_len)
{
	struct reftable_reader **cur = reftable_calloc(cur_len, sizeof(*cur));
	int i = 0;
	for (i = 0; i < cur_len; i++) {
		cur[i] = st->readers[i];
	}
	return cur;
}

static int reftable_stack_reload_once(struct reftable_stack *st,
				      const char **names,
				      int reuse_open)
{
	size_t cur_len = !st->merged ? 0 : st->merged->stack_len;
	struct reftable_reader **cur = stack_copy_readers(st, cur_len);
	size_t names_len = names_length(names);
	struct reftable_reader **new_readers =
		reftable_calloc(names_len, sizeof(*new_readers));
	struct reftable_table *new_tables =
		reftable_calloc(names_len, sizeof(*new_tables));
	size_t new_readers_len = 0;
	struct reftable_merged_table *new_merged = NULL;
	struct strbuf table_path = STRBUF_INIT;
	int err = 0;
	size_t i;

	while (*names) {
		struct reftable_reader *rd = NULL;
		const char *name = *names++;

		/* this is linear; we assume compaction keeps the number of
		   tables under control so this is not quadratic. */
		for (i = 0; reuse_open && i < cur_len; i++) {
			if (cur[i] && 0 == strcmp(cur[i]->name, name)) {
				rd = cur[i];
				cur[i] = NULL;
				break;
			}
		}

		if (!rd) {
			struct reftable_block_source src = { NULL };
			stack_filename(&table_path, st, name);

			err = reftable_block_source_from_file(&src,
							      table_path.buf);
			if (err < 0)
				goto done;

			err = reftable_new_reader(&rd, &src, name);
			if (err < 0)
				goto done;
		}

		new_readers[new_readers_len] = rd;
		reftable_table_from_reader(&new_tables[new_readers_len], rd);
		new_readers_len++;
	}

	/* success! */
	err = reftable_new_merged_table(&new_merged, new_tables,
					new_readers_len, st->opts.hash_id);
	if (err < 0)
		goto done;

	new_tables = NULL;
	st->readers_len = new_readers_len;
	if (st->merged)
		reftable_merged_table_free(st->merged);
	if (st->readers) {
		reftable_free(st->readers);
	}
	st->readers = new_readers;
	new_readers = NULL;
	new_readers_len = 0;

	new_merged->suppress_deletions = 1;
	st->merged = new_merged;
	for (i = 0; i < cur_len; i++) {
		if (cur[i]) {
			const char *name = reader_name(cur[i]);
			stack_filename(&table_path, st, name);

			reader_close(cur[i]);
			reftable_reader_free(cur[i]);

			/* On Windows, can only unlink after closing. */
			unlink(table_path.buf);
		}
	}

done:
	for (i = 0; i < new_readers_len; i++) {
		reader_close(new_readers[i]);
		reftable_reader_free(new_readers[i]);
	}
	reftable_free(new_readers);
	reftable_free(new_tables);
	reftable_free(cur);
	strbuf_release(&table_path);
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

	if (names[st->merged->stack_len]) {
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

static void format_name(struct strbuf *dest, uint64_t min, uint64_t max)
{
	char buf[100];
	uint32_t rnd = (uint32_t)git_rand();
	snprintf(buf, sizeof(buf), "0x%012" PRIx64 "-0x%012" PRIx64 "-%08x",
		 min, max, rnd);
	strbuf_reset(dest);
	strbuf_addstr(dest, buf);
}

struct reftable_addition {
	struct tempfile *lock_file;
	struct reftable_stack *stack;

	char **new_tables;
	size_t new_tables_len, new_tables_cap;
	uint64_t next_update_index;
};

#define REFTABLE_ADDITION_INIT {0}

static int reftable_stack_init_addition(struct reftable_addition *add,
					struct reftable_stack *st)
{
	struct strbuf lock_file_name = STRBUF_INIT;
	int err = 0;
	add->stack = st;

	strbuf_addf(&lock_file_name, "%s.lock", st->list_file);

	add->lock_file = create_tempfile(lock_file_name.buf);
	if (!add->lock_file) {
		if (errno == EEXIST) {
			err = REFTABLE_LOCK_ERROR;
		} else {
			err = REFTABLE_IO_ERROR;
		}
		goto done;
	}
	if (st->opts.default_permissions) {
		if (chmod(add->lock_file->filename.buf, st->opts.default_permissions) < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	err = stack_uptodate(st);
	if (err < 0)
		goto done;
	if (err > 0) {
		err = REFTABLE_OUTDATED_ERROR;
		goto done;
	}

	add->next_update_index = reftable_stack_next_update_index(st);
done:
	if (err) {
		reftable_addition_close(add);
	}
	strbuf_release(&lock_file_name);
	return err;
}

static void reftable_addition_close(struct reftable_addition *add)
{
	struct strbuf nm = STRBUF_INIT;
	size_t i;

	for (i = 0; i < add->new_tables_len; i++) {
		stack_filename(&nm, add->stack, add->new_tables[i]);
		unlink(nm.buf);
		reftable_free(add->new_tables[i]);
		add->new_tables[i] = NULL;
	}
	reftable_free(add->new_tables);
	add->new_tables = NULL;
	add->new_tables_len = 0;
	add->new_tables_cap = 0;

	delete_tempfile(&add->lock_file);
	strbuf_release(&nm);
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
	struct strbuf table_list = STRBUF_INIT;
	int lock_file_fd = get_tempfile_fd(add->lock_file);
	int err = 0;
	size_t i;

	if (add->new_tables_len == 0)
		goto done;

	for (i = 0; i < add->stack->merged->stack_len; i++) {
		strbuf_addstr(&table_list, add->stack->readers[i]->name);
		strbuf_addstr(&table_list, "\n");
	}
	for (i = 0; i < add->new_tables_len; i++) {
		strbuf_addstr(&table_list, add->new_tables[i]);
		strbuf_addstr(&table_list, "\n");
	}

	err = write_in_full(lock_file_fd, table_list.buf, table_list.len);
	strbuf_release(&table_list);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	fsync_component_or_die(FSYNC_COMPONENT_REFERENCE, lock_file_fd,
			       get_tempfile_path(add->lock_file));

	err = rename_tempfile(&add->lock_file, add->stack->list_file);
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
				struct reftable_stack *st)
{
	int err = 0;
	struct reftable_addition empty = REFTABLE_ADDITION_INIT;
	REFTABLE_CALLOC_ARRAY(*dest, 1);
	**dest = empty;
	err = reftable_stack_init_addition(*dest, st);
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
	int err = reftable_stack_init_addition(&add, st);
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
	struct strbuf temp_tab_file_name = STRBUF_INIT;
	struct strbuf tab_file_name = STRBUF_INIT;
	struct strbuf next_name = STRBUF_INIT;
	struct reftable_writer *wr = NULL;
	struct tempfile *tab_file = NULL;
	int err = 0;
	int tab_fd;

	strbuf_reset(&next_name);
	format_name(&next_name, add->next_update_index, add->next_update_index);

	stack_filename(&temp_tab_file_name, add->stack, next_name.buf);
	strbuf_addstr(&temp_tab_file_name, ".temp.XXXXXX");

	tab_file = mks_tempfile(temp_tab_file_name.buf);
	if (!tab_file) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}
	if (add->stack->opts.default_permissions) {
		if (chmod(get_tempfile_path(tab_file),
			  add->stack->opts.default_permissions)) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}
	tab_fd = get_tempfile_fd(tab_file);

	wr = reftable_new_writer(reftable_fd_write, reftable_fd_flush, &tab_fd,
				 &add->stack->opts);
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

	err = close_tempfile_gently(tab_file);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	if (wr->min_update_index < add->next_update_index) {
		err = REFTABLE_API_ERROR;
		goto done;
	}

	format_name(&next_name, wr->min_update_index, wr->max_update_index);
	strbuf_addstr(&next_name, ".ref");
	stack_filename(&tab_file_name, add->stack, next_name.buf);

	/*
	  On windows, this relies on rand() picking a unique destination name.
	  Maybe we should do retry loop as well?
	 */
	err = rename_tempfile(&tab_file, tab_file_name.buf);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	REFTABLE_ALLOC_GROW(add->new_tables, add->new_tables_len + 1,
			    add->new_tables_cap);
	add->new_tables[add->new_tables_len++] = strbuf_detach(&next_name, NULL);
done:
	delete_tempfile(&tab_file);
	strbuf_release(&temp_tab_file_name);
	strbuf_release(&tab_file_name);
	strbuf_release(&next_name);
	reftable_writer_free(wr);
	return err;
}

uint64_t reftable_stack_next_update_index(struct reftable_stack *st)
{
	int sz = st->merged->stack_len;
	if (sz > 0)
		return reftable_reader_max_update_index(st->readers[sz - 1]) +
		       1;
	return 1;
}

static int stack_compact_locked(struct reftable_stack *st,
				size_t first, size_t last,
				struct reftable_log_expiry_config *config,
				struct tempfile **tab_file_out)
{
	struct strbuf next_name = STRBUF_INIT;
	struct strbuf tab_file_path = STRBUF_INIT;
	struct reftable_writer *wr = NULL;
	struct tempfile *tab_file;
	int tab_fd, err = 0;

	format_name(&next_name,
		    reftable_reader_min_update_index(st->readers[first]),
		    reftable_reader_max_update_index(st->readers[last]));
	stack_filename(&tab_file_path, st, next_name.buf);
	strbuf_addstr(&tab_file_path, ".temp.XXXXXX");

	tab_file = mks_tempfile(tab_file_path.buf);
	if (!tab_file) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}
	tab_fd = get_tempfile_fd(tab_file);

	if (st->opts.default_permissions &&
	    chmod(get_tempfile_path(tab_file), st->opts.default_permissions) < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	wr = reftable_new_writer(reftable_fd_write, reftable_fd_flush,
				 &tab_fd, &st->opts);
	err = stack_write_compact(st, wr, first, last, config);
	if (err < 0)
		goto done;

	err = reftable_writer_close(wr);
	if (err < 0)
		goto done;

	err = close_tempfile_gently(tab_file);
	if (err < 0)
		goto done;

	*tab_file_out = tab_file;
	tab_file = NULL;

done:
	delete_tempfile(&tab_file);
	reftable_writer_free(wr);
	strbuf_release(&next_name);
	strbuf_release(&tab_file_path);
	return err;
}

static int stack_write_compact(struct reftable_stack *st,
			       struct reftable_writer *wr,
			       size_t first, size_t last,
			       struct reftable_log_expiry_config *config)
{
	size_t subtabs_len = last - first + 1;
	struct reftable_table *subtabs = reftable_calloc(
		last - first + 1, sizeof(*subtabs));
	struct reftable_merged_table *mt = NULL;
	struct reftable_iterator it = { NULL };
	struct reftable_ref_record ref = { NULL };
	struct reftable_log_record log = { NULL };
	uint64_t entries = 0;
	int err = 0;

	for (size_t i = first, j = 0; i <= last; i++) {
		struct reftable_reader *t = st->readers[i];
		reftable_table_from_reader(&subtabs[j++], t);
		st->stats.bytes += t->size;
	}
	reftable_writer_set_limits(wr, st->readers[first]->min_update_index,
				   st->readers[last]->max_update_index);

	err = reftable_new_merged_table(&mt, subtabs, subtabs_len,
					st->opts.hash_id);
	if (err < 0) {
		reftable_free(subtabs);
		goto done;
	}

	merged_table_init_iter(mt, &it, BLOCK_TYPE_REF);
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

	merged_table_init_iter(mt, &it, BLOCK_TYPE_LOG);
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
			       struct reftable_log_expiry_config *expiry)
{
	struct strbuf tables_list_buf = STRBUF_INIT;
	struct strbuf new_table_name = STRBUF_INIT;
	struct strbuf new_table_path = STRBUF_INIT;
	struct strbuf table_name = STRBUF_INIT;
	struct lock_file tables_list_lock = LOCK_INIT;
	struct lock_file *table_locks = NULL;
	struct tempfile *new_table = NULL;
	int is_empty_table = 0, err = 0;
	size_t i;

	if (first > last || (!expiry && first == last)) {
		err = 0;
		goto done;
	}

	st->stats.attempts++;

	/*
	 * Hold the lock so that we can read "tables.list" and lock all tables
	 * which are part of the user-specified range.
	 */
	err = hold_lock_file_for_update(&tables_list_lock, st->list_file,
					LOCK_NO_DEREF);
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
	 */
	REFTABLE_CALLOC_ARRAY(table_locks, last - first + 1);
	for (i = first; i <= last; i++) {
		stack_filename(&table_name, st, reader_name(st->readers[i]));

		err = hold_lock_file_for_update(&table_locks[i - first],
						table_name.buf, LOCK_NO_DEREF);
		if (err < 0) {
			if (errno == EEXIST)
				err = REFTABLE_LOCK_ERROR;
			else
				err = REFTABLE_IO_ERROR;
			goto done;
		}

		/*
		 * We need to close the lockfiles as we might otherwise easily
		 * run into file descriptor exhaustion when we compress a lot
		 * of tables.
		 */
		err = close_lock_file_gently(&table_locks[i - first]);
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
	err = rollback_lock_file(&tables_list_lock);
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
	err = hold_lock_file_for_update(&tables_list_lock, st->list_file,
					LOCK_NO_DEREF);
	if (err < 0) {
		if (errno == EEXIST)
			err = REFTABLE_LOCK_ERROR;
		else
			err = REFTABLE_IO_ERROR;
		goto done;
	}

	if (st->opts.default_permissions) {
		if (chmod(get_lock_file_path(&tables_list_lock),
			  st->opts.default_permissions) < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	/*
	 * If the resulting compacted table is not empty, then we need to move
	 * it into place now.
	 */
	if (!is_empty_table) {
		format_name(&new_table_name, st->readers[first]->min_update_index,
			    st->readers[last]->max_update_index);
		strbuf_addstr(&new_table_name, ".ref");
		stack_filename(&new_table_path, st, new_table_name.buf);

		err = rename_tempfile(&new_table, new_table_path.buf);
		if (err < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	/*
	 * Write the new "tables.list" contents with the compacted table we
	 * have just written. In case the compacted table became empty we
	 * simply skip writing it.
	 */
	for (i = 0; i < first; i++)
		strbuf_addf(&tables_list_buf, "%s\n", st->readers[i]->name);
	if (!is_empty_table)
		strbuf_addf(&tables_list_buf, "%s\n", new_table_name.buf);
	for (i = last + 1; i < st->merged->stack_len; i++)
		strbuf_addf(&tables_list_buf, "%s\n", st->readers[i]->name);

	err = write_in_full(get_lock_file_fd(&tables_list_lock),
			    tables_list_buf.buf, tables_list_buf.len);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		unlink(new_table_path.buf);
		goto done;
	}

	err = fsync_component(FSYNC_COMPONENT_REFERENCE, get_lock_file_fd(&tables_list_lock));
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		unlink(new_table_path.buf);
		goto done;
	}

	err = commit_lock_file(&tables_list_lock);
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
	for (i = first; i <= last; i++) {
		struct lock_file *table_lock = &table_locks[i - first];
		char *table_path = get_locked_file_path(table_lock);
		unlink(table_path);
		free(table_path);
	}

done:
	rollback_lock_file(&tables_list_lock);
	for (i = first; table_locks && i <= last; i++)
		rollback_lock_file(&table_locks[i - first]);
	reftable_free(table_locks);

	delete_tempfile(&new_table);
	strbuf_release(&new_table_name);
	strbuf_release(&new_table_path);

	strbuf_release(&tables_list_buf);
	strbuf_release(&table_name);
	return err;
}

int reftable_stack_compact_all(struct reftable_stack *st,
			       struct reftable_log_expiry_config *config)
{
	return stack_compact_range(st, 0, st->merged->stack_len ?
			st->merged->stack_len - 1 : 0, config);
}

static int stack_compact_range_stats(struct reftable_stack *st,
				     size_t first, size_t last,
				     struct reftable_log_expiry_config *config)
{
	int err = stack_compact_range(st, first, last, config);
	if (err == REFTABLE_LOCK_ERROR)
		st->stats.failures++;
	return err;
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
	uint64_t *sizes =
		reftable_calloc(st->merged->stack_len, sizeof(*sizes));
	int version = (st->opts.hash_id == GIT_SHA1_FORMAT_ID) ? 1 : 2;
	int overhead = header_size(version) - 1;
	int i = 0;
	for (i = 0; i < st->merged->stack_len; i++) {
		sizes[i] = st->readers[i]->size - overhead;
	}
	return sizes;
}

int reftable_stack_auto_compact(struct reftable_stack *st)
{
	uint64_t *sizes = stack_table_sizes_for_compaction(st);
	struct segment seg =
		suggest_compaction_segment(sizes, st->merged->stack_len,
					   st->opts.auto_compaction_factor);
	reftable_free(sizes);
	if (segment_size(&seg) > 0)
		return stack_compact_range_stats(st, seg.start, seg.end - 1,
						 NULL);

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
	struct reftable_table tab = { NULL };
	reftable_table_from_merged_table(&tab, reftable_stack_merged_table(st));
	return reftable_table_read_ref(&tab, refname, ref);
}

int reftable_stack_read_log(struct reftable_stack *st, const char *refname,
			    struct reftable_log_record *log)
{
	struct reftable_iterator it = {0};
	int err;

	reftable_stack_init_log_iterator(st, &it);
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
	struct strbuf table_path = STRBUF_INIT;
	stack_filename(&table_path, st, name);

	err = reftable_block_source_from_file(&src, table_path.buf);
	if (err < 0)
		goto done;

	err = reftable_new_reader(&rd, &src, name);
	if (err < 0)
		goto done;

	update_idx = reftable_reader_max_update_index(rd);
	reftable_reader_free(rd);

	if (update_idx <= max) {
		unlink(table_path.buf);
	}
done:
	strbuf_release(&table_path);
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
	int err = reftable_stack_new_addition(&add, st);
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

int reftable_stack_print_directory(const char *stackdir, uint32_t hash_id)
{
	struct reftable_stack *stack = NULL;
	struct reftable_write_options opts = { .hash_id = hash_id };
	struct reftable_merged_table *merged = NULL;
	struct reftable_table table = { NULL };

	int err = reftable_new_stack(&stack, stackdir, &opts);
	if (err < 0)
		goto done;

	merged = reftable_stack_merged_table(stack);
	reftable_table_from_merged_table(&table, merged);
	err = reftable_table_print(&table);
done:
	if (stack)
		reftable_stack_destroy(stack);
	return err;
}
