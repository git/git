/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "unit-test.h"
#include "dir.h"
#include "lib-reftable.h"
#include "reftable/merged.h"
#include "reftable/reftable-error.h"
#include "reftable/stack.h"
#include "reftable/table.h"
#include "strbuf.h"
#include "tempfile.h"
#include <dirent.h>

static void clear_dir(const char *dirname)
{
	struct strbuf path = REFTABLE_BUF_INIT;
	strbuf_addstr(&path, dirname);
	remove_dir_recursively(&path, 0);
	strbuf_release(&path);
}

static int count_dir_entries(const char *dirname)
{
	DIR *dir = opendir(dirname);
	int len = 0;
	struct dirent *d;
	if (!dir)
		return 0;

	while ((d = readdir(dir))) {
		/*
		 * Besides skipping over "." and "..", we also need to
		 * skip over other files that have a leading ".". This
		 * is due to behaviour of NFS, which will rename files
		 * to ".nfs*" to emulate delete-on-last-close.
		 *
		 * In any case this should be fine as the reftable
		 * library will never write files with leading dots
		 * anyway.
		 */
		if (starts_with(d->d_name, "."))
			continue;
		len++;
	}
	closedir(dir);
	return len;
}

/*
 * Work linenumber into the tempdir, so we can see which tests forget to
 * cleanup.
 */
static char *get_tmp_template(int linenumber)
{
	const char *tmp = getenv("TMPDIR");
	static char template[1024];
	snprintf(template, sizeof(template) - 1, "%s/stack_test-%d.XXXXXX",
		 tmp ? tmp : "/tmp", linenumber);
	return template;
}

static char *get_tmp_dir(int linenumber)
{
	char *dir = get_tmp_template(linenumber);
	cl_assert(mkdtemp(dir) != NULL);
	return dir;
}

void test_reftable_stack__read_file(void)
{
	char *fn = get_tmp_template(__LINE__);
	struct tempfile *tmp = mks_tempfile(fn);
	int fd = get_tempfile_fd(tmp);
	char out[1024] = "line1\n\nline2\nline3";
	int n, err;
	char **names = NULL;
	const char *want[] = { "line1", "line2", "line3" };

	cl_assert(fd > 0);
	n = write_in_full(fd, out, strlen(out));
	cl_assert_equal_i(n, strlen(out));
	err = close(fd);
	cl_assert(err >= 0);

	err = read_lines(fn, &names);
	cl_assert(!err);

	for (size_t i = 0; names[i]; i++)
		cl_assert_equal_s(want[i], names[i]);
	free_names(names);
	(void) remove(fn);
	delete_tempfile(&tmp);
}

static int write_test_ref(struct reftable_writer *wr, void *arg)
{
	struct reftable_ref_record *ref = arg;
	cl_assert_equal_i(reftable_writer_set_limits(wr,
						     ref->update_index, ref->update_index), 0);
	return reftable_writer_add_ref(wr, ref);
}

static void write_n_ref_tables(struct reftable_stack *st,
			       size_t n)
{
	int disable_auto_compact;

	disable_auto_compact = st->opts.disable_auto_compact;
	st->opts.disable_auto_compact = 1;

	for (size_t i = 0; i < n; i++) {
		struct reftable_ref_record ref = {
			.update_index = reftable_stack_next_update_index(st),
			.value_type = REFTABLE_REF_VAL1,
		};
		char buf[128];

		snprintf(buf, sizeof(buf), "refs/heads/branch-%04"PRIuMAX, (uintmax_t)i);
		ref.refname = buf;
		cl_reftable_set_hash(ref.value.val1, i, REFTABLE_HASH_SHA1);

		cl_assert_equal_i(reftable_stack_add(st,
						     &write_test_ref, &ref), 0);
	}

	st->opts.disable_auto_compact = disable_auto_compact;
}

struct write_log_arg {
	struct reftable_log_record *log;
	uint64_t update_index;
};

static int write_test_log(struct reftable_writer *wr, void *arg)
{
	struct write_log_arg *wla = arg;

	cl_assert_equal_i(reftable_writer_set_limits(wr,
						     wla->update_index,
						     wla->update_index),  0);
	return reftable_writer_add_log(wr, wla->log);
}

void test_reftable_stack__add_one(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_buf scratch = REFTABLE_BUF_INIT;
	int mask = umask(002);
	struct reftable_write_options opts = {
		.default_permissions = 0660,
	};
	struct reftable_stack *st = NULL;
	struct reftable_ref_record ref = {
		.refname = (char *) "HEAD",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *) "master",
	};
	struct reftable_ref_record dest = { 0 };
	struct stat stat_result = { 0 };
	int err;

	err = reftable_new_stack(&st, dir, &opts);
	cl_assert(!err);

	err = reftable_stack_add(st, write_test_ref, &ref);
	cl_assert(!err);

	err = reftable_stack_read_ref(st, ref.refname, &dest);
	cl_assert(!err);
	cl_assert(reftable_ref_record_equal(&ref, &dest,
					    REFTABLE_HASH_SIZE_SHA1));
	cl_assert(st->tables_len > 0);

#ifndef GIT_WINDOWS_NATIVE
	cl_assert_equal_i(reftable_buf_addstr(&scratch, dir), 0);
	cl_assert_equal_i(reftable_buf_addstr(&scratch,
					      "/tables.list"), 0);
	cl_assert_equal_i(stat(scratch.buf, &stat_result), 0);
	cl_assert_equal_i((stat_result.st_mode & 0777),
			  opts.default_permissions);

	reftable_buf_reset(&scratch);
	cl_assert_equal_i(reftable_buf_addstr(&scratch, dir), 0);
	cl_assert_equal_i(reftable_buf_addstr(&scratch, "/"), 0);
	/* do not try at home; not an external API for reftable. */
	cl_assert(!reftable_buf_addstr(&scratch, st->tables[0]->name));
	err = stat(scratch.buf, &stat_result);
	cl_assert(!err);
	cl_assert_equal_i((stat_result.st_mode & 0777),
			   opts.default_permissions);
#else
	(void) stat_result;
#endif

	reftable_ref_record_release(&dest);
	reftable_stack_destroy(st);
	reftable_buf_release(&scratch);
	clear_dir(dir);
	umask(mask);
}

void test_reftable_stack__uptodate(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st1 = NULL;
	struct reftable_stack *st2 = NULL;
	char *dir = get_tmp_dir(__LINE__);

	struct reftable_ref_record ref1 = {
		.refname = (char *) "HEAD",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *) "master",
	};
	struct reftable_ref_record ref2 = {
		.refname = (char *) "branch2",
		.update_index = 2,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *) "master",
	};


	/* simulate multi-process access to the same stack
	   by creating two stacks for the same directory.
	 */
	cl_assert_equal_i(reftable_new_stack(&st1, dir, &opts), 0);
	cl_assert_equal_i(reftable_new_stack(&st2, dir, &opts), 0);
	cl_assert_equal_i(reftable_stack_add(st1, write_test_ref,
					     &ref1), 0);
	cl_assert_equal_i(reftable_stack_add(st2, write_test_ref,
					     &ref2), REFTABLE_OUTDATED_ERROR);
	cl_assert_equal_i(reftable_stack_reload(st2), 0);
	cl_assert_equal_i(reftable_stack_add(st2, write_test_ref,
					     &ref2), 0);
	reftable_stack_destroy(st1);
	reftable_stack_destroy(st2);
	clear_dir(dir);
}

void test_reftable_stack__transaction_api(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	struct reftable_addition *add = NULL;

	struct reftable_ref_record ref = {
		.refname = (char *) "HEAD",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *) "master",
	};
	struct reftable_ref_record dest = { 0 };

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	reftable_addition_destroy(add);

	cl_assert_equal_i(reftable_stack_new_addition(&add, st, 0), 0);
	cl_assert_equal_i(reftable_addition_add(add, write_test_ref,
						&ref), 0);
	cl_assert_equal_i(reftable_addition_commit(add), 0);

	reftable_addition_destroy(add);

	cl_assert_equal_i(reftable_stack_read_ref(st, ref.refname,
						  &dest), 0);
	cl_assert_equal_i(REFTABLE_REF_SYMREF, dest.value_type);
	cl_assert(reftable_ref_record_equal(&ref, &dest,
					    REFTABLE_HASH_SIZE_SHA1) != 0);

	reftable_ref_record_release(&dest);
	reftable_stack_destroy(st);
	clear_dir(dir);
}

void test_reftable_stack__transaction_with_reload(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_stack *st1 = NULL, *st2 = NULL;
	struct reftable_addition *add = NULL;
	struct reftable_ref_record refs[2] = {
		{
			.refname = (char *) "refs/heads/a",
			.update_index = 1,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { '1' },
		},
		{
			.refname = (char *) "refs/heads/b",
			.update_index = 2,
			.value_type = REFTABLE_REF_VAL1,
			.value.val1 = { '1' },
		},
	};
	struct reftable_ref_record ref = { 0 };

	cl_assert_equal_i(reftable_new_stack(&st1, dir, NULL), 0);
	cl_assert_equal_i(reftable_new_stack(&st2, dir, NULL), 0);
	cl_assert_equal_i(reftable_stack_new_addition(&add, st1, 0), 0);
	cl_assert_equal_i(reftable_addition_add(add, write_test_ref,
						&refs[0]), 0);
	cl_assert_equal_i(reftable_addition_commit(add), 0);
	reftable_addition_destroy(add);

	/*
	 * The second stack is now outdated, which we should notice. We do not
	 * create the addition and lock the stack by default, but allow the
	 * reload to happen when REFTABLE_STACK_NEW_ADDITION_RELOAD is set.
	 */
	cl_assert_equal_i(reftable_stack_new_addition(&add, st2, 0),
						      REFTABLE_OUTDATED_ERROR);
	cl_assert_equal_i(reftable_stack_new_addition(&add, st2,
						      REFTABLE_STACK_NEW_ADDITION_RELOAD), 0);
	cl_assert_equal_i(reftable_addition_add(add, write_test_ref,
						&refs[1]), 0);
	cl_assert_equal_i(reftable_addition_commit(add), 0);
	reftable_addition_destroy(add);

	for (size_t i = 0; i < ARRAY_SIZE(refs); i++) {
		cl_assert_equal_i(reftable_stack_read_ref(st2,
							  refs[i].refname, &ref) , 0);
		cl_assert(reftable_ref_record_equal(&refs[i], &ref,
						    REFTABLE_HASH_SIZE_SHA1) != 0);
	}

	reftable_ref_record_release(&ref);
	reftable_stack_destroy(st1);
	reftable_stack_destroy(st2);
	clear_dir(dir);
}

void test_reftable_stack__transaction_api_performs_auto_compaction(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_write_options opts = {0};
	struct reftable_addition *add = NULL;
	struct reftable_stack *st = NULL;
	size_t n = 20;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	for (size_t i = 0; i <= n; i++) {
		struct reftable_ref_record ref = {
			.update_index = reftable_stack_next_update_index(st),
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = (char *) "master",
		};
		char name[100];

		snprintf(name, sizeof(name), "branch%04"PRIuMAX, (uintmax_t)i);
		ref.refname = name;

		/*
		 * Disable auto-compaction for all but the last runs. Like this
		 * we can ensure that we indeed honor this setting and have
		 * better control over when exactly auto compaction runs.
		 */
		st->opts.disable_auto_compact = i != n;

		cl_assert_equal_i(reftable_stack_new_addition(&add,
							      st, 0), 0);
		cl_assert_equal_i(reftable_addition_add(add,
							write_test_ref, &ref), 0);
		cl_assert_equal_i(reftable_addition_commit(add), 0);

		reftable_addition_destroy(add);

		/*
		 * The stack length should grow continuously for all runs where
		 * auto compaction is disabled. When enabled, we should merge
		 * all tables in the stack.
		 */
		if (i != n)
			cl_assert_equal_i(st->merged->tables_len, i + 1);
		else
			cl_assert_equal_i(st->merged->tables_len, 1);
	}

	reftable_stack_destroy(st);
	clear_dir(dir);
}

void test_reftable_stack__auto_compaction_fails_gracefully(void)
{
	struct reftable_ref_record ref = {
		.refname = (char *) "refs/heads/master",
		.update_index = 1,
		.value_type = REFTABLE_REF_VAL1,
		.value.val1 = {0x01},
	};
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st;
	struct reftable_buf table_path = REFTABLE_BUF_INIT;
	char *dir = get_tmp_dir(__LINE__);
	int err;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);
	cl_assert_equal_i(reftable_stack_add(st, write_test_ref,
					     &ref), 0);
	cl_assert_equal_i(st->merged->tables_len, 1);
	cl_assert_equal_i(st->stats.attempts, 0);
	cl_assert_equal_i(st->stats.failures, 0);

	/*
	 * Lock the newly written table such that it cannot be compacted.
	 * Adding a new table to the stack should not be impacted by this, even
	 * though auto-compaction will now fail.
	 */
	cl_assert(!reftable_buf_addstr(&table_path, dir));
	cl_assert(!reftable_buf_addstr(&table_path, "/"));
	cl_assert(!reftable_buf_addstr(&table_path,
				       st->tables[0]->name));
	cl_assert(!reftable_buf_addstr(&table_path, ".lock"));
	write_file_buf(table_path.buf, "", 0);

	ref.update_index = 2;
	err = reftable_stack_add(st, write_test_ref, &ref);
	cl_assert(!err);
	cl_assert_equal_i(st->merged->tables_len, 2);
	cl_assert_equal_i(st->stats.attempts, 1);
	cl_assert_equal_i(st->stats.failures, 1);

	reftable_stack_destroy(st);
	reftable_buf_release(&table_path);
	clear_dir(dir);
}

static int write_error(struct reftable_writer *wr UNUSED, void *arg)
{
	return *((int *)arg);
}

void test_reftable_stack__update_index_check(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	struct reftable_ref_record ref1 = {
		.refname = (char *) "name1",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *) "master",
	};
	struct reftable_ref_record ref2 = {
		.refname = (char *) "name2",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *) "master",
	};

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);
	cl_assert_equal_i(reftable_stack_add(st, write_test_ref,
					     &ref1), 0);
	cl_assert_equal_i(reftable_stack_add(st, write_test_ref,
					     &ref2), REFTABLE_API_ERROR);
	reftable_stack_destroy(st);
	clear_dir(dir);
}

void test_reftable_stack__lock_failure(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	int i;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);
	for (i = -1; i != REFTABLE_EMPTY_TABLE_ERROR; i--)
		cl_assert_equal_i(reftable_stack_add(st, write_error,
						     &i), i);

	reftable_stack_destroy(st);
	clear_dir(dir);
}

void test_reftable_stack__add(void)
{
	struct reftable_write_options opts = {
		.exact_log_message = 1,
		.default_permissions = 0660,
		.disable_auto_compact = 1,
	};
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_ref_record refs[2] = { 0 };
	struct reftable_log_record logs[2] = { 0 };
	struct reftable_buf path = REFTABLE_BUF_INIT;
	struct stat stat_result;
	size_t i, N = ARRAY_SIZE(refs);
	int err = 0;

	err = reftable_new_stack(&st, dir, &opts);
	cl_assert(!err);

	for (i = 0; i < N; i++) {
		char buf[256];
		snprintf(buf, sizeof(buf), "branch%02"PRIuMAX, (uintmax_t)i);
		refs[i].refname = xstrdup(buf);
		refs[i].update_index = i + 1;
		refs[i].value_type = REFTABLE_REF_VAL1;
		cl_reftable_set_hash(refs[i].value.val1, i,
				     REFTABLE_HASH_SHA1);

		logs[i].refname = xstrdup(buf);
		logs[i].update_index = N + i + 1;
		logs[i].value_type = REFTABLE_LOG_UPDATE;
		logs[i].value.update.email = xstrdup("identity@invalid");
		cl_reftable_set_hash(logs[i].value.update.new_hash, i,
				     REFTABLE_HASH_SHA1);
	}

	for (i = 0; i < N; i++)
		cl_assert_equal_i(reftable_stack_add(st, write_test_ref,
					     &refs[i]), 0);

	for (i = 0; i < N; i++) {
		struct write_log_arg arg = {
			.log = &logs[i],
			.update_index = reftable_stack_next_update_index(st),
		};
		cl_assert_equal_i(reftable_stack_add(st, write_test_log,
						     &arg), 0);
	}

	cl_assert_equal_i(reftable_stack_compact_all(st, NULL), 0);

	for (i = 0; i < N; i++) {
		struct reftable_ref_record dest = { 0 };

		cl_assert_equal_i(reftable_stack_read_ref(st,
							  refs[i].refname, &dest), 0);
		cl_assert(reftable_ref_record_equal(&dest, refs + i,
						    REFTABLE_HASH_SIZE_SHA1) != 0);
		reftable_ref_record_release(&dest);
	}

	for (i = 0; i < N; i++) {
		struct reftable_log_record dest = { 0 };
		cl_assert_equal_i(reftable_stack_read_log(st,
							  refs[i].refname, &dest), 0);
		cl_assert(reftable_log_record_equal(&dest, logs + i,
						    REFTABLE_HASH_SIZE_SHA1) != 0);
		reftable_log_record_release(&dest);
	}

#ifndef GIT_WINDOWS_NATIVE
	cl_assert_equal_i(reftable_buf_addstr(&path, dir), 0);
	cl_assert_equal_i(reftable_buf_addstr(&path, "/tables.list"), 0);
	cl_assert_equal_i(stat(path.buf, &stat_result), 0);
	cl_assert_equal_i((stat_result.st_mode & 0777), opts.default_permissions);

	reftable_buf_reset(&path);
	cl_assert_equal_i(reftable_buf_addstr(&path, dir), 0);
	cl_assert_equal_i(reftable_buf_addstr(&path, "/"), 0);
	/* do not try at home; not an external API for reftable. */
	cl_assert(!reftable_buf_addstr(&path, st->tables[0]->name));
	err = stat(path.buf, &stat_result);
	cl_assert(!err);
	cl_assert_equal_i((stat_result.st_mode & 0777),
			  opts.default_permissions);
#else
	(void) stat_result;
#endif

	/* cleanup */
	reftable_stack_destroy(st);
	for (i = 0; i < N; i++) {
		reftable_ref_record_release(&refs[i]);
		reftable_log_record_release(&logs[i]);
	}
	reftable_buf_release(&path);
	clear_dir(dir);
}

void test_reftable_stack__iterator(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_ref_record refs[10] = { 0 };
	struct reftable_log_record logs[10] = { 0 };
	struct reftable_iterator it = { 0 };
	size_t N = ARRAY_SIZE(refs), i;
	int err;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	for (i = 0; i < N; i++) {
		refs[i].refname = xstrfmt("branch%02"PRIuMAX, (uintmax_t)i);
		refs[i].update_index = i + 1;
		refs[i].value_type = REFTABLE_REF_VAL1;
		cl_reftable_set_hash(refs[i].value.val1, i,
				     REFTABLE_HASH_SHA1);

		logs[i].refname = xstrfmt("branch%02"PRIuMAX, (uintmax_t)i);
		logs[i].update_index = i + 1;
		logs[i].value_type = REFTABLE_LOG_UPDATE;
		logs[i].value.update.email = xstrdup("johndoe@invalid");
		logs[i].value.update.message = xstrdup("commit\n");
		cl_reftable_set_hash(logs[i].value.update.new_hash, i,
				     REFTABLE_HASH_SHA1);
	}

	for (i = 0; i < N; i++)
		cl_assert_equal_i(reftable_stack_add(st,
						     write_test_ref, &refs[i]), 0);

	for (i = 0; i < N; i++) {
		struct write_log_arg arg = {
			.log = &logs[i],
			.update_index = reftable_stack_next_update_index(st),
		};

		cl_assert_equal_i(reftable_stack_add(st,
						     write_test_log, &arg), 0);
	}

	reftable_stack_init_ref_iterator(st, &it);
	reftable_iterator_seek_ref(&it, refs[0].refname);
	for (i = 0; ; i++) {
		struct reftable_ref_record ref = { 0 };

		err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0)
			break;
		cl_assert(!err);
		cl_assert(reftable_ref_record_equal(&ref, &refs[i],
						    REFTABLE_HASH_SIZE_SHA1) != 0);
		reftable_ref_record_release(&ref);
	}
	cl_assert_equal_i(i, N);

	reftable_iterator_destroy(&it);

	cl_assert_equal_i(reftable_stack_init_log_iterator(st, &it), 0);

	reftable_iterator_seek_log(&it, logs[0].refname);
	for (i = 0; ; i++) {
		struct reftable_log_record log = { 0 };

		err = reftable_iterator_next_log(&it, &log);
		if (err > 0)
			break;
		cl_assert(!err);
		cl_assert(reftable_log_record_equal(&log, &logs[i],
						    REFTABLE_HASH_SIZE_SHA1) != 0);
		reftable_log_record_release(&log);
	}
	cl_assert_equal_i(i, N);

	reftable_stack_destroy(st);
	reftable_iterator_destroy(&it);
	for (i = 0; i < N; i++) {
		reftable_ref_record_release(&refs[i]);
		reftable_log_record_release(&logs[i]);
	}
	clear_dir(dir);
}

void test_reftable_stack__log_normalize(void)
{
	struct reftable_write_options opts = {
		0,
	};
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_log_record input = {
		.refname = (char *) "branch",
		.update_index = 1,
		.value_type = REFTABLE_LOG_UPDATE,
		.value = {
			.update = {
				.new_hash = { 1 },
				.old_hash = { 2 },
			},
		},
	};
	struct reftable_log_record dest = {
		.update_index = 0,
	};
	struct write_log_arg arg = {
		.log = &input,
		.update_index = 1,
	};

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	input.value.update.message = (char *) "one\ntwo";
	cl_assert_equal_i(reftable_stack_add(st, write_test_log,
					     &arg), REFTABLE_API_ERROR);

	input.value.update.message = (char *) "one";
	cl_assert_equal_i(reftable_stack_add(st, write_test_log,
					     &arg), 0);
	cl_assert_equal_i(reftable_stack_read_log(st, input.refname,
						  &dest), 0);
	cl_assert_equal_s(dest.value.update.message, "one\n");

	input.value.update.message = (char *) "two\n";
	arg.update_index = 2;
	cl_assert_equal_i(reftable_stack_add(st, write_test_log,
					     &arg), 0);
	cl_assert_equal_i(reftable_stack_read_log(st, input.refname,
						  &dest), 0);
	cl_assert_equal_s(dest.value.update.message, "two\n");

	/* cleanup */
	reftable_stack_destroy(st);
	reftable_log_record_release(&dest);
	clear_dir(dir);
}

void test_reftable_stack__tombstone(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	struct reftable_ref_record refs[2] = { 0 };
	struct reftable_log_record logs[2] = { 0 };
	size_t i, N = ARRAY_SIZE(refs);
	struct reftable_ref_record dest = { 0 };
	struct reftable_log_record log_dest = { 0 };

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	/* even entries add the refs, odd entries delete them. */
	for (i = 0; i < N; i++) {
		const char *buf = "branch";
		refs[i].refname = xstrdup(buf);
		refs[i].update_index = i + 1;
		if (i % 2 == 0) {
			refs[i].value_type = REFTABLE_REF_VAL1;
			cl_reftable_set_hash(refs[i].value.val1, i,
					     REFTABLE_HASH_SHA1);
		}

		logs[i].refname = xstrdup(buf);
		/*
		 * update_index is part of the key so should be constant.
		 * The value itself should be less than the writer's upper
		 * limit.
		 */
		logs[i].update_index = 1;
		if (i % 2 == 0) {
			logs[i].value_type = REFTABLE_LOG_UPDATE;
			cl_reftable_set_hash(logs[i].value.update.new_hash, i, REFTABLE_HASH_SHA1);
			logs[i].value.update.email =
				xstrdup("identity@invalid");
		}
	}
	for (i = 0; i < N; i++)
		cl_assert_equal_i(reftable_stack_add(st, write_test_ref, &refs[i]), 0);

	for (i = 0; i < N; i++) {
		struct write_log_arg arg = {
			.log = &logs[i],
			.update_index = reftable_stack_next_update_index(st),
		};
		cl_assert_equal_i(reftable_stack_add(st,
						     write_test_log, &arg), 0);
	}

	cl_assert_equal_i(reftable_stack_read_ref(st, "branch",
						  &dest), 1);
	reftable_ref_record_release(&dest);

	cl_assert_equal_i(reftable_stack_read_log(st, "branch",
						  &log_dest), 1);
	reftable_log_record_release(&log_dest);

	cl_assert_equal_i(reftable_stack_compact_all(st, NULL), 0);
	cl_assert_equal_i(reftable_stack_read_ref(st, "branch",
						  &dest), 1);
	cl_assert_equal_i(reftable_stack_read_log(st, "branch",
						  &log_dest), 1);
	reftable_ref_record_release(&dest);
	reftable_log_record_release(&log_dest);

	/* cleanup */
	reftable_stack_destroy(st);
	for (i = 0; i < N; i++) {
		reftable_ref_record_release(&refs[i]);
		reftable_log_record_release(&logs[i]);
	}
	clear_dir(dir);
}

void test_reftable_stack__hash_id(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;

	struct reftable_ref_record ref = {
		.refname = (char *) "master",
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *) "target",
		.update_index = 1,
	};
	struct reftable_write_options opts32 = { .hash_id = REFTABLE_HASH_SHA256 };
	struct reftable_stack *st32 = NULL;
	struct reftable_write_options opts_default = { 0 };
	struct reftable_stack *st_default = NULL;
	struct reftable_ref_record dest = { 0 };

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);
	cl_assert_equal_i(reftable_stack_add(st, write_test_ref,
					     &ref), 0);

	/* can't read it with the wrong hash ID. */
	cl_assert_equal_i(reftable_new_stack(&st32, dir,
					     &opts32), REFTABLE_FORMAT_ERROR);

	/* check that we can read it back with default opts too. */
	cl_assert_equal_i(reftable_new_stack(&st_default, dir,
					     &opts_default), 0);
	cl_assert_equal_i(reftable_stack_read_ref(st_default, "master",
						  &dest), 0);
	cl_assert(reftable_ref_record_equal(&ref, &dest,
					    REFTABLE_HASH_SIZE_SHA1) != 0);
	reftable_ref_record_release(&dest);
	reftable_stack_destroy(st);
	reftable_stack_destroy(st_default);
	clear_dir(dir);
}

void test_reftable_stack__suggest_compaction_segment(void)
{
	uint64_t sizes[] = { 512, 64, 17, 16, 9, 9, 9, 16, 2, 16 };
	struct segment min =
		suggest_compaction_segment(sizes, ARRAY_SIZE(sizes), 2);
	cl_assert_equal_i(min.start, 1);
	cl_assert_equal_i(min.end, 10);
}

void test_reftable_stack__suggest_compaction_segment_nothing(void)
{
	uint64_t sizes[] = { 64, 32, 16, 8, 4, 2 };
	struct segment result =
		suggest_compaction_segment(sizes, ARRAY_SIZE(sizes), 2);
	cl_assert_equal_i(result.start, result.end);
}

void test_reftable_stack__reflog_expire(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	struct reftable_log_record logs[20] = { 0 };
	size_t i, N = ARRAY_SIZE(logs) - 1;
	struct reftable_log_expiry_config expiry = {
		.time = 10,
	};
	struct reftable_log_record log = { 0 };

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	for (i = 1; i <= N; i++) {
		char buf[256];
		snprintf(buf, sizeof(buf), "branch%02"PRIuMAX, (uintmax_t)i);

		logs[i].refname = xstrdup(buf);
		logs[i].update_index = i;
		logs[i].value_type = REFTABLE_LOG_UPDATE;
		logs[i].value.update.time = i;
		logs[i].value.update.email = xstrdup("identity@invalid");
		cl_reftable_set_hash(logs[i].value.update.new_hash, i,
				     REFTABLE_HASH_SHA1);
	}

	for (i = 1; i <= N; i++) {
		struct write_log_arg arg = {
			.log = &logs[i],
			.update_index = reftable_stack_next_update_index(st),
		};
		cl_assert_equal_i(reftable_stack_add(st, write_test_log,
						     &arg), 0);
	}

	cl_assert_equal_i(reftable_stack_compact_all(st, NULL), 0);
	cl_assert_equal_i(reftable_stack_compact_all(st, &expiry), 0);
	cl_assert_equal_i(reftable_stack_read_log(st, logs[9].refname,
						  &log), 1);
	cl_assert_equal_i(reftable_stack_read_log(st, logs[11].refname,
						  &log), 0);

	expiry.min_update_index = 15;
	cl_assert_equal_i(reftable_stack_compact_all(st, &expiry), 0);
	cl_assert_equal_i(reftable_stack_read_log(st, logs[14].refname,
						  &log), 1);
	cl_assert_equal_i(reftable_stack_read_log(st, logs[16].refname,
						  &log), 0);

	/* cleanup */
	reftable_stack_destroy(st);
	for (i = 0; i <= N; i++)
		reftable_log_record_release(&logs[i]);
	clear_dir(dir);
	reftable_log_record_release(&log);
}

static int write_nothing(struct reftable_writer *wr, void *arg UNUSED)
{
	cl_assert_equal_i(reftable_writer_set_limits(wr, 1, 1), 0);
	return 0;
}

void test_reftable_stack__empty_add(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_stack *st2 = NULL;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);
	cl_assert_equal_i(reftable_stack_add(st, write_nothing,
					     NULL), 0);
	cl_assert_equal_i(reftable_new_stack(&st2, dir, &opts), 0);
	clear_dir(dir);
	reftable_stack_destroy(st);
	reftable_stack_destroy(st2);
}

static int fastlogN(uint64_t sz, uint64_t N)
{
	int l = 0;
	if (sz == 0)
		return 0;
	for (; sz; sz /= N)
		l++;
	return l - 1;
}

void test_reftable_stack__auto_compaction(void)
{
	struct reftable_write_options opts = {
		.disable_auto_compact = 1,
	};
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);
	size_t i, N = 100;
	int err;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	for (i = 0; i < N; i++) {
		char name[100];
		struct reftable_ref_record ref = {
			.refname = name,
			.update_index = reftable_stack_next_update_index(st),
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = (char *) "master",
		};
		snprintf(name, sizeof(name), "branch%04"PRIuMAX, (uintmax_t)i);

		err = reftable_stack_add(st, write_test_ref, &ref);
		cl_assert(!err);

		err = reftable_stack_auto_compact(st);
		cl_assert(!err);
		cl_assert(i < 2 || st->merged->tables_len < 2 * fastlogN(i, 2));
	}

	cl_assert(reftable_stack_compaction_stats(st)->entries_written <
			  (uint64_t)(N * fastlogN(N, 2)));

	reftable_stack_destroy(st);
	clear_dir(dir);
}

void test_reftable_stack__auto_compaction_factor(void)
{
	struct reftable_write_options opts = {
		.auto_compaction_factor = 5,
	};
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);
	size_t N = 100;
	int err;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	for (size_t i = 0; i < N; i++) {
		char name[20];
		struct reftable_ref_record ref = {
			.refname = name,
			.update_index = reftable_stack_next_update_index(st),
			.value_type = REFTABLE_REF_VAL1,
		};
		xsnprintf(name, sizeof(name), "branch%04"PRIuMAX, (uintmax_t)i);

		err = reftable_stack_add(st, &write_test_ref, &ref);
		cl_assert(!err);

		cl_assert(i < 5 || st->merged->tables_len < 5 * fastlogN(i, 5));
	}

	reftable_stack_destroy(st);
	clear_dir(dir);
}

void test_reftable_stack__auto_compaction_with_locked_tables(void)
{
	struct reftable_write_options opts = {
		.disable_auto_compact = 1,
	};
	struct reftable_stack *st = NULL;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	char *dir = get_tmp_dir(__LINE__);
	int err;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	write_n_ref_tables(st, 5);
	cl_assert_equal_i(st->merged->tables_len, 5);

	/*
	 * Given that all tables we have written should be roughly the same
	 * size, we expect that auto-compaction will want to compact all of the
	 * tables. Locking any of the tables will keep it from doing so.
	 */
	cl_assert(!reftable_buf_addstr(&buf, dir));
	cl_assert(!reftable_buf_addstr(&buf, "/"));
	cl_assert(!reftable_buf_addstr(&buf, st->tables[2]->name));
	cl_assert(!reftable_buf_addstr(&buf, ".lock"));
	write_file_buf(buf.buf, "", 0);

	/*
	 * When parts of the stack are locked, then auto-compaction does a best
	 * effort compaction of those tables which aren't locked. So while this
	 * would in theory compact all tables, due to the preexisting lock we
	 * only compact the newest two tables.
	 */
	err = reftable_stack_auto_compact(st);
	cl_assert(!err);
	cl_assert_equal_i(st->stats.failures, 0);
	cl_assert_equal_i(st->merged->tables_len, 4);

	reftable_stack_destroy(st);
	reftable_buf_release(&buf);
	clear_dir(dir);
}

void test_reftable_stack__add_performs_auto_compaction(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);
	size_t i, n = 20;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	for (i = 0; i <= n; i++) {
		struct reftable_ref_record ref = {
			.update_index = reftable_stack_next_update_index(st),
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = (char *) "master",
		};
		char buf[128];

		/*
		 * Disable auto-compaction for all but the last runs. Like this
		 * we can ensure that we indeed honor this setting and have
		 * better control over when exactly auto compaction runs.
		 */
		st->opts.disable_auto_compact = i != n;

		snprintf(buf, sizeof(buf), "branch-%04"PRIuMAX, (uintmax_t)i);
		ref.refname = buf;

		cl_assert_equal_i(reftable_stack_add(st,
						     write_test_ref, &ref), 0);

		/*
		 * The stack length should grow continuously for all runs where
		 * auto compaction is disabled. When enabled, we should merge
		 * all tables in the stack.
		 */
		if (i != n)
			cl_assert_equal_i(st->merged->tables_len, i + 1);
		else
			cl_assert_equal_i(st->merged->tables_len, 1);
	}

	reftable_stack_destroy(st);
	clear_dir(dir);
}

void test_reftable_stack__compaction_with_locked_tables(void)
{
	struct reftable_write_options opts = {
		.disable_auto_compact = 1,
	};
	struct reftable_stack *st = NULL;
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	char *dir = get_tmp_dir(__LINE__);
	int err;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	write_n_ref_tables(st, 3);
	cl_assert_equal_i(st->merged->tables_len, 3);

	/* Lock one of the tables that we're about to compact. */
	cl_assert(!reftable_buf_addstr(&buf, dir));
	cl_assert(!reftable_buf_addstr(&buf, "/"));
	cl_assert(!reftable_buf_addstr(&buf, st->tables[1]->name));
	cl_assert(!reftable_buf_addstr(&buf, ".lock"));
	write_file_buf(buf.buf, "", 0);

	/*
	 * Compaction is expected to fail given that we were not able to
	 * compact all tables.
	 */
	err = reftable_stack_compact_all(st, NULL);
	cl_assert_equal_i(err, REFTABLE_LOCK_ERROR);
	cl_assert_equal_i(st->stats.failures, 1);
	cl_assert_equal_i(st->merged->tables_len, 3);

	reftable_stack_destroy(st);
	reftable_buf_release(&buf);
	clear_dir(dir);
}

void test_reftable_stack__compaction_concurrent(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st1 = NULL, *st2 = NULL;
	char *dir = get_tmp_dir(__LINE__);

	cl_assert_equal_i(reftable_new_stack(&st1, dir, &opts), 0);
	write_n_ref_tables(st1, 3);

	cl_assert_equal_i(reftable_new_stack(&st2, dir, &opts), 0);
	cl_assert_equal_i(reftable_stack_compact_all(st1, NULL), 0);

	reftable_stack_destroy(st1);
	reftable_stack_destroy(st2);

	cl_assert_equal_i(count_dir_entries(dir), 2);
	clear_dir(dir);
}

static void unclean_stack_close(struct reftable_stack *st)
{
	/* break abstraction boundary to simulate unclean shutdown. */
	for (size_t i = 0; i < st->tables_len; i++)
		reftable_table_decref(st->tables[i]);
	st->tables_len = 0;
	REFTABLE_FREE_AND_NULL(st->tables);
}

void test_reftable_stack__compaction_concurrent_clean(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st1 = NULL, *st2 = NULL, *st3 = NULL;
	char *dir = get_tmp_dir(__LINE__);

	cl_assert_equal_i(reftable_new_stack(&st1, dir, &opts), 0);
	write_n_ref_tables(st1, 3);

	cl_assert_equal_i(reftable_new_stack(&st2, dir, &opts), 0);
	cl_assert_equal_i(reftable_stack_compact_all(st1, NULL), 0);

	unclean_stack_close(st1);
	unclean_stack_close(st2);

	cl_assert_equal_i(reftable_new_stack(&st3, dir, &opts), 0);
	cl_assert_equal_i(reftable_stack_clean(st3), 0);
	cl_assert_equal_i(count_dir_entries(dir), 2);

	reftable_stack_destroy(st1);
	reftable_stack_destroy(st2);
	reftable_stack_destroy(st3);

	clear_dir(dir);
}

void test_reftable_stack__read_across_reload(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st1 = NULL, *st2 = NULL;
	struct reftable_ref_record rec = { 0 };
	struct reftable_iterator it = { 0 };
	char *dir = get_tmp_dir(__LINE__);
	int err;

	/* Create a first stack and set up an iterator for it. */
	cl_assert_equal_i(reftable_new_stack(&st1, dir, &opts), 0);
	write_n_ref_tables(st1, 2);
	cl_assert_equal_i(st1->merged->tables_len, 2);
	reftable_stack_init_ref_iterator(st1, &it);
	cl_assert_equal_i(reftable_iterator_seek_ref(&it, ""), 0);

	/* Set up a second stack for the same directory and compact it. */
	err = reftable_new_stack(&st2, dir, &opts);
	cl_assert(!err);
	cl_assert_equal_i(st2->merged->tables_len, 2);
	err = reftable_stack_compact_all(st2, NULL);
	cl_assert(!err);
	cl_assert_equal_i(st2->merged->tables_len, 1);

	/*
	 * Verify that we can continue to use the old iterator even after we
	 * have reloaded its stack.
	 */
	err = reftable_stack_reload(st1);
	cl_assert(!err);
	cl_assert_equal_i(st1->merged->tables_len, 1);
	err = reftable_iterator_next_ref(&it, &rec);
	cl_assert(!err);
	cl_assert_equal_s(rec.refname, "refs/heads/branch-0000");
	err = reftable_iterator_next_ref(&it, &rec);
	cl_assert(!err);
	cl_assert_equal_s(rec.refname, "refs/heads/branch-0001");
	err = reftable_iterator_next_ref(&it, &rec);
	cl_assert(err > 0);

	reftable_ref_record_release(&rec);
	reftable_iterator_destroy(&it);
	reftable_stack_destroy(st1);
	reftable_stack_destroy(st2);
	clear_dir(dir);
}

void test_reftable_stack__reload_with_missing_table(void)
{
	struct reftable_write_options opts = { 0 };
	struct reftable_stack *st = NULL;
	struct reftable_ref_record rec = { 0 };
	struct reftable_iterator it = { 0 };
	struct reftable_buf table_path = REFTABLE_BUF_INIT, content = REFTABLE_BUF_INIT;
	char *dir = get_tmp_dir(__LINE__);
	int err;

	/* Create a first stack and set up an iterator for it. */
	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);
	write_n_ref_tables(st, 2);
	cl_assert_equal_i(st->merged->tables_len, 2);
	reftable_stack_init_ref_iterator(st, &it);
	cl_assert_equal_i(reftable_iterator_seek_ref(&it, ""), 0);

	/*
	 * Update the tables.list file with some garbage data, while reusing
	 * our old tables. This should trigger a partial reload of the stack,
	 * where we try to reuse our old tables.
	*/
	cl_assert(!reftable_buf_addstr(&content, st->tables[0]->name));
	cl_assert(!reftable_buf_addstr(&content, "\n"));
	cl_assert(!reftable_buf_addstr(&content, st->tables[1]->name));
	cl_assert(!reftable_buf_addstr(&content, "\n"));
	cl_assert(!reftable_buf_addstr(&content, "garbage\n"));
	cl_assert(!reftable_buf_addstr(&table_path, st->list_file));
	cl_assert(!reftable_buf_addstr(&table_path, ".lock"));
	write_file_buf(table_path.buf, content.buf, content.len);
	cl_assert_equal_i(rename(table_path.buf, st->list_file), 0);

	err = reftable_stack_reload(st);
	cl_assert_equal_i(err, -4);
	cl_assert_equal_i(st->merged->tables_len, 2);

	/*
	 * Even though the reload has failed, we should be able to continue
	 * using the iterator.
	*/
	cl_assert_equal_i(reftable_iterator_next_ref(&it, &rec), 0);
	cl_assert_equal_s(rec.refname, "refs/heads/branch-0000");
	cl_assert_equal_i(reftable_iterator_next_ref(&it, &rec), 0);
	cl_assert_equal_s(rec.refname, "refs/heads/branch-0001");
	cl_assert(reftable_iterator_next_ref(&it, &rec) > 0);

	reftable_ref_record_release(&rec);
	reftable_iterator_destroy(&it);
	reftable_stack_destroy(st);
	reftable_buf_release(&table_path);
	reftable_buf_release(&content);
	clear_dir(dir);
}

static int write_limits_after_ref(struct reftable_writer *wr, void *arg)
{
	struct reftable_ref_record *ref = arg;
	cl_assert_equal_i(reftable_writer_set_limits(wr,
						     ref->update_index, ref->update_index), 0);
	cl_assert_equal_i(reftable_writer_add_ref(wr, ref), 0);
	return reftable_writer_set_limits(wr, ref->update_index, ref->update_index);
}

void test_reftable_stack__invalid_limit_updates(void)
{
	struct reftable_ref_record ref = {
		.refname = (char *) "HEAD",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = (char *) "master",
	};
	struct reftable_write_options opts = {
		.default_permissions = 0660,
	};
	struct reftable_addition *add = NULL;
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_stack *st = NULL;

	cl_assert_equal_i(reftable_new_stack(&st, dir, &opts), 0);

	reftable_addition_destroy(add);

	cl_assert_equal_i(reftable_stack_new_addition(&add, st, 0), 0);

	/*
	 * write_limits_after_ref also updates the update indexes after adding
	 * the record. This should cause an err to be returned, since the limits
	 * must be set at the start.
	 */
	cl_assert_equal_i(reftable_addition_add(add,
						write_limits_after_ref, &ref), REFTABLE_API_ERROR);

	reftable_addition_destroy(add);
	reftable_stack_destroy(st);
	clear_dir(dir);
}
