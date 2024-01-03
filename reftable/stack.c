/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "stack.h"

#include "system.h"
#include "merged.h"
#include "reader.h"
#include "refname.h"
#include "reftable-error.h"
#include "reftable-record.h"
#include "reftable-merged.h"
#include "writer.h"

#include "tempfile.h"

static int stack_try_add(struct reftable_stack *st,
			 int (*write_table)(struct reftable_writer *wr,
					    void *arg),
			 void *arg);
static int stack_write_compact(struct reftable_stack *st,
			       struct reftable_writer *wr, int first, int last,
			       struct reftable_log_expiry_config *config);
static int stack_check_addition(struct reftable_stack *st,
				const char *new_tab_name);
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

int reftable_new_stack(struct reftable_stack **dest, const char *dir,
		       struct reftable_write_options config)
{
	struct reftable_stack *p =
		reftable_calloc(sizeof(struct reftable_stack));
	struct strbuf list_file_name = STRBUF_INIT;
	int err = 0;

	if (config.hash_id == 0) {
		config.hash_id = GIT_SHA1_FORMAT_ID;
	}

	*dest = NULL;

	strbuf_reset(&list_file_name);
	strbuf_addstr(&list_file_name, dir);
	strbuf_addstr(&list_file_name, "/tables.list");

	p->list_file = strbuf_detach(&list_file_name, NULL);
	p->reftable_dir = xstrdup(dir);
	p->config = config;

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

	buf = reftable_malloc(size + 1);
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
			*namesp = reftable_calloc(sizeof(char *));
			return 0;
		}

		return REFTABLE_IO_ERROR;
	}
	err = fd_read_lines(fd, namesp);
	close(fd);
	return err;
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
	FREE_AND_NULL(st->list_file);
	FREE_AND_NULL(st->reftable_dir);
	reftable_free(st);
	free_names(names);
}

static struct reftable_reader **stack_copy_readers(struct reftable_stack *st,
						   int cur_len)
{
	struct reftable_reader **cur =
		reftable_calloc(sizeof(struct reftable_reader *) * cur_len);
	int i = 0;
	for (i = 0; i < cur_len; i++) {
		cur[i] = st->readers[i];
	}
	return cur;
}

static int reftable_stack_reload_once(struct reftable_stack *st, char **names,
				      int reuse_open)
{
	int cur_len = !st->merged ? 0 : st->merged->stack_len;
	struct reftable_reader **cur = stack_copy_readers(st, cur_len);
	int err = 0;
	int names_len = names_length(names);
	struct reftable_reader **new_readers =
		reftable_calloc(sizeof(struct reftable_reader *) * names_len);
	struct reftable_table *new_tables =
		reftable_calloc(sizeof(struct reftable_table) * names_len);
	int new_readers_len = 0;
	struct reftable_merged_table *new_merged = NULL;
	struct strbuf table_path = STRBUF_INIT;
	int i;

	while (*names) {
		struct reftable_reader *rd = NULL;
		char *name = *names++;

		/* this is linear; we assume compaction keeps the number of
		   tables under control so this is not quadratic. */
		int j = 0;
		for (j = 0; reuse_open && j < cur_len; j++) {
			if (cur[j] && 0 == strcmp(cur[j]->name, name)) {
				rd = cur[j];
				cur[j] = NULL;
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
					new_readers_len, st->config.hash_id);
	if (err < 0)
		goto done;

	new_tables = NULL;
	st->readers_len = new_readers_len;
	if (st->merged) {
		merged_table_release(st->merged);
		reftable_merged_table_free(st->merged);
	}
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
	struct timeval deadline = { 0 };
	int err = gettimeofday(&deadline, NULL);
	int64_t delay = 0;
	int tries = 0;
	if (err < 0)
		return err;

	deadline.tv_sec += 3;
	while (1) {
		char **names = NULL;
		char **names_after = NULL;
		struct timeval now = { 0 };
		int err = gettimeofday(&now, NULL);
		int err2 = 0;
		if (err < 0) {
			return err;
		}

		/* Only look at deadlines after the first few times. This
		   simplifies debugging in GDB */
		tries++;
		if (tries > 3 && tv_cmp(&now, &deadline) >= 0) {
			break;
		}

		err = read_lines(st->list_file, &names);
		if (err < 0) {
			free_names(names);
			return err;
		}
		err = reftable_stack_reload_once(st, names, reuse_open);
		if (err == 0) {
			free_names(names);
			break;
		}
		if (err != REFTABLE_NOT_EXIST_ERROR) {
			free_names(names);
			return err;
		}

		/* err == REFTABLE_NOT_EXIST_ERROR can be caused by a concurrent
		   writer. Check if there was one by checking if the name list
		   changed.
		*/
		err2 = read_lines(st->list_file, &names_after);
		if (err2 < 0) {
			free_names(names);
			return err2;
		}

		if (names_equal(names_after, names)) {
			free_names(names);
			free_names(names_after);
			return err;
		}
		free_names(names);
		free_names(names_after);

		delay = delay + (delay * rand()) / RAND_MAX + 1;
		sleep_millisec(delay);
	}

	return 0;
}

/* -1 = error
 0 = up to date
 1 = changed. */
static int stack_uptodate(struct reftable_stack *st)
{
	char **names = NULL;
	int err = read_lines(st->list_file, &names);
	int i = 0;
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
		if (err == REFTABLE_LOCK_ERROR) {
			/* Ignore error return, we want to propagate
			   REFTABLE_LOCK_ERROR.
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
	int new_tables_len;
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
	if (st->config.default_permissions) {
		if (chmod(add->lock_file->filename.buf, st->config.default_permissions) < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	err = stack_uptodate(st);
	if (err < 0)
		goto done;

	if (err > 1) {
		err = REFTABLE_LOCK_ERROR;
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
	int i = 0;
	struct strbuf nm = STRBUF_INIT;
	for (i = 0; i < add->new_tables_len; i++) {
		stack_filename(&nm, add->stack, add->new_tables[i]);
		unlink(nm.buf);
		reftable_free(add->new_tables[i]);
		add->new_tables[i] = NULL;
	}
	reftable_free(add->new_tables);
	add->new_tables = NULL;
	add->new_tables_len = 0;

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
	int i = 0;
	int err = 0;

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

	err = rename_tempfile(&add->lock_file, add->stack->list_file);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	/* success, no more state to clean up. */
	for (i = 0; i < add->new_tables_len; i++) {
		reftable_free(add->new_tables[i]);
	}
	reftable_free(add->new_tables);
	add->new_tables = NULL;
	add->new_tables_len = 0;

	err = reftable_stack_reload(add->stack);
	if (err)
		goto done;

	if (!add->stack->disable_auto_compact)
		err = reftable_stack_auto_compact(add->stack);

done:
	reftable_addition_close(add);
	return err;
}

int reftable_stack_new_addition(struct reftable_addition **dest,
				struct reftable_stack *st)
{
	int err = 0;
	struct reftable_addition empty = REFTABLE_ADDITION_INIT;
	*dest = reftable_calloc(sizeof(**dest));
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
	if (err > 0) {
		err = REFTABLE_LOCK_ERROR;
		goto done;
	}

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
	int err = 0;
	int tab_fd = 0;

	strbuf_reset(&next_name);
	format_name(&next_name, add->next_update_index, add->next_update_index);

	stack_filename(&temp_tab_file_name, add->stack, next_name.buf);
	strbuf_addstr(&temp_tab_file_name, ".temp.XXXXXX");

	tab_fd = mkstemp(temp_tab_file_name.buf);
	if (tab_fd < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}
	if (add->stack->config.default_permissions) {
		if (chmod(temp_tab_file_name.buf, add->stack->config.default_permissions)) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}
	wr = reftable_new_writer(reftable_fd_write, &tab_fd,
				 &add->stack->config);
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

	err = close(tab_fd);
	tab_fd = 0;
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	err = stack_check_addition(add->stack, temp_tab_file_name.buf);
	if (err < 0)
		goto done;

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
	err = rename(temp_tab_file_name.buf, tab_file_name.buf);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	add->new_tables = reftable_realloc(add->new_tables,
					   sizeof(*add->new_tables) *
						   (add->new_tables_len + 1));
	add->new_tables[add->new_tables_len] = strbuf_detach(&next_name, NULL);
	add->new_tables_len++;
done:
	if (tab_fd > 0) {
		close(tab_fd);
		tab_fd = 0;
	}
	if (temp_tab_file_name.len > 0) {
		unlink(temp_tab_file_name.buf);
	}

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

static int stack_compact_locked(struct reftable_stack *st, int first, int last,
				struct strbuf *temp_tab,
				struct reftable_log_expiry_config *config)
{
	struct strbuf next_name = STRBUF_INIT;
	int tab_fd = -1;
	struct reftable_writer *wr = NULL;
	int err = 0;

	format_name(&next_name,
		    reftable_reader_min_update_index(st->readers[first]),
		    reftable_reader_max_update_index(st->readers[last]));

	stack_filename(temp_tab, st, next_name.buf);
	strbuf_addstr(temp_tab, ".temp.XXXXXX");

	tab_fd = mkstemp(temp_tab->buf);
	wr = reftable_new_writer(reftable_fd_write, &tab_fd, &st->config);

	err = stack_write_compact(st, wr, first, last, config);
	if (err < 0)
		goto done;
	err = reftable_writer_close(wr);
	if (err < 0)
		goto done;

	err = close(tab_fd);
	tab_fd = 0;

done:
	reftable_writer_free(wr);
	if (tab_fd > 0) {
		close(tab_fd);
		tab_fd = 0;
	}
	if (err != 0 && temp_tab->len > 0) {
		unlink(temp_tab->buf);
		strbuf_release(temp_tab);
	}
	strbuf_release(&next_name);
	return err;
}

static int stack_write_compact(struct reftable_stack *st,
			       struct reftable_writer *wr, int first, int last,
			       struct reftable_log_expiry_config *config)
{
	int subtabs_len = last - first + 1;
	struct reftable_table *subtabs = reftable_calloc(
		sizeof(struct reftable_table) * (last - first + 1));
	struct reftable_merged_table *mt = NULL;
	int err = 0;
	struct reftable_iterator it = { NULL };
	struct reftable_ref_record ref = { NULL };
	struct reftable_log_record log = { NULL };

	uint64_t entries = 0;

	int i = 0, j = 0;
	for (i = first, j = 0; i <= last; i++) {
		struct reftable_reader *t = st->readers[i];
		reftable_table_from_reader(&subtabs[j++], t);
		st->stats.bytes += t->size;
	}
	reftable_writer_set_limits(wr, st->readers[first]->min_update_index,
				   st->readers[last]->max_update_index);

	err = reftable_new_merged_table(&mt, subtabs, subtabs_len,
					st->config.hash_id);
	if (err < 0) {
		reftable_free(subtabs);
		goto done;
	}

	err = reftable_merged_table_seek_ref(mt, &it, "");
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

	err = reftable_merged_table_seek_log(mt, &it, "");
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
	if (mt) {
		merged_table_release(mt);
		reftable_merged_table_free(mt);
	}
	reftable_ref_record_release(&ref);
	reftable_log_record_release(&log);
	st->stats.entries_written += entries;
	return err;
}

/* <  0: error. 0 == OK, > 0 attempt failed; could retry. */
static int stack_compact_range(struct reftable_stack *st, int first, int last,
			       struct reftable_log_expiry_config *expiry)
{
	struct strbuf temp_tab_file_name = STRBUF_INIT;
	struct strbuf new_table_name = STRBUF_INIT;
	struct strbuf lock_file_name = STRBUF_INIT;
	struct strbuf ref_list_contents = STRBUF_INIT;
	struct strbuf new_table_path = STRBUF_INIT;
	int err = 0;
	int have_lock = 0;
	int lock_file_fd = -1;
	int compact_count = last - first + 1;
	char **listp = NULL;
	char **delete_on_success =
		reftable_calloc(sizeof(char *) * (compact_count + 1));
	char **subtable_locks =
		reftable_calloc(sizeof(char *) * (compact_count + 1));
	int i = 0;
	int j = 0;
	int is_empty_table = 0;

	if (first > last || (!expiry && first == last)) {
		err = 0;
		goto done;
	}

	st->stats.attempts++;

	strbuf_reset(&lock_file_name);
	strbuf_addstr(&lock_file_name, st->list_file);
	strbuf_addstr(&lock_file_name, ".lock");

	lock_file_fd =
		open(lock_file_name.buf, O_EXCL | O_CREAT | O_WRONLY, 0666);
	if (lock_file_fd < 0) {
		if (errno == EEXIST) {
			err = 1;
		} else {
			err = REFTABLE_IO_ERROR;
		}
		goto done;
	}
	/* Don't want to write to the lock for now.  */
	close(lock_file_fd);
	lock_file_fd = -1;

	have_lock = 1;
	err = stack_uptodate(st);
	if (err != 0)
		goto done;

	for (i = first, j = 0; i <= last; i++) {
		struct strbuf subtab_file_name = STRBUF_INIT;
		struct strbuf subtab_lock = STRBUF_INIT;
		int sublock_file_fd = -1;

		stack_filename(&subtab_file_name, st,
			       reader_name(st->readers[i]));

		strbuf_reset(&subtab_lock);
		strbuf_addbuf(&subtab_lock, &subtab_file_name);
		strbuf_addstr(&subtab_lock, ".lock");

		sublock_file_fd = open(subtab_lock.buf,
				       O_EXCL | O_CREAT | O_WRONLY, 0666);
		if (sublock_file_fd >= 0) {
			close(sublock_file_fd);
		} else if (sublock_file_fd < 0) {
			if (errno == EEXIST) {
				err = 1;
			} else {
				err = REFTABLE_IO_ERROR;
			}
		}

		subtable_locks[j] = subtab_lock.buf;
		delete_on_success[j] = subtab_file_name.buf;
		j++;

		if (err != 0)
			goto done;
	}

	err = unlink(lock_file_name.buf);
	if (err < 0)
		goto done;
	have_lock = 0;

	err = stack_compact_locked(st, first, last, &temp_tab_file_name,
				   expiry);
	/* Compaction + tombstones can create an empty table out of non-empty
	 * tables. */
	is_empty_table = (err == REFTABLE_EMPTY_TABLE_ERROR);
	if (is_empty_table) {
		err = 0;
	}
	if (err < 0)
		goto done;

	lock_file_fd =
		open(lock_file_name.buf, O_EXCL | O_CREAT | O_WRONLY, 0666);
	if (lock_file_fd < 0) {
		if (errno == EEXIST) {
			err = 1;
		} else {
			err = REFTABLE_IO_ERROR;
		}
		goto done;
	}
	have_lock = 1;
	if (st->config.default_permissions) {
		if (chmod(lock_file_name.buf, st->config.default_permissions) < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	format_name(&new_table_name, st->readers[first]->min_update_index,
		    st->readers[last]->max_update_index);
	strbuf_addstr(&new_table_name, ".ref");

	stack_filename(&new_table_path, st, new_table_name.buf);

	if (!is_empty_table) {
		/* retry? */
		err = rename(temp_tab_file_name.buf, new_table_path.buf);
		if (err < 0) {
			err = REFTABLE_IO_ERROR;
			goto done;
		}
	}

	for (i = 0; i < first; i++) {
		strbuf_addstr(&ref_list_contents, st->readers[i]->name);
		strbuf_addstr(&ref_list_contents, "\n");
	}
	if (!is_empty_table) {
		strbuf_addbuf(&ref_list_contents, &new_table_name);
		strbuf_addstr(&ref_list_contents, "\n");
	}
	for (i = last + 1; i < st->merged->stack_len; i++) {
		strbuf_addstr(&ref_list_contents, st->readers[i]->name);
		strbuf_addstr(&ref_list_contents, "\n");
	}

	err = write_in_full(lock_file_fd, ref_list_contents.buf, ref_list_contents.len);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		unlink(new_table_path.buf);
		goto done;
	}
	err = close(lock_file_fd);
	lock_file_fd = -1;
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		unlink(new_table_path.buf);
		goto done;
	}

	err = rename(lock_file_name.buf, st->list_file);
	if (err < 0) {
		err = REFTABLE_IO_ERROR;
		unlink(new_table_path.buf);
		goto done;
	}
	have_lock = 0;

	/* Reload the stack before deleting. On windows, we can only delete the
	   files after we closed them.
	*/
	err = reftable_stack_reload_maybe_reuse(st, first < last);

	listp = delete_on_success;
	while (*listp) {
		if (strcmp(*listp, new_table_path.buf)) {
			unlink(*listp);
		}
		listp++;
	}

done:
	free_names(delete_on_success);

	listp = subtable_locks;
	while (*listp) {
		unlink(*listp);
		listp++;
	}
	free_names(subtable_locks);
	if (lock_file_fd >= 0) {
		close(lock_file_fd);
		lock_file_fd = -1;
	}
	if (have_lock) {
		unlink(lock_file_name.buf);
	}
	strbuf_release(&new_table_name);
	strbuf_release(&new_table_path);
	strbuf_release(&ref_list_contents);
	strbuf_release(&temp_tab_file_name);
	strbuf_release(&lock_file_name);
	return err;
}

int reftable_stack_compact_all(struct reftable_stack *st,
			       struct reftable_log_expiry_config *config)
{
	return stack_compact_range(st, 0, st->merged->stack_len - 1, config);
}

static int stack_compact_range_stats(struct reftable_stack *st, int first,
				     int last,
				     struct reftable_log_expiry_config *config)
{
	int err = stack_compact_range(st, first, last, config);
	if (err > 0) {
		st->stats.failures++;
	}
	return err;
}

static int segment_size(struct segment *s)
{
	return s->end - s->start;
}

int fastlog2(uint64_t sz)
{
	int l = 0;
	if (sz == 0)
		return 0;
	for (; sz; sz /= 2) {
		l++;
	}
	return l - 1;
}

struct segment *sizes_to_segments(int *seglen, uint64_t *sizes, int n)
{
	struct segment *segs = reftable_calloc(sizeof(struct segment) * n);
	int next = 0;
	struct segment cur = { 0 };
	int i = 0;

	if (n == 0) {
		*seglen = 0;
		return segs;
	}
	for (i = 0; i < n; i++) {
		int log = fastlog2(sizes[i]);
		if (cur.log != log && cur.bytes > 0) {
			struct segment fresh = {
				.start = i,
			};

			segs[next++] = cur;
			cur = fresh;
		}

		cur.log = log;
		cur.end = i + 1;
		cur.bytes += sizes[i];
	}
	segs[next++] = cur;
	*seglen = next;
	return segs;
}

struct segment suggest_compaction_segment(uint64_t *sizes, int n)
{
	int seglen = 0;
	struct segment *segs = sizes_to_segments(&seglen, sizes, n);
	struct segment min_seg = {
		.log = 64,
	};
	int i = 0;
	for (i = 0; i < seglen; i++) {
		if (segment_size(&segs[i]) == 1) {
			continue;
		}

		if (segs[i].log < min_seg.log) {
			min_seg = segs[i];
		}
	}

	while (min_seg.start > 0) {
		int prev = min_seg.start - 1;
		if (fastlog2(min_seg.bytes) < fastlog2(sizes[prev])) {
			break;
		}

		min_seg.start = prev;
		min_seg.bytes += sizes[prev];
	}

	reftable_free(segs);
	return min_seg;
}

static uint64_t *stack_table_sizes_for_compaction(struct reftable_stack *st)
{
	uint64_t *sizes =
		reftable_calloc(sizeof(uint64_t) * st->merged->stack_len);
	int version = (st->config.hash_id == GIT_SHA1_FORMAT_ID) ? 1 : 2;
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
		suggest_compaction_segment(sizes, st->merged->stack_len);
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
	struct reftable_iterator it = { NULL };
	struct reftable_merged_table *mt = reftable_stack_merged_table(st);
	int err = reftable_merged_table_seek_log(mt, &it, refname);
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

static int stack_check_addition(struct reftable_stack *st,
				const char *new_tab_name)
{
	int err = 0;
	struct reftable_block_source src = { NULL };
	struct reftable_reader *rd = NULL;
	struct reftable_table tab = { NULL };
	struct reftable_ref_record *refs = NULL;
	struct reftable_iterator it = { NULL };
	int cap = 0;
	int len = 0;
	int i = 0;

	if (st->config.skip_name_check)
		return 0;

	err = reftable_block_source_from_file(&src, new_tab_name);
	if (err < 0)
		goto done;

	err = reftable_new_reader(&rd, &src, new_tab_name);
	if (err < 0)
		goto done;

	err = reftable_reader_seek_ref(rd, &it, "");
	if (err > 0) {
		err = 0;
		goto done;
	}
	if (err < 0)
		goto done;

	while (1) {
		struct reftable_ref_record ref = { NULL };
		err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0) {
			break;
		}
		if (err < 0)
			goto done;

		if (len >= cap) {
			cap = 2 * cap + 1;
			refs = reftable_realloc(refs, cap * sizeof(refs[0]));
		}

		refs[len++] = ref;
	}

	reftable_table_from_merged_table(&tab, reftable_stack_merged_table(st));

	err = validate_ref_record_addition(tab, refs, len);

done:
	for (i = 0; i < len; i++) {
		reftable_ref_record_release(&refs[i]);
	}

	free(refs);
	reftable_iterator_destroy(&it);
	reftable_reader_free(rd);
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
	struct reftable_write_options cfg = { .hash_id = hash_id };
	struct reftable_merged_table *merged = NULL;
	struct reftable_table table = { NULL };

	int err = reftable_new_stack(&stack, stackdir, cfg);
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
