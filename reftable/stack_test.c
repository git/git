/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "stack.h"

#include "system.h"

#include "reftable-reader.h"
#include "merged.h"
#include "basics.h"
#include "record.h"
#include "test_framework.h"
#include "reftable-tests.h"
#include "reader.h"

#include <sys/types.h>
#include <dirent.h>

static void clear_dir(const char *dirname)
{
	struct strbuf path = STRBUF_INIT;
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
		if (!strcmp(d->d_name, "..") || !strcmp(d->d_name, "."))
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
	EXPECT(mkdtemp(dir));
	return dir;
}

static void test_read_file(void)
{
	char *fn = get_tmp_template(__LINE__);
	int fd = mkstemp(fn);
	char out[1024] = "line1\n\nline2\nline3";
	int n, err;
	char **names = NULL;
	char *want[] = { "line1", "line2", "line3" };
	int i = 0;

	EXPECT(fd > 0);
	n = write_in_full(fd, out, strlen(out));
	EXPECT(n == strlen(out));
	err = close(fd);
	EXPECT(err >= 0);

	err = read_lines(fn, &names);
	EXPECT_ERR(err);

	for (i = 0; names[i]; i++) {
		EXPECT(0 == strcmp(want[i], names[i]));
	}
	free_names(names);
	(void) remove(fn);
}

static void test_parse_names(void)
{
	char buf[] = "line\n";
	char **names = NULL;
	parse_names(buf, strlen(buf), &names);

	EXPECT(NULL != names[0]);
	EXPECT(0 == strcmp(names[0], "line"));
	EXPECT(NULL == names[1]);
	free_names(names);
}

static void test_names_equal(void)
{
	char *a[] = { "a", "b", "c", NULL };
	char *b[] = { "a", "b", "d", NULL };
	char *c[] = { "a", "b", NULL };

	EXPECT(names_equal(a, a));
	EXPECT(!names_equal(a, b));
	EXPECT(!names_equal(a, c));
}

static int write_test_ref(struct reftable_writer *wr, void *arg)
{
	struct reftable_ref_record *ref = arg;
	reftable_writer_set_limits(wr, ref->update_index, ref->update_index);
	return reftable_writer_add_ref(wr, ref);
}

struct write_log_arg {
	struct reftable_log_record *log;
	uint64_t update_index;
};

static int write_test_log(struct reftable_writer *wr, void *arg)
{
	struct write_log_arg *wla = arg;

	reftable_writer_set_limits(wr, wla->update_index, wla->update_index);
	return reftable_writer_add_log(wr, wla->log);
}

static void test_reftable_stack_add_one(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct strbuf scratch = STRBUF_INIT;
	int mask = umask(002);
	struct reftable_write_options cfg = {
		.default_permissions = 0660,
	};
	struct reftable_stack *st = NULL;
	int err;
	struct reftable_ref_record ref = {
		.refname = "HEAD",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = "master",
	};
	struct reftable_ref_record dest = { NULL };
	struct stat stat_result = { 0 };
	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_add(st, &write_test_ref, &ref);
	EXPECT_ERR(err);

	err = reftable_stack_read_ref(st, ref.refname, &dest);
	EXPECT_ERR(err);
	EXPECT(0 == strcmp("master", dest.value.symref));
	EXPECT(st->readers_len > 0);

	printf("testing print functionality:\n");
	err = reftable_stack_print_directory(dir, GIT_SHA1_FORMAT_ID);
	EXPECT_ERR(err);

	err = reftable_stack_print_directory(dir, GIT_SHA256_FORMAT_ID);
	EXPECT(err == REFTABLE_FORMAT_ERROR);

#ifndef GIT_WINDOWS_NATIVE
	strbuf_addstr(&scratch, dir);
	strbuf_addstr(&scratch, "/tables.list");
	err = stat(scratch.buf, &stat_result);
	EXPECT(!err);
	EXPECT((stat_result.st_mode & 0777) == cfg.default_permissions);

	strbuf_reset(&scratch);
	strbuf_addstr(&scratch, dir);
	strbuf_addstr(&scratch, "/");
	/* do not try at home; not an external API for reftable. */
	strbuf_addstr(&scratch, st->readers[0]->name);
	err = stat(scratch.buf, &stat_result);
	EXPECT(!err);
	EXPECT((stat_result.st_mode & 0777) == cfg.default_permissions);
#else
	(void) stat_result;
#endif

	reftable_ref_record_release(&dest);
	reftable_stack_destroy(st);
	strbuf_release(&scratch);
	clear_dir(dir);
	umask(mask);
}

static void test_reftable_stack_uptodate(void)
{
	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st1 = NULL;
	struct reftable_stack *st2 = NULL;
	char *dir = get_tmp_dir(__LINE__);

	int err;
	struct reftable_ref_record ref1 = {
		.refname = "HEAD",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = "master",
	};
	struct reftable_ref_record ref2 = {
		.refname = "branch2",
		.update_index = 2,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = "master",
	};


	/* simulate multi-process access to the same stack
	   by creating two stacks for the same directory.
	 */
	err = reftable_new_stack(&st1, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_new_stack(&st2, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_add(st1, &write_test_ref, &ref1);
	EXPECT_ERR(err);

	err = reftable_stack_add(st2, &write_test_ref, &ref2);
	EXPECT(err == REFTABLE_LOCK_ERROR);

	err = reftable_stack_reload(st2);
	EXPECT_ERR(err);

	err = reftable_stack_add(st2, &write_test_ref, &ref2);
	EXPECT_ERR(err);
	reftable_stack_destroy(st1);
	reftable_stack_destroy(st2);
	clear_dir(dir);
}

static void test_reftable_stack_transaction_api(void)
{
	char *dir = get_tmp_dir(__LINE__);

	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	int err;
	struct reftable_addition *add = NULL;

	struct reftable_ref_record ref = {
		.refname = "HEAD",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = "master",
	};
	struct reftable_ref_record dest = { NULL };


	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	reftable_addition_destroy(add);

	err = reftable_stack_new_addition(&add, st);
	EXPECT_ERR(err);

	err = reftable_addition_add(add, &write_test_ref, &ref);
	EXPECT_ERR(err);

	err = reftable_addition_commit(add);
	EXPECT_ERR(err);

	reftable_addition_destroy(add);

	err = reftable_stack_read_ref(st, ref.refname, &dest);
	EXPECT_ERR(err);
	EXPECT(REFTABLE_REF_SYMREF == dest.value_type);
	EXPECT(0 == strcmp("master", dest.value.symref));

	reftable_ref_record_release(&dest);
	reftable_stack_destroy(st);
	clear_dir(dir);
}

static void test_reftable_stack_transaction_api_performs_auto_compaction(void)
{
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_write_options cfg = {0};
	struct reftable_addition *add = NULL;
	struct reftable_stack *st = NULL;
	int i, n = 20, err;

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	for (i = 0; i <= n; i++) {
		struct reftable_ref_record ref = {
			.update_index = reftable_stack_next_update_index(st),
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = "master",
		};
		char name[100];

		snprintf(name, sizeof(name), "branch%04d", i);
		ref.refname = name;

		/*
		 * Disable auto-compaction for all but the last runs. Like this
		 * we can ensure that we indeed honor this setting and have
		 * better control over when exactly auto compaction runs.
		 */
		st->disable_auto_compact = i != n;

		err = reftable_stack_new_addition(&add, st);
		EXPECT_ERR(err);

		err = reftable_addition_add(add, &write_test_ref, &ref);
		EXPECT_ERR(err);

		err = reftable_addition_commit(add);
		EXPECT_ERR(err);

		reftable_addition_destroy(add);

		/*
		 * The stack length should grow continuously for all runs where
		 * auto compaction is disabled. When enabled, we should merge
		 * all tables in the stack.
		 */
		if (i != n)
			EXPECT(st->merged->stack_len == i + 1);
		else
			EXPECT(st->merged->stack_len == 1);
	}

	reftable_stack_destroy(st);
	clear_dir(dir);
}

static void test_reftable_stack_validate_refname(void)
{
	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	int err;
	char *dir = get_tmp_dir(__LINE__);

	int i;
	struct reftable_ref_record ref = {
		.refname = "a/b",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = "master",
	};
	char *additions[] = { "a", "a/b/c" };

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_add(st, &write_test_ref, &ref);
	EXPECT_ERR(err);

	for (i = 0; i < ARRAY_SIZE(additions); i++) {
		struct reftable_ref_record ref = {
			.refname = additions[i],
			.update_index = 1,
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = "master",
		};

		err = reftable_stack_add(st, &write_test_ref, &ref);
		EXPECT(err == REFTABLE_NAME_CONFLICT);
	}

	reftable_stack_destroy(st);
	clear_dir(dir);
}

static int write_error(struct reftable_writer *wr, void *arg)
{
	return *((int *)arg);
}

static void test_reftable_stack_update_index_check(void)
{
	char *dir = get_tmp_dir(__LINE__);

	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	int err;
	struct reftable_ref_record ref1 = {
		.refname = "name1",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = "master",
	};
	struct reftable_ref_record ref2 = {
		.refname = "name2",
		.update_index = 1,
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = "master",
	};

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_add(st, &write_test_ref, &ref1);
	EXPECT_ERR(err);

	err = reftable_stack_add(st, &write_test_ref, &ref2);
	EXPECT(err == REFTABLE_API_ERROR);
	reftable_stack_destroy(st);
	clear_dir(dir);
}

static void test_reftable_stack_lock_failure(void)
{
	char *dir = get_tmp_dir(__LINE__);

	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	int err, i;

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);
	for (i = -1; i != REFTABLE_EMPTY_TABLE_ERROR; i--) {
		err = reftable_stack_add(st, &write_error, &i);
		EXPECT(err == i);
	}

	reftable_stack_destroy(st);
	clear_dir(dir);
}

static void test_reftable_stack_add(void)
{
	int i = 0;
	int err = 0;
	struct reftable_write_options cfg = {
		.exact_log_message = 1,
		.default_permissions = 0660,
	};
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);
	struct reftable_ref_record refs[2] = { { NULL } };
	struct reftable_log_record logs[2] = { { NULL } };
	struct strbuf path = STRBUF_INIT;
	struct stat stat_result;
	int N = ARRAY_SIZE(refs);

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);
	st->disable_auto_compact = 1;

	for (i = 0; i < N; i++) {
		char buf[256];
		snprintf(buf, sizeof(buf), "branch%02d", i);
		refs[i].refname = xstrdup(buf);
		refs[i].update_index = i + 1;
		refs[i].value_type = REFTABLE_REF_VAL1;
		set_test_hash(refs[i].value.val1, i);

		logs[i].refname = xstrdup(buf);
		logs[i].update_index = N + i + 1;
		logs[i].value_type = REFTABLE_LOG_UPDATE;

		logs[i].value.update.new_hash = reftable_malloc(GIT_SHA1_RAWSZ);
		logs[i].value.update.email = xstrdup("identity@invalid");
		set_test_hash(logs[i].value.update.new_hash, i);
	}

	for (i = 0; i < N; i++) {
		int err = reftable_stack_add(st, &write_test_ref, &refs[i]);
		EXPECT_ERR(err);
	}

	for (i = 0; i < N; i++) {
		struct write_log_arg arg = {
			.log = &logs[i],
			.update_index = reftable_stack_next_update_index(st),
		};
		int err = reftable_stack_add(st, &write_test_log, &arg);
		EXPECT_ERR(err);
	}

	err = reftable_stack_compact_all(st, NULL);
	EXPECT_ERR(err);

	for (i = 0; i < N; i++) {
		struct reftable_ref_record dest = { NULL };

		int err = reftable_stack_read_ref(st, refs[i].refname, &dest);
		EXPECT_ERR(err);
		EXPECT(reftable_ref_record_equal(&dest, refs + i,
						 GIT_SHA1_RAWSZ));
		reftable_ref_record_release(&dest);
	}

	for (i = 0; i < N; i++) {
		struct reftable_log_record dest = { NULL };
		int err = reftable_stack_read_log(st, refs[i].refname, &dest);
		EXPECT_ERR(err);
		EXPECT(reftable_log_record_equal(&dest, logs + i,
						 GIT_SHA1_RAWSZ));
		reftable_log_record_release(&dest);
	}

#ifndef GIT_WINDOWS_NATIVE
	strbuf_addstr(&path, dir);
	strbuf_addstr(&path, "/tables.list");
	err = stat(path.buf, &stat_result);
	EXPECT(!err);
	EXPECT((stat_result.st_mode & 0777) == cfg.default_permissions);

	strbuf_reset(&path);
	strbuf_addstr(&path, dir);
	strbuf_addstr(&path, "/");
	/* do not try at home; not an external API for reftable. */
	strbuf_addstr(&path, st->readers[0]->name);
	err = stat(path.buf, &stat_result);
	EXPECT(!err);
	EXPECT((stat_result.st_mode & 0777) == cfg.default_permissions);
#else
	(void) stat_result;
#endif

	/* cleanup */
	reftable_stack_destroy(st);
	for (i = 0; i < N; i++) {
		reftable_ref_record_release(&refs[i]);
		reftable_log_record_release(&logs[i]);
	}
	strbuf_release(&path);
	clear_dir(dir);
}

static void test_reftable_stack_log_normalize(void)
{
	int err = 0;
	struct reftable_write_options cfg = {
		0,
	};
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);

	uint8_t h1[GIT_SHA1_RAWSZ] = { 0x01 }, h2[GIT_SHA1_RAWSZ] = { 0x02 };

	struct reftable_log_record input = { .refname = "branch",
					     .update_index = 1,
					     .value_type = REFTABLE_LOG_UPDATE,
					     .value = { .update = {
								.new_hash = h1,
								.old_hash = h2,
							} } };
	struct reftable_log_record dest = {
		.update_index = 0,
	};
	struct write_log_arg arg = {
		.log = &input,
		.update_index = 1,
	};

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	input.value.update.message = "one\ntwo";
	err = reftable_stack_add(st, &write_test_log, &arg);
	EXPECT(err == REFTABLE_API_ERROR);

	input.value.update.message = "one";
	err = reftable_stack_add(st, &write_test_log, &arg);
	EXPECT_ERR(err);

	err = reftable_stack_read_log(st, input.refname, &dest);
	EXPECT_ERR(err);
	EXPECT(0 == strcmp(dest.value.update.message, "one\n"));

	input.value.update.message = "two\n";
	arg.update_index = 2;
	err = reftable_stack_add(st, &write_test_log, &arg);
	EXPECT_ERR(err);
	err = reftable_stack_read_log(st, input.refname, &dest);
	EXPECT_ERR(err);
	EXPECT(0 == strcmp(dest.value.update.message, "two\n"));

	/* cleanup */
	reftable_stack_destroy(st);
	reftable_log_record_release(&dest);
	clear_dir(dir);
}

static void test_reftable_stack_tombstone(void)
{
	int i = 0;
	char *dir = get_tmp_dir(__LINE__);

	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	int err;
	struct reftable_ref_record refs[2] = { { NULL } };
	struct reftable_log_record logs[2] = { { NULL } };
	int N = ARRAY_SIZE(refs);
	struct reftable_ref_record dest = { NULL };
	struct reftable_log_record log_dest = { NULL };


	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	/* even entries add the refs, odd entries delete them. */
	for (i = 0; i < N; i++) {
		const char *buf = "branch";
		refs[i].refname = xstrdup(buf);
		refs[i].update_index = i + 1;
		if (i % 2 == 0) {
			refs[i].value_type = REFTABLE_REF_VAL1;
			set_test_hash(refs[i].value.val1, i);
		}

		logs[i].refname = xstrdup(buf);
		/* update_index is part of the key. */
		logs[i].update_index = 42;
		if (i % 2 == 0) {
			logs[i].value_type = REFTABLE_LOG_UPDATE;
			logs[i].value.update.new_hash =
				reftable_malloc(GIT_SHA1_RAWSZ);
			set_test_hash(logs[i].value.update.new_hash, i);
			logs[i].value.update.email =
				xstrdup("identity@invalid");
		}
	}
	for (i = 0; i < N; i++) {
		int err = reftable_stack_add(st, &write_test_ref, &refs[i]);
		EXPECT_ERR(err);
	}

	for (i = 0; i < N; i++) {
		struct write_log_arg arg = {
			.log = &logs[i],
			.update_index = reftable_stack_next_update_index(st),
		};
		int err = reftable_stack_add(st, &write_test_log, &arg);
		EXPECT_ERR(err);
	}

	err = reftable_stack_read_ref(st, "branch", &dest);
	EXPECT(err == 1);
	reftable_ref_record_release(&dest);

	err = reftable_stack_read_log(st, "branch", &log_dest);
	EXPECT(err == 1);
	reftable_log_record_release(&log_dest);

	err = reftable_stack_compact_all(st, NULL);
	EXPECT_ERR(err);

	err = reftable_stack_read_ref(st, "branch", &dest);
	EXPECT(err == 1);

	err = reftable_stack_read_log(st, "branch", &log_dest);
	EXPECT(err == 1);
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

static void test_reftable_stack_hash_id(void)
{
	char *dir = get_tmp_dir(__LINE__);

	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	int err;

	struct reftable_ref_record ref = {
		.refname = "master",
		.value_type = REFTABLE_REF_SYMREF,
		.value.symref = "target",
		.update_index = 1,
	};
	struct reftable_write_options cfg32 = { .hash_id = GIT_SHA256_FORMAT_ID };
	struct reftable_stack *st32 = NULL;
	struct reftable_write_options cfg_default = { 0 };
	struct reftable_stack *st_default = NULL;
	struct reftable_ref_record dest = { NULL };

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_add(st, &write_test_ref, &ref);
	EXPECT_ERR(err);

	/* can't read it with the wrong hash ID. */
	err = reftable_new_stack(&st32, dir, cfg32);
	EXPECT(err == REFTABLE_FORMAT_ERROR);

	/* check that we can read it back with default config too. */
	err = reftable_new_stack(&st_default, dir, cfg_default);
	EXPECT_ERR(err);

	err = reftable_stack_read_ref(st_default, "master", &dest);
	EXPECT_ERR(err);

	EXPECT(reftable_ref_record_equal(&ref, &dest, GIT_SHA1_RAWSZ));
	reftable_ref_record_release(&dest);
	reftable_stack_destroy(st);
	reftable_stack_destroy(st_default);
	clear_dir(dir);
}

static void test_log2(void)
{
	EXPECT(1 == fastlog2(3));
	EXPECT(2 == fastlog2(4));
	EXPECT(2 == fastlog2(5));
}

static void test_sizes_to_segments(void)
{
	uint64_t sizes[] = { 2, 3, 4, 5, 7, 9 };
	/* .................0  1  2  3  4  5 */

	size_t seglen = 0;
	struct segment *segs =
		sizes_to_segments(&seglen, sizes, ARRAY_SIZE(sizes));
	EXPECT(segs[2].log == 3);
	EXPECT(segs[2].start == 5);
	EXPECT(segs[2].end == 6);

	EXPECT(segs[1].log == 2);
	EXPECT(segs[1].start == 2);
	EXPECT(segs[1].end == 5);
	reftable_free(segs);
}

static void test_sizes_to_segments_empty(void)
{
	size_t seglen = 0;
	struct segment *segs = sizes_to_segments(&seglen, NULL, 0);
	EXPECT(seglen == 0);
	reftable_free(segs);
}

static void test_sizes_to_segments_all_equal(void)
{
	uint64_t sizes[] = { 5, 5 };
	size_t seglen = 0;
	struct segment *segs =
		sizes_to_segments(&seglen, sizes, ARRAY_SIZE(sizes));
	EXPECT(seglen == 1);
	EXPECT(segs[0].start == 0);
	EXPECT(segs[0].end == 2);
	reftable_free(segs);
}

static void test_suggest_compaction_segment(void)
{
	uint64_t sizes[] = { 128, 64, 17, 16, 9, 9, 9, 16, 16 };
	/* .................0    1    2  3   4  5  6 */
	struct segment min =
		suggest_compaction_segment(sizes, ARRAY_SIZE(sizes));
	EXPECT(min.start == 2);
	EXPECT(min.end == 7);
}

static void test_suggest_compaction_segment_nothing(void)
{
	uint64_t sizes[] = { 64, 32, 16, 8, 4, 2 };
	struct segment result =
		suggest_compaction_segment(sizes, ARRAY_SIZE(sizes));
	EXPECT(result.start == result.end);
}

static void test_reflog_expire(void)
{
	char *dir = get_tmp_dir(__LINE__);

	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	struct reftable_log_record logs[20] = { { NULL } };
	int N = ARRAY_SIZE(logs) - 1;
	int i = 0;
	int err;
	struct reftable_log_expiry_config expiry = {
		.time = 10,
	};
	struct reftable_log_record log = { NULL };


	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	for (i = 1; i <= N; i++) {
		char buf[256];
		snprintf(buf, sizeof(buf), "branch%02d", i);

		logs[i].refname = xstrdup(buf);
		logs[i].update_index = i;
		logs[i].value_type = REFTABLE_LOG_UPDATE;
		logs[i].value.update.time = i;
		logs[i].value.update.new_hash = reftable_malloc(GIT_SHA1_RAWSZ);
		logs[i].value.update.email = xstrdup("identity@invalid");
		set_test_hash(logs[i].value.update.new_hash, i);
	}

	for (i = 1; i <= N; i++) {
		struct write_log_arg arg = {
			.log = &logs[i],
			.update_index = reftable_stack_next_update_index(st),
		};
		int err = reftable_stack_add(st, &write_test_log, &arg);
		EXPECT_ERR(err);
	}

	err = reftable_stack_compact_all(st, NULL);
	EXPECT_ERR(err);

	err = reftable_stack_compact_all(st, &expiry);
	EXPECT_ERR(err);

	err = reftable_stack_read_log(st, logs[9].refname, &log);
	EXPECT(err == 1);

	err = reftable_stack_read_log(st, logs[11].refname, &log);
	EXPECT_ERR(err);

	expiry.min_update_index = 15;
	err = reftable_stack_compact_all(st, &expiry);
	EXPECT_ERR(err);

	err = reftable_stack_read_log(st, logs[14].refname, &log);
	EXPECT(err == 1);

	err = reftable_stack_read_log(st, logs[16].refname, &log);
	EXPECT_ERR(err);

	/* cleanup */
	reftable_stack_destroy(st);
	for (i = 0; i <= N; i++) {
		reftable_log_record_release(&logs[i]);
	}
	clear_dir(dir);
	reftable_log_record_release(&log);
}

static int write_nothing(struct reftable_writer *wr, void *arg)
{
	reftable_writer_set_limits(wr, 1, 1);
	return 0;
}

static void test_empty_add(void)
{
	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	int err;
	char *dir = get_tmp_dir(__LINE__);

	struct reftable_stack *st2 = NULL;


	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_add(st, &write_nothing, NULL);
	EXPECT_ERR(err);

	err = reftable_new_stack(&st2, dir, cfg);
	EXPECT_ERR(err);
	clear_dir(dir);
	reftable_stack_destroy(st);
	reftable_stack_destroy(st2);
}

static void test_reftable_stack_auto_compaction(void)
{
	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	char *dir = get_tmp_dir(__LINE__);

	int err, i;
	int N = 100;

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	st->disable_auto_compact = 1; /* call manually below for coverage. */
	for (i = 0; i < N; i++) {
		char name[100];
		struct reftable_ref_record ref = {
			.refname = name,
			.update_index = reftable_stack_next_update_index(st),
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = "master",
		};
		snprintf(name, sizeof(name), "branch%04d", i);

		err = reftable_stack_add(st, &write_test_ref, &ref);
		EXPECT_ERR(err);

		err = reftable_stack_auto_compact(st);
		EXPECT_ERR(err);
		EXPECT(i < 3 || st->merged->stack_len < 2 * fastlog2(i));
	}

	EXPECT(reftable_stack_compaction_stats(st)->entries_written <
	       (uint64_t)(N * fastlog2(N)));

	reftable_stack_destroy(st);
	clear_dir(dir);
}

static void test_reftable_stack_add_performs_auto_compaction(void)
{
	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st = NULL;
	struct strbuf refname = STRBUF_INIT;
	char *dir = get_tmp_dir(__LINE__);
	int err, i, n = 20;

	err = reftable_new_stack(&st, dir, cfg);
	EXPECT_ERR(err);

	for (i = 0; i <= n; i++) {
		struct reftable_ref_record ref = {
			.update_index = reftable_stack_next_update_index(st),
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = "master",
		};

		/*
		 * Disable auto-compaction for all but the last runs. Like this
		 * we can ensure that we indeed honor this setting and have
		 * better control over when exactly auto compaction runs.
		 */
		st->disable_auto_compact = i != n;

		strbuf_reset(&refname);
		strbuf_addf(&refname, "branch-%04d", i);
		ref.refname = refname.buf;

		err = reftable_stack_add(st, &write_test_ref, &ref);
		EXPECT_ERR(err);

		/*
		 * The stack length should grow continuously for all runs where
		 * auto compaction is disabled. When enabled, we should merge
		 * all tables in the stack.
		 */
		if (i != n)
			EXPECT(st->merged->stack_len == i + 1);
		else
			EXPECT(st->merged->stack_len == 1);
	}

	reftable_stack_destroy(st);
	strbuf_release(&refname);
	clear_dir(dir);
}

static void test_reftable_stack_compaction_concurrent(void)
{
	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st1 = NULL, *st2 = NULL;
	char *dir = get_tmp_dir(__LINE__);

	int err, i;
	int N = 3;

	err = reftable_new_stack(&st1, dir, cfg);
	EXPECT_ERR(err);

	for (i = 0; i < N; i++) {
		char name[100];
		struct reftable_ref_record ref = {
			.refname = name,
			.update_index = reftable_stack_next_update_index(st1),
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = "master",
		};
		snprintf(name, sizeof(name), "branch%04d", i);

		err = reftable_stack_add(st1, &write_test_ref, &ref);
		EXPECT_ERR(err);
	}

	err = reftable_new_stack(&st2, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_compact_all(st1, NULL);
	EXPECT_ERR(err);

	reftable_stack_destroy(st1);
	reftable_stack_destroy(st2);

	EXPECT(count_dir_entries(dir) == 2);
	clear_dir(dir);
}

static void unclean_stack_close(struct reftable_stack *st)
{
	/* break abstraction boundary to simulate unclean shutdown. */
	int i = 0;
	for (; i < st->readers_len; i++) {
		reftable_reader_free(st->readers[i]);
	}
	st->readers_len = 0;
	FREE_AND_NULL(st->readers);
}

static void test_reftable_stack_compaction_concurrent_clean(void)
{
	struct reftable_write_options cfg = { 0 };
	struct reftable_stack *st1 = NULL, *st2 = NULL, *st3 = NULL;
	char *dir = get_tmp_dir(__LINE__);

	int err, i;
	int N = 3;

	err = reftable_new_stack(&st1, dir, cfg);
	EXPECT_ERR(err);

	for (i = 0; i < N; i++) {
		char name[100];
		struct reftable_ref_record ref = {
			.refname = name,
			.update_index = reftable_stack_next_update_index(st1),
			.value_type = REFTABLE_REF_SYMREF,
			.value.symref = "master",
		};
		snprintf(name, sizeof(name), "branch%04d", i);

		err = reftable_stack_add(st1, &write_test_ref, &ref);
		EXPECT_ERR(err);
	}

	err = reftable_new_stack(&st2, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_compact_all(st1, NULL);
	EXPECT_ERR(err);

	unclean_stack_close(st1);
	unclean_stack_close(st2);

	err = reftable_new_stack(&st3, dir, cfg);
	EXPECT_ERR(err);

	err = reftable_stack_clean(st3);
	EXPECT_ERR(err);
	EXPECT(count_dir_entries(dir) == 2);

	reftable_stack_destroy(st1);
	reftable_stack_destroy(st2);
	reftable_stack_destroy(st3);

	clear_dir(dir);
}

int stack_test_main(int argc, const char *argv[])
{
	RUN_TEST(test_empty_add);
	RUN_TEST(test_log2);
	RUN_TEST(test_names_equal);
	RUN_TEST(test_parse_names);
	RUN_TEST(test_read_file);
	RUN_TEST(test_reflog_expire);
	RUN_TEST(test_reftable_stack_add);
	RUN_TEST(test_reftable_stack_add_one);
	RUN_TEST(test_reftable_stack_auto_compaction);
	RUN_TEST(test_reftable_stack_add_performs_auto_compaction);
	RUN_TEST(test_reftable_stack_compaction_concurrent);
	RUN_TEST(test_reftable_stack_compaction_concurrent_clean);
	RUN_TEST(test_reftable_stack_hash_id);
	RUN_TEST(test_reftable_stack_lock_failure);
	RUN_TEST(test_reftable_stack_log_normalize);
	RUN_TEST(test_reftable_stack_tombstone);
	RUN_TEST(test_reftable_stack_transaction_api);
	RUN_TEST(test_reftable_stack_transaction_api_performs_auto_compaction);
	RUN_TEST(test_reftable_stack_update_index_check);
	RUN_TEST(test_reftable_stack_uptodate);
	RUN_TEST(test_reftable_stack_validate_refname);
	RUN_TEST(test_sizes_to_segments);
	RUN_TEST(test_sizes_to_segments_all_equal);
	RUN_TEST(test_sizes_to_segments_empty);
	RUN_TEST(test_suggest_compaction_segment);
	RUN_TEST(test_suggest_compaction_segment_nothing);
	return 0;
}
