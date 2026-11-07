/*
 * test-run-command.c: test run command API.
 *
 * (C) 2009 Ilari Liusvaara <ilari.liusvaara@elisanet.fi>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "test-tool.h"
#include "run-command.h"
#include "strvec.h"
#include "strbuf.h"
#include "parse-options.h"

static int number_callbacks;
static int parallel_next(struct child_process *cp,
			 struct strbuf *err,
			 void *cb,
			 void **task_cb)
{
	struct child_process *d = cb;
	if (number_callbacks >= 4)
		return 0;

	strvec_pushv(&cp->args, d->args.v);
	cp->in = d->in;
	cp->no_stdin = d->no_stdin;
	if (err)
		strbuf_addstr(err, "preloaded output of a child\n");
	else
		fprintf(stderr, "preloaded output of a child\n");

	number_callbacks++;

	/* test_stdin callback will use this to count remaining lines */
	*task_cb = xmalloc(sizeof(int));
	*(int*)(*task_cb) = 2;

	return 1;
}

static int no_job(struct child_process *cp UNUSED,
		  struct strbuf *err,
		  void *cb UNUSED,
		  void **task_cb UNUSED)
{
	if (err)
		strbuf_addstr(err, "no further jobs available\n");
	else
		fprintf(stderr, "no further jobs available\n");
	return 0;
}

static int task_finished(int result UNUSED,
			 struct strbuf *err,
			 void *pp_cb UNUSED,
			 void *pp_task_cb)
{
	if (err)
		strbuf_addstr(err, "asking for a quick stop\n");
	else
		fprintf(stderr, "asking for a quick stop\n");

	FREE_AND_NULL(pp_task_cb);

	return 1;
}

static int task_finished_quiet(int result UNUSED,
			       struct strbuf *err UNUSED,
			       void *pp_cb UNUSED,
			       void *pp_task_cb)
{
	FREE_AND_NULL(pp_task_cb);
	return 0;
}

static int test_stdin_pipe_feed(int hook_stdin_fd, void *cb UNUSED, void *task_cb)
{
	int *lines_remaining = task_cb;

	if (*lines_remaining) {
		struct strbuf buf = STRBUF_INIT;
		strbuf_addf(&buf, "sample stdin %d\n", --(*lines_remaining));
		if (write_in_full(hook_stdin_fd, buf.buf, buf.len) < 0) {
			if (errno == EPIPE) {
				/* child closed stdin, nothing more to do */
				strbuf_release(&buf);
				return 1;
			}
			die_errno("write");
		}
		strbuf_release(&buf);
	}

	return !(*lines_remaining);
}

static uint64_t my_random_next = 1234;

static uint64_t my_random(void)
{
	uint64_t res = my_random_next;
	my_random_next = my_random_next * 1103515245 + 12345;
	return res;
}

static int quote_stress_test(int argc, const char **argv)
{
	/*
	 * We are running a quote-stress test.
	 * spawn a subprocess that runs quote-stress with a
	 * special option that echoes back the arguments that
	 * were passed in.
	 */
	char special[] = ".?*\\^_\"'`{}()[]<>@~&+:;$%"; // \t\r\n\a";
	int i, j, k, trials = 100, skip = 0, msys2 = 0;
	struct strbuf out = STRBUF_INIT;
	struct strvec args = STRVEC_INIT;
	struct option options[] = {
		OPT_INTEGER('n', "trials", &trials, "number of trials"),
		OPT_INTEGER('s', "skip", &skip, "skip <n> trials"),
		OPT_BOOL('m', "msys2", &msys2, "test quoting for MSYS2's sh"),
		OPT_END()
	};
	const char * const usage[] = {
		"test-tool run-command quote-stress-test <options>",
		NULL
	};

	argc = parse_options(argc, argv, NULL, options, usage, 0);

	setenv("MSYS_NO_PATHCONV", "1", 0);

	for (i = 0; i < trials; i++) {
		struct child_process cp = CHILD_PROCESS_INIT;
		size_t arg_count, arg_offset;
		int ret = 0;

		strvec_clear(&args);
		if (msys2)
			strvec_pushl(&args, "sh", "-c",
				     "printf %s\\\\0 \"$@\"", "skip", NULL);
		else
			strvec_pushl(&args, "test-tool", "run-command",
				     "quote-echo", NULL);
		arg_offset = args.nr;

		if (argc > 0) {
			trials = 1;
			arg_count = argc;
			for (j = 0; j < arg_count; j++)
				strvec_push(&args, argv[j]);
		} else {
			arg_count = 1 + (my_random() % 5);
			for (j = 0; j < arg_count; j++) {
				char buf[20];
				size_t min_len = 1;
				size_t arg_len = min_len +
					(my_random() % (ARRAY_SIZE(buf) - min_len));

				for (k = 0; k < arg_len; k++)
					buf[k] = special[my_random() %
						ARRAY_SIZE(special)];
				buf[arg_len] = '\0';

				strvec_push(&args, buf);
			}
		}

		if (i < skip)
			continue;

		strvec_pushv(&cp.args, args.v);
		strbuf_reset(&out);
		if (pipe_command(&cp, NULL, 0, &out, 0, NULL, 0) < 0)
			return error("Failed to spawn child process");

		for (j = 0, k = 0; j < arg_count; j++) {
			const char *arg = args.v[j + arg_offset];

			if (strcmp(arg, out.buf + k))
				ret = error("incorrectly quoted arg: '%s', "
					    "echoed back as '%s'",
					     arg, out.buf + k);
			k += strlen(out.buf + k) + 1;
		}

		if (k != out.len)
			ret = error("got %d bytes, but consumed only %d",
				     (int)out.len, (int)k);

		if (ret) {
			fprintf(stderr, "Trial #%d failed. Arguments:\n", i);
			for (j = 0; j < arg_count; j++)
				fprintf(stderr, "arg #%d: '%s'\n",
					(int)j, args.v[j + arg_offset]);

			strbuf_release(&out);
			strvec_clear(&args);

			return ret;
		}

		if (i && (i % 100) == 0)
			fprintf(stderr, "Trials completed: %d\n", (int)i);
	}

	strbuf_release(&out);
	strvec_clear(&args);

	return 0;
}

static int quote_echo(int argc, const char **argv)
{
	while (argc > 1) {
		fwrite(argv[1], strlen(argv[1]), 1, stdout);
		fputc('\0', stdout);
		argv++;
		argc--;
	}

	return 0;
}

static int inherit_handle(const char *argv0)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	char path[PATH_MAX];
	int tmp;

	/* First, open an inheritable handle */
	xsnprintf(path, sizeof(path), "out-XXXXXX");
	tmp = xmkstemp(path);

	strvec_pushl(&cp.args,
		     "test-tool", argv0, "inherited-handle-child", NULL);
	cp.in = -1;
	cp.no_stdout = cp.no_stderr = 1;
	if (start_command(&cp) < 0)
		die("Could not start child process");

	/* Then close it, and try to delete it. */
	close(tmp);
	if (unlink(path))
		die("Could not delete '%s'", path);

	if (close(cp.in) < 0 || finish_command(&cp) < 0)
		die("Child did not finish");

	return 0;
}

static int inherit_handle_child(void)
{
	struct strbuf buf = STRBUF_INIT;

	if (strbuf_read(&buf, 0, 0) < 0)
		die("Could not read stdin");
	printf("Received %s\n", buf.buf);
	strbuf_release(&buf);

	return 0;
}

int cmd__run_command(int argc, const char **argv)
{
	struct child_process proc = CHILD_PROCESS_INIT;
	int jobs;
	int ret;
	struct run_process_parallel_opts opts = {
		.data = &proc,
	};

	if (!strcmp(argv[1], "inherited-handle"))
		return inherit_handle(argv[0]);
	if (!strcmp(argv[1], "inherited-handle-child"))
		return inherit_handle_child();

	if (argc >= 2 && !strcmp(argv[1], "quote-stress-test"))
		return !!quote_stress_test(argc - 1, argv + 1);

	if (argc >= 2 && !strcmp(argv[1], "quote-echo"))
		return !!quote_echo(argc - 1, argv + 1);

	if (argc < 3)
		return 1;
	while (!strcmp(argv[1], "env")) {
		if (!argv[2])
			die("env specifier without a value");
		strvec_push(&proc.env, argv[2]);
		argv += 2;
		argc -= 2;
	}
	if (argc < 3) {
		ret = 1;
		goto cleanup;
	}
	strvec_pushv(&proc.args, (const char **)argv + 2);

	if (!strcmp(argv[1], "start-command-ENOENT")) {
		if (start_command(&proc) < 0 && errno == ENOENT) {
			ret = 0;
			goto cleanup;
		}
		fprintf(stderr, "FAIL %s\n", argv[1]);
		return 1;
	}
	if (!strcmp(argv[1], "run-command")) {
		ret = run_command(&proc);
		goto cleanup;
	}

	if (!strcmp(argv[1], "--ungroup")) {
		argv += 1;
		argc -= 1;
		opts.ungroup = 1;
	}

	jobs = atoi(argv[2]);
	strvec_clear(&proc.args);
	strvec_pushv(&proc.args, (const char **)argv + 3);

	if (!strcmp(argv[1], "run-command-parallel")) {
		opts.get_next_task = parallel_next;
		opts.task_finished = task_finished_quiet;
	} else if (!strcmp(argv[1], "run-command-abort")) {
		opts.get_next_task = parallel_next;
		opts.task_finished = task_finished;
	} else if (!strcmp(argv[1], "run-command-no-jobs")) {
		opts.get_next_task = no_job;
		opts.task_finished = task_finished;
	} else if (!strcmp(argv[1], "run-command-stdin")) {
		proc.in = -1;
		proc.no_stdin = 0;
		opts.get_next_task = parallel_next;
		opts.task_finished = task_finished_quiet;
		opts.feed_pipe = test_stdin_pipe_feed;
	} else {
		ret = 1;
		fprintf(stderr, "check usage\n");
		goto cleanup;
	}
	opts.processes = jobs;
	run_processes_parallel(&opts);
	ret = 0;
cleanup:
	child_process_clear(&proc);
	return ret;
}
