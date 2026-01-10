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

static char *execute(const char *suite, int expected_error_code, const char **args, size_t nargs)
{
	SECURITY_ATTRIBUTES security_attributes = { 0 };
	PROCESS_INFORMATION process_info = { 0 };
	STARTUPINFO startup_info = { 0 };
	char binary_path[4096] = { 0 };
	char cmdline[4096] = { 0 };
	char *output = NULL;
	HANDLE stdout_write;
	HANDLE stdout_read;
	DWORD exit_code;
	size_t i;

	snprintf(binary_path, sizeof(binary_path), "%s/%s_suite.exe",
		 selftest_suite_directory, suite);

	/*
	 * Assemble command line arguments. In theory we'd have to properly
	 * quote them. In practice none of our tests actually care.
	 */
	snprintf(cmdline, sizeof(cmdline), suite);
	for (i = 0; i < nargs; i++) {
		size_t cmdline_len = strlen(cmdline);
		const char *arg = args[i];
		cl_assert(cmdline_len + strlen(arg) < sizeof(cmdline));
		snprintf(cmdline + cmdline_len, sizeof(cmdline) - cmdline_len,
			 " %s", arg);
	}

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
	cl_assert_equal_b(1, CreateProcess(binary_path, cmdline, NULL, NULL, TRUE,
					   0, NULL, NULL, &startup_info, &process_info));
	cl_assert_equal_b(1, CloseHandle(stdout_write));

	output = read_full(stdout_read, 1);
	cl_assert_equal_b(1, CloseHandle(stdout_read));
	cl_assert_equal_b(1, GetExitCodeProcess(process_info.hProcess, &exit_code));
	cl_assert_equal_i(exit_code, expected_error_code);

	return output;
}

static void assert_output(const char *suite, const char *expected_output_file, int expected_error_code, ...)
{
	char *expected_output = NULL;
	char *output = NULL;
	const char *args[16];
	va_list ap;
	size_t i;

	va_start(ap, expected_error_code);
	for (i = 0; ; i++) {
		const char *arg = va_arg(ap, const char *);
		if (!arg)
			break;
		cl_assert(i < sizeof(args) / sizeof(*args));
		args[i] = arg;
	}
	va_end(ap);

	output = execute(suite, expected_error_code, args, i);
	expected_output = read_file(cl_fixture(expected_output_file));
	cl_assert_equal_s(output, expected_output);

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

static char *execute(const char *suite, int expected_error_code, const char **args, size_t nargs)
{
	int pipe_fds[2];
	pid_t pid;

	cl_must_pass(pipe(pipe_fds));

	pid = fork();
	if (!pid) {
		const char *final_args[17] = { NULL };
		char binary_path[4096];
		size_t len = 0;
		size_t i;

		cl_assert(nargs < sizeof(final_args) / sizeof(*final_args));
		final_args[0] = suite;
		for (i = 0; i < nargs; i++)
			final_args[i + 1] = args[i];

		if (dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
		    dup2(pipe_fds[1], STDERR_FILENO) < 0 ||
		    close(0) < 0 ||
		    close(pipe_fds[0]) < 0 ||
		    close(pipe_fds[1]) < 0)
			exit(1);

		cl_assert(len + strlen(selftest_suite_directory) < sizeof(binary_path));
		strcpy(binary_path, selftest_suite_directory);
		len += strlen(selftest_suite_directory);

		cl_assert(len + 1 < sizeof(binary_path));
		binary_path[len] = '/';
		len += 1;

		cl_assert(len + strlen(suite) < sizeof(binary_path));
		strcpy(binary_path + len, suite);
		len += strlen(suite);

		cl_assert(len + strlen("_suite") < sizeof(binary_path));
		strcpy(binary_path + len, "_suite");
		len += strlen("_suite");

		binary_path[len] = '\0';

		execv(binary_path, (char **) final_args);
		exit(1);
	} else if (pid > 0) {
		pid_t waited_pid;
		char *output;
		int stat;

		cl_must_pass(close(pipe_fds[1]));

		output = read_full(pipe_fds[0]);

		waited_pid = waitpid(pid, &stat, 0);
		cl_assert_equal_i(pid, waited_pid);
		cl_assert(WIFEXITED(stat));
		cl_assert_equal_i(WEXITSTATUS(stat), expected_error_code);

		return output;
	} else {
		cl_fail("Fork failed.");
	}

	return NULL;
}

static void assert_output(const char *suite, const char *expected_output_file, int expected_error_code, ...)
{
	char *expected_output, *output;
	const char *args[16];
	va_list ap;
	size_t i;

	va_start(ap, expected_error_code);
	for (i = 0; ; i++) {
		cl_assert(i < sizeof(args) / sizeof(*args));
		args[i] = va_arg(ap, const char *);
		if (!args[i])
			break;
	}
	va_end(ap);

	output = execute(suite, expected_error_code, args, i);
	expected_output = read_file(cl_fixture(expected_output_file));
	cl_assert_equal_s(output, expected_output);

	free(expected_output);
	free(output);
}
#endif

void test_selftest__help(void)
{
	cl_invoke(assert_output("combined", "help", 1, "-h", NULL));
}

void test_selftest__without_arguments(void)
{
	cl_invoke(assert_output("combined", "without_arguments", 15, NULL));
}

void test_selftest__specific_test(void)
{
	cl_invoke(assert_output("combined", "specific_test", 1, "-scombined::bool", NULL));
}

void test_selftest__stop_on_failure(void)
{
	cl_invoke(assert_output("combined", "stop_on_failure", 1, "-Q", NULL));
}

void test_selftest__quiet(void)
{
	cl_invoke(assert_output("combined", "quiet", 15, "-q", NULL));
}

void test_selftest__tap(void)
{
	cl_invoke(assert_output("combined", "tap", 15, "-t", NULL));
}

void test_selftest__suite_names(void)
{
	cl_invoke(assert_output("combined", "suite_names", 0, "-l", NULL));
}

void test_selftest__summary_without_filename(void)
{
	struct stat st;
	cl_invoke(assert_output("combined", "summary_without_filename", 15, "-r", NULL));
	/* The summary contains timestamps, so we cannot verify its contents. */
	cl_must_pass(stat("summary.xml", &st));
}

void test_selftest__summary_with_filename(void)
{
	struct stat st;
	cl_invoke(assert_output("combined", "summary_with_filename", 15, "-rdifferent.xml", NULL));
	/* The summary contains timestamps, so we cannot verify its contents. */
	cl_must_pass(stat("different.xml", &st));
}

void test_selftest__pointer_equal(void)
{
	const char *args[] = {
		"-spointer::equal",
		"-t"
	};
	char *output = execute("pointer", 0, args, 2);
	cl_assert_equal_s(output,
		   "TAP version 13\n"
		   "# start of suite 1: pointer\n"
		   "ok 1 - pointer::equal\n"
		   "1..1\n"
	);
	free(output);
}

void test_selftest__pointer_unequal(void)
{
	const char *args[] = {
		"-spointer::unequal",
	};
	char *output = execute("pointer", 1, args, 1);
	cl_assert(output);
	cl_assert(strstr(output, "Pointer mismatch: "));
	free(output);
}
