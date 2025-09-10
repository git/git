#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "selftest.h"

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>

static char *read_full(HANDLE h, int is_pipe)
{
	char *data = NULL;
	size_t data_size = 0;

	while (1) {
		CHAR buf[4096];
		DWORD bytes_read;

		if (!ReadFile(h, buf, sizeof(buf), &bytes_read, NULL)) {
			if (!is_pipe)
				cl_fail("Failed reading file handle.");
			cl_assert_equal_i(GetLastError(), ERROR_BROKEN_PIPE);
			break;
		}
		if (!bytes_read)
			break;

		data = realloc(data, data_size + bytes_read);
		cl_assert(data);
		memcpy(data + data_size, buf, bytes_read);
		data_size += bytes_read;
	}

	data = realloc(data, data_size + 1);
	cl_assert(data);
	data[data_size] = '\0';

	while (strstr(data, "\r\n")) {
		char *ptr = strstr(data, "\r\n");
		memmove(ptr, ptr + 1, strlen(ptr));
	}

	return data;
}

static char *read_file(const char *path)
{
	char *content;
	HANDLE file;

	file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
			  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	cl_assert(file != INVALID_HANDLE_VALUE);
	content = read_full(file, 0);
	cl_assert_equal_b(1, CloseHandle(file));

	return content;
}

static void run(const char *expected_output_file, int expected_error_code, ...)
{
	SECURITY_ATTRIBUTES security_attributes = { 0 };
	PROCESS_INFORMATION process_info = { 0 };
	STARTUPINFO startup_info = { 0 };
	char cmdline[4096] = { 0 };
	char *expected_output = NULL;
	char *output = NULL;
	HANDLE stdout_write;
	HANDLE stdout_read;
	DWORD exit_code;
	va_list ap;

	/*
	 * Assemble command line arguments. In theory we'd have to properly
	 * quote them. In practice none of our tests actually care.
	 */
	va_start(ap, expected_error_code);
	snprintf(cmdline, sizeof(cmdline), "selftest");
	while (1) {
		size_t cmdline_len = strlen(cmdline);
		const char *arg;

		arg = va_arg(ap, const char *);
		if (!arg)
			break;

		cl_assert(cmdline_len + strlen(arg) < sizeof(cmdline));
		snprintf(cmdline + cmdline_len, sizeof(cmdline) - cmdline_len,
			 " %s", arg);
	}
	va_end(ap);

	/*
	 * Create a pipe that we will use to read data from the child process.
	 * The writing side needs to be inheritable such that the child can use
	 * it as stdout and stderr. The reading side should only be used by the
	 * parent.
	 */
	security_attributes.nLength = sizeof(security_attributes);
	security_attributes.bInheritHandle = TRUE;
	cl_assert_equal_b(1, CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0));
	cl_assert_equal_b(1, SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0));

	/*
	 * Create the child process with our pipe.
	 */
	startup_info.cb = sizeof(startup_info);
	startup_info.hStdError = stdout_write;
	startup_info.hStdOutput = stdout_write;
	startup_info.dwFlags |= STARTF_USESTDHANDLES;
	cl_assert_equal_b(1, CreateProcess(selftest_binary_path, cmdline, NULL, NULL, TRUE,
					   0, NULL, NULL, &startup_info, &process_info));
	cl_assert_equal_b(1, CloseHandle(stdout_write));

	output = read_full(stdout_read, 1);
	cl_assert_equal_b(1, CloseHandle(stdout_read));
	cl_assert_equal_b(1, GetExitCodeProcess(process_info.hProcess, &exit_code));

	expected_output = read_file(cl_fixture(expected_output_file));
	cl_assert_equal_s(output, expected_output);
	cl_assert_equal_i(exit_code, expected_error_code);

	free(expected_output);
	free(output);
}

#else
# include <errno.h>
# include <fcntl.h>
# include <limits.h>
# include <unistd.h>
# include <sys/wait.h>

static char *read_full(int fd)
{
	size_t data_bytes = 0;
	char *data = NULL;

	while (1) {
		char buf[4096];
		ssize_t n;

		n = read(fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			cl_fail("Failed reading from child process.");
		}
		if (!n)
			break;

		data = realloc(data, data_bytes + n);
		cl_assert(data);

		memcpy(data + data_bytes, buf, n);
		data_bytes += n;
	}

	data = realloc(data, data_bytes + 1);
	cl_assert(data);
	data[data_bytes] = '\0';

	return data;
}

static char *read_file(const char *path)
{
	char *data;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		cl_fail("Failed reading expected file.");

	data = read_full(fd);
	cl_must_pass(close(fd));

	return data;
}

static void run(const char *expected_output_file, int expected_error_code, ...)
{
	const char *argv[16];
	int pipe_fds[2];
	va_list ap;
	pid_t pid;
	int i;

	va_start(ap, expected_error_code);
	argv[0] = "selftest";
	for (i = 1; ; i++) {
		cl_assert(i < sizeof(argv) / sizeof(*argv));

		argv[i] = va_arg(ap, const char *);
		if (!argv[i])
			break;
	}
	va_end(ap);

	cl_must_pass(pipe(pipe_fds));

	pid = fork();
	if (!pid) {
		if (dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
		    dup2(pipe_fds[1], STDERR_FILENO) < 0 ||
		    close(0) < 0 ||
		    close(pipe_fds[0]) < 0 ||
		    close(pipe_fds[1]) < 0)
			exit(1);

		execv(selftest_binary_path, (char **) argv);
		exit(1);
	} else if (pid > 0) {
		pid_t waited_pid;
		char *expected_output, *output;
		int stat;

		cl_must_pass(close(pipe_fds[1]));

		output = read_full(pipe_fds[0]);

		waited_pid = waitpid(pid, &stat, 0);
		cl_assert_equal_i(pid, waited_pid);
		cl_assert(WIFEXITED(stat));
		cl_assert_equal_i(WEXITSTATUS(stat), expected_error_code);

		expected_output = read_file(cl_fixture(expected_output_file));
		cl_assert_equal_s(output, expected_output);

		free(expected_output);
		free(output);
	} else {
		cl_fail("Fork failed.");
	}
}
#endif

void test_selftest__help(void)
{
	cl_invoke(run("help", 1, "-h", NULL));
}

void test_selftest__without_arguments(void)
{
	cl_invoke(run("without_arguments", 10, NULL));
}

void test_selftest__specific_test(void)
{
	cl_invoke(run("specific_test", 1, "-sselftest::suite::bool", NULL));
}

void test_selftest__stop_on_failure(void)
{
	cl_invoke(run("stop_on_failure", 1, "-Q", NULL));
}

void test_selftest__quiet(void)
{
	cl_invoke(run("quiet", 10, "-q", NULL));
}

void test_selftest__tap(void)
{
	cl_invoke(run("tap", 10, "-t", NULL));
}

void test_selftest__suite_names(void)
{
	cl_invoke(run("suite_names", 0, "-l", NULL));
}

void test_selftest__summary_without_filename(void)
{
	struct stat st;
	cl_invoke(run("summary_without_filename", 10, "-r", NULL));
	/* The summary contains timestamps, so we cannot verify its contents. */
	cl_must_pass(stat("summary.xml", &st));
}

void test_selftest__summary_with_filename(void)
{
	struct stat st;
	cl_invoke(run("summary_with_filename", 10, "-rdifferent.xml", NULL));
	/* The summary contains timestamps, so we cannot verify its contents. */
	cl_must_pass(stat("different.xml", &st));
}
