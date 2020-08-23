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
#include "reftable.h"
#include "writer.h"

int new_stack(struct stack **dest, const char *dir, const char *list_file,
	      struct write_options config)
{
	struct stack *p = calloc(sizeof(struct stack), 1);
	int err = 0;
	*dest = NULL;
	p->list_file = strdup(list_file);
	p->reftable_dir = strdup(dir);
	p->config = config;

	err = stack_reload(p);
	if (err < 0) {
		stack_destroy(p);
	} else {
		*dest = p;
	}
	return err;
}

static int fread_lines(FILE *f, char ***namesp)
{
	long size = 0;
	int err = fseek(f, 0, SEEK_END);
	char *buf = NULL;
	if (err < 0) {
		err = IO_ERROR;
		goto exit;
	}
	size = ftell(f);
	if (size < 0) {
		err = IO_ERROR;
		goto exit;
	}
	err = fseek(f, 0, SEEK_SET);
	if (err < 0) {
		err = IO_ERROR;
		goto exit;
	}

	buf = malloc(size + 1);
	if (fread(buf, 1, size, f) != size) {
		err = IO_ERROR;
		goto exit;
	}
	buf[size] = 0;

	parse_names(buf, size, namesp);
exit:
	free(buf);
	return err;
}

int read_lines(const char *filename, char ***namesp)
{
	FILE *f = fopen(filename, "r");
	int err = 0;
	if (f == NULL) {
		if (errno == ENOENT) {
			*namesp = calloc(sizeof(char *), 1);
			return 0;
		}

		return IO_ERROR;
	}
	err = fread_lines(f, namesp);
	fclose(f);
	return err;
}

struct merged_table *stack_merged_table(struct stack *st)
{
	return st->merged;
}

/* Close and free the stack */
void stack_destroy(struct stack *st)
{
	if (st->merged == NULL) {
		return;
	}

	merged_table_close(st->merged);
	merged_table_free(st->merged);
	st->merged = NULL;

	FREE_AND_NULL(st->list_file);
	FREE_AND_NULL(st->reftable_dir);
	free(st);
}

static struct reader **stack_copy_readers(struct stack *st, int cur_len)
{
	struct reader **cur = calloc(sizeof(struct reader *), cur_len);
	int i = 0;
	for (i = 0; i < cur_len; i++) {
		cur[i] = st->merged->stack[i];
	}
	return cur;
}

static int stack_reload_once(struct stack *st, char **names, bool reuse_open)
{
	int cur_len = st->merged == NULL ? 0 : st->merged->stack_len;
	struct reader **cur = stack_copy_readers(st, cur_len);
	int err = 0;
	int names_len = names_length(names);
	struct reader **new_tables =
		malloc(sizeof(struct reader *) * names_len);
	int new_tables_len = 0;
	struct merged_table *new_merged = NULL;

	struct slice table_path = {};

	while (*names) {
		struct reader *rd = NULL;
		char *name = *names++;

		/* this is linear; we assume compaction keeps the number of
		   tables under control so this is not quadratic. */
		int j = 0;
		for (j = 0; reuse_open && j < cur_len; j++) {
			if (cur[j] != NULL && 0 == strcmp(cur[j]->name, name)) {
				rd = cur[j];
				cur[j] = NULL;
				break;
			}
		}

		if (rd == NULL) {
			struct block_source src = {};
			slice_set_string(&table_path, st->reftable_dir);
			slice_append_string(&table_path, "/");
			slice_append_string(&table_path, name);

			err = block_source_from_file(
				&src, slice_as_string(&table_path));
			if (err < 0) {
				goto exit;
			}

			err = new_reader(&rd, src, name);
			if (err < 0) {
				goto exit;
			}
		}

		new_tables[new_tables_len++] = rd;
	}

	/* success! */
	err = new_merged_table(&new_merged, new_tables, new_tables_len);
	if (err < 0) {
		goto exit;
	}

	new_tables = NULL;
	new_tables_len = 0;
	if (st->merged != NULL) {
		merged_table_clear(st->merged);
		merged_table_free(st->merged);
	}
	st->merged = new_merged;

	{
		int i = 0;
		for (i = 0; i < cur_len; i++) {
			if (cur[i] != NULL) {
				reader_close(cur[i]);
				reader_free(cur[i]);
			}
		}
	}
exit:
	free(slice_yield(&table_path));
	{
		int i = 0;
		for (i = 0; i < new_tables_len; i++) {
			reader_close(new_tables[i]);
		}
	}
	free(new_tables);
	free(cur);
	return err;
}

/* return negative if a before b. */
static int tv_cmp(struct timeval *a, struct timeval *b)
{
	time_t diff = a->tv_sec - b->tv_sec;
	int udiff = a->tv_usec - b->tv_usec;

	if (diff != 0) {
		return diff;
	}

	return udiff;
}

static int stack_reload_maybe_reuse(struct stack *st, bool reuse_open)
{
	struct timeval deadline = {};
	int err = gettimeofday(&deadline, NULL);
	int64_t delay = 0;
	int tries = 0;
	if (err < 0) {
		return err;
	}

	deadline.tv_sec += 3;
	while (true) {
		char **names = NULL;
		char **names_after = NULL;
		struct timeval now = {};
		int err = gettimeofday(&now, NULL);
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
		err = stack_reload_once(st, names, reuse_open);
		if (err == 0) {
			free_names(names);
			break;
		}
		if (err != NOT_EXIST_ERROR) {
			free_names(names);
			return err;
		}

		err = read_lines(st->list_file, &names_after);
		if (err < 0) {
			free_names(names);
			return err;
		}

		if (names_equal(names_after, names)) {
			free_names(names);
			free_names(names_after);
			return -1;
		}
		free_names(names);
		free_names(names_after);

		delay = delay + (delay * rand()) / RAND_MAX + 100;
		usleep(delay);
	}

	return 0;
}

int stack_reload(struct stack *st)
{
	return stack_reload_maybe_reuse(st, true);
}

/* -1 = error
 0 = up to date
 1 = changed. */
static int stack_uptodate(struct stack *st)
{
	char **names = NULL;
	int err = read_lines(st->list_file, &names);
	int i = 0;
	if (err < 0) {
		return err;
	}

	for (i = 0; i < st->merged->stack_len; i++) {
		if (names[i] == NULL) {
			err = 1;
			goto exit;
		}

		if (strcmp(st->merged->stack[i]->name, names[i])) {
			err = 1;
			goto exit;
		}
	}

	if (names[st->merged->stack_len] != NULL) {
		err = 1;
		goto exit;
	}

exit:
	free_names(names);
	return err;
}

int stack_add(struct stack *st, int (*write)(struct writer *wr, void *arg),
	      void *arg)
{
	int err = stack_try_add(st, write, arg);
	if (err < 0) {
		if (err == LOCK_ERROR) {
			err = stack_reload(st);
		}
		return err;
	}

	return stack_auto_compact(st);
}

static void format_name(struct slice *dest, uint64_t min, uint64_t max)
{
	char buf[100];
	snprintf(buf, sizeof(buf), "%012" PRIx64 "-%012" PRIx64, min, max);
	slice_set_string(dest, buf);
}

int stack_try_add(struct stack *st,
		  int (*write_table)(struct writer *wr, void *arg), void *arg)
{
	struct slice lock_name = {};
	struct slice temp_tab_name = {};
	struct slice tab_name = {};
	struct slice next_name = {};
	struct slice table_list = {};
	struct writer *wr = NULL;
	int err = 0;
	int tab_fd = 0;
	int lock_fd = 0;
	uint64_t next_update_index = 0;

	slice_set_string(&lock_name, st->list_file);
	slice_append_string(&lock_name, ".lock");

	lock_fd = open(slice_as_string(&lock_name), O_EXCL | O_CREAT | O_WRONLY,
		       0644);
	if (lock_fd < 0) {
		if (errno == EEXIST) {
			err = LOCK_ERROR;
			goto exit;
		}
		err = IO_ERROR;
		goto exit;
	}

	err = stack_uptodate(st);
	if (err < 0) {
		goto exit;
	}

	if (err > 1) {
		err = LOCK_ERROR;
		goto exit;
	}

	next_update_index = stack_next_update_index(st);

	slice_resize(&next_name, 0);
	format_name(&next_name, next_update_index, next_update_index);

	slice_set_string(&temp_tab_name, st->reftable_dir);
	slice_append_string(&temp_tab_name, "/");
	slice_append(&temp_tab_name, next_name);
	slice_append_string(&temp_tab_name, ".temp.XXXXXX");

	tab_fd = mkstemp((char *)slice_as_string(&temp_tab_name));
	if (tab_fd < 0) {
		err = IO_ERROR;
		goto exit;
	}

	wr = new_writer(fd_writer, &tab_fd, &st->config);
	err = write_table(wr, arg);
	if (err < 0) {
		goto exit;
	}

	err = writer_close(wr);
	if (err < 0) {
		goto exit;
	}

	err = close(tab_fd);
	tab_fd = 0;
	if (err < 0) {
		err = IO_ERROR;
		goto exit;
	}

	if (wr->min_update_index < next_update_index) {
		err = API_ERROR;
		goto exit;
	}

	{
		int i = 0;
		for (i = 0; i < st->merged->stack_len; i++) {
			slice_append_string(&table_list,
					    st->merged->stack[i]->name);
			slice_append_string(&table_list, "\n");
		}
	}

	format_name(&next_name, wr->min_update_index, wr->max_update_index);
	slice_append_string(&next_name, ".ref");
	slice_append(&table_list, next_name);
	slice_append_string(&table_list, "\n");

	slice_set_string(&tab_name, st->reftable_dir);
	slice_append_string(&tab_name, "/");
	slice_append(&tab_name, next_name);

	err = rename(slice_as_string(&temp_tab_name),
		     slice_as_string(&tab_name));
	if (err < 0) {
		err = IO_ERROR;
		goto exit;
	}
	free(slice_yield(&temp_tab_name));

	err = write(lock_fd, table_list.buf, table_list.len);
	if (err < 0) {
		err = IO_ERROR;
		goto exit;
	}
	err = close(lock_fd);
	lock_fd = 0;
	if (err < 0) {
		unlink(slice_as_string(&tab_name));
		err = IO_ERROR;
		goto exit;
	}

	err = rename(slice_as_string(&lock_name), st->list_file);
	if (err < 0) {
		unlink(slice_as_string(&tab_name));
		err = IO_ERROR;
		goto exit;
	}

	err = stack_reload(st);
exit:
	if (tab_fd > 0) {
		close(tab_fd);
		tab_fd = 0;
	}
	if (temp_tab_name.len > 0) {
		unlink(slice_as_string(&temp_tab_name));
	}
	unlink(slice_as_string(&lock_name));

	if (lock_fd > 0) {
		close(lock_fd);
		lock_fd = 0;
	}

	free(slice_yield(&lock_name));
	free(slice_yield(&temp_tab_name));
	free(slice_yield(&tab_name));
	free(slice_yield(&next_name));
	free(slice_yield(&table_list));
	writer_free(wr);
	return err;
}

uint64_t stack_next_update_index(struct stack *st)
{
	int sz = st->merged->stack_len;
	if (sz > 0) {
		return reader_max_update_index(st->merged->stack[sz - 1]) + 1;
	}
	return 1;
}

static int stack_compact_locked(struct stack *st, int first, int last,
				struct slice *temp_tab,
				struct log_expiry_config *config)
{
	struct slice next_name = {};
	int tab_fd = -1;
	struct writer *wr = NULL;
	int err = 0;

	format_name(&next_name,
		    reader_min_update_index(st->merged->stack[first]),
		    reader_max_update_index(st->merged->stack[first]));

	slice_set_string(temp_tab, st->reftable_dir);
	slice_append_string(temp_tab, "/");
	slice_append(temp_tab, next_name);
	slice_append_string(temp_tab, ".temp.XXXXXX");

	tab_fd = mkstemp((char *)slice_as_string(temp_tab));
	wr = new_writer(fd_writer, &tab_fd, &st->config);

	err = stack_write_compact(st, wr, first, last, config);
	if (err < 0) {
		goto exit;
	}
	err = writer_close(wr);
	if (err < 0) {
		goto exit;
	}
	writer_free(wr);

	err = close(tab_fd);
	tab_fd = 0;

exit:
	if (tab_fd > 0) {
		close(tab_fd);
		tab_fd = 0;
	}
	if (err != 0 && temp_tab->len > 0) {
		unlink(slice_as_string(temp_tab));
		free(slice_yield(temp_tab));
	}
	free(slice_yield(&next_name));
	return err;
}

int stack_write_compact(struct stack *st, struct writer *wr, int first,
			int last, struct log_expiry_config *config)
{
	int subtabs_len = last - first + 1;
	struct reader **subtabs =
		calloc(sizeof(struct reader *), last - first + 1);
	struct merged_table *mt = NULL;
	int err = 0;
	struct iterator it = {};
	struct ref_record ref = {};
	struct log_record log = {};

	int i = 0, j = 0;
	for (i = first, j = 0; i <= last; i++) {
		struct reader *t = st->merged->stack[i];
		subtabs[j++] = t;
		st->stats.bytes += t->size;
	}
	writer_set_limits(wr, st->merged->stack[first]->min_update_index,
			  st->merged->stack[last]->max_update_index);

	err = new_merged_table(&mt, subtabs, subtabs_len);
	if (err < 0) {
		free(subtabs);
		goto exit;
	}

	err = merged_table_seek_ref(mt, &it, "");
	if (err < 0) {
		goto exit;
	}

	while (true) {
		err = iterator_next_ref(it, &ref);
		if (err > 0) {
			err = 0;
			break;
		}
		if (err < 0) {
			break;
		}
		if (first == 0 && ref_record_is_deletion(&ref)) {
			continue;
		}

		err = writer_add_ref(wr, &ref);
		if (err < 0) {
			break;
		}
	}

	err = merged_table_seek_log(mt, &it, "");
	if (err < 0) {
		goto exit;
	}

	while (true) {
		err = iterator_next_log(it, &log);
		if (err > 0) {
			err = 0;
			break;
		}
		if (err < 0) {
			break;
		}
		if (first == 0 && log_record_is_deletion(&log)) {
			continue;
		}

		/* XXX collect stats? */

		if (config != NULL && config->time > 0 &&
		    log.time < config->time) {
			continue;
		}

		if (config != NULL && config->min_update_index > 0 &&
		    log.update_index < config->min_update_index) {
			continue;
		}

		err = writer_add_log(wr, &log);
		if (err < 0) {
			break;
		}
	}

exit:
	iterator_destroy(&it);
	if (mt != NULL) {
		merged_table_clear(mt);
		merged_table_free(mt);
	}
	ref_record_clear(&ref);

	return err;
}

/* <  0: error. 0 == OK, > 0 attempt failed; could retry. */
static int stack_compact_range(struct stack *st, int first, int last,
			       struct log_expiry_config *expiry)
{
	struct slice temp_tab_name = {};
	struct slice new_table_name = {};
	struct slice lock_file_name = {};
	struct slice ref_list_contents = {};
	struct slice new_table_path = {};
	int err = 0;
	bool have_lock = false;
	int lock_file_fd = 0;
	int compact_count = last - first + 1;
	char **delete_on_success = calloc(sizeof(char *), compact_count + 1);
	char **subtable_locks = calloc(sizeof(char *), compact_count + 1);
	int i = 0;
	int j = 0;

	if (first > last || (expiry == NULL && first == last)) {
		err = 0;
		goto exit;
	}

	st->stats.attempts++;

	slice_set_string(&lock_file_name, st->list_file);
	slice_append_string(&lock_file_name, ".lock");

	lock_file_fd = open(slice_as_string(&lock_file_name),
			    O_EXCL | O_CREAT | O_WRONLY, 0644);
	if (lock_file_fd < 0) {
		if (errno == EEXIST) {
			err = 1;
		} else {
			err = IO_ERROR;
		}
		goto exit;
	}
	have_lock = true;
	err = stack_uptodate(st);
	if (err != 0) {
		goto exit;
	}

	for (i = first, j = 0; i <= last; i++) {
		struct slice subtab_name = {};
		struct slice subtab_lock = {};
		slice_set_string(&subtab_name, st->reftable_dir);
		slice_append_string(&subtab_name, "/");
		slice_append_string(&subtab_name,
				    reader_name(st->merged->stack[i]));

		slice_copy(&subtab_lock, subtab_name);
		slice_append_string(&subtab_lock, ".lock");

		{
			int sublock_file_fd =
				open(slice_as_string(&subtab_lock),
				     O_EXCL | O_CREAT | O_WRONLY, 0644);
			if (sublock_file_fd > 0) {
				close(sublock_file_fd);
			} else if (sublock_file_fd < 0) {
				if (errno == EEXIST) {
					err = 1;
				}
				err = IO_ERROR;
			}
		}

		subtable_locks[j] = (char *)slice_as_string(&subtab_lock);
		delete_on_success[j] = (char *)slice_as_string(&subtab_name);
		j++;

		if (err != 0) {
			goto exit;
		}
	}

	err = unlink(slice_as_string(&lock_file_name));
	if (err < 0) {
		goto exit;
	}
	have_lock = false;

	err = stack_compact_locked(st, first, last, &temp_tab_name, expiry);
	if (err < 0) {
		goto exit;
	}

	lock_file_fd = open(slice_as_string(&lock_file_name),
			    O_EXCL | O_CREAT | O_WRONLY, 0644);
	if (lock_file_fd < 0) {
		if (errno == EEXIST) {
			err = 1;
		} else {
			err = IO_ERROR;
		}
		goto exit;
	}
	have_lock = true;

	format_name(&new_table_name, st->merged->stack[first]->min_update_index,
		    st->merged->stack[last]->max_update_index);
	slice_append_string(&new_table_name, ".ref");

	slice_set_string(&new_table_path, st->reftable_dir);
	slice_append_string(&new_table_path, "/");

	slice_append(&new_table_path, new_table_name);

	err = rename(slice_as_string(&temp_tab_name),
		     slice_as_string(&new_table_path));
	if (err < 0) {
		goto exit;
	}

	for (i = 0; i < first; i++) {
		slice_append_string(&ref_list_contents,
				    st->merged->stack[i]->name);
		slice_append_string(&ref_list_contents, "\n");
	}
	slice_append(&ref_list_contents, new_table_name);
	slice_append_string(&ref_list_contents, "\n");
	for (i = last + 1; i < st->merged->stack_len; i++) {
		slice_append_string(&ref_list_contents,
				    st->merged->stack[i]->name);
		slice_append_string(&ref_list_contents, "\n");
	}

	err = write(lock_file_fd, ref_list_contents.buf, ref_list_contents.len);
	if (err < 0) {
		unlink(slice_as_string(&new_table_path));
		goto exit;
	}
	err = close(lock_file_fd);
	lock_file_fd = 0;
	if (err < 0) {
		unlink(slice_as_string(&new_table_path));
		goto exit;
	}

	err = rename(slice_as_string(&lock_file_name), st->list_file);
	if (err < 0) {
		unlink(slice_as_string(&new_table_path));
		goto exit;
	}
	have_lock = false;

	for (char **p = delete_on_success; *p; p++) {
		if (strcmp(*p, slice_as_string(&new_table_path))) {
			unlink(*p);
		}
	}

	err = stack_reload_maybe_reuse(st, first < last);
exit:
	for (char **p = subtable_locks; *p; p++) {
		unlink(*p);
	}
	free_names(delete_on_success);
	free_names(subtable_locks);
	if (lock_file_fd > 0) {
		close(lock_file_fd);
		lock_file_fd = 0;
	}
	if (have_lock) {
		unlink(slice_as_string(&lock_file_name));
	}
	free(slice_yield(&new_table_name));
	free(slice_yield(&new_table_path));
	free(slice_yield(&ref_list_contents));
	free(slice_yield(&temp_tab_name));
	free(slice_yield(&lock_file_name));
	return err;
}

int stack_compact_all(struct stack *st, struct log_expiry_config *config)
{
	return stack_compact_range(st, 0, st->merged->stack_len - 1, config);
}

static int stack_compact_range_stats(struct stack *st, int first, int last,
				     struct log_expiry_config *config)
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
	assert(sz > 0);
	for (; sz; sz /= 2) {
		l++;
	}
	return l - 1;
}

struct segment *sizes_to_segments(int *seglen, uint64_t *sizes, int n)
{
	struct segment *segs = calloc(sizeof(struct segment), n);
	int next = 0;
	struct segment cur = {};
	int i = 0;
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

	free(segs);
	return min_seg;
}

static uint64_t *stack_table_sizes_for_compaction(struct stack *st)
{
	uint64_t *sizes = calloc(sizeof(uint64_t), st->merged->stack_len);
	int i = 0;
	for (i = 0; i < st->merged->stack_len; i++) {
		/* overhead is 24 + 68 = 92. */
		sizes[i] = st->merged->stack[i]->size - 91;
	}
	return sizes;
}

int stack_auto_compact(struct stack *st)
{
	uint64_t *sizes = stack_table_sizes_for_compaction(st);
	struct segment seg =
		suggest_compaction_segment(sizes, st->merged->stack_len);
	free(sizes);
	if (segment_size(&seg) > 0) {
		return stack_compact_range_stats(st, seg.start, seg.end - 1,
						 NULL);
	}

	return 0;
}

struct compaction_stats *stack_compaction_stats(struct stack *st)
{
	return &st->stats;
}

int stack_read_ref(struct stack *st, const char *refname,
		   struct ref_record *ref)
{
	struct iterator it = {};
	struct merged_table *mt = stack_merged_table(st);
	int err = merged_table_seek_ref(mt, &it, refname);
	if (err) {
		goto exit;
	}

	err = iterator_next_ref(it, ref);
	if (err) {
		goto exit;
	}

	if (strcmp(ref->ref_name, refname) || ref_record_is_deletion(ref)) {
		err = 1;
		goto exit;
	}

exit:
	iterator_destroy(&it);
	return err;
}

int stack_read_log(struct stack *st, const char *refname,
		   struct log_record *log)
{
	struct iterator it = {};
	struct merged_table *mt = stack_merged_table(st);
	int err = merged_table_seek_log(mt, &it, refname);
	if (err) {
		goto exit;
	}

	err = iterator_next_log(it, log);
	if (err) {
		goto exit;
	}

	if (strcmp(log->ref_name, refname) || log_record_is_deletion(log)) {
		err = 1;
		goto exit;
	}

exit:
	iterator_destroy(&it);
	return err;
}
