#include "unit-test.h"
#include "tempfile.h"

static struct tempfile *file;
static char data[128 * 1024], actual[sizeof(data)];

void test_wrapper__initialize(void)
{
	file = mks_tempfile("wrapper-test-XXXXXX");
	cl_assert(file != NULL);
	for (size_t i = 0; i < sizeof(data); i++)
		data[i] = i % 127;
	memset(actual, 0, sizeof(actual));
}

void test_wrapper__cleanup(void)
{
	delete_tempfile(&file);
}

void test_wrapper__write_in_full(void)
{
	int fd = get_tempfile_fd(file);

	cl_assert_equal_i(write_in_full(fd, data, sizeof(data)), 0);
	cl_assert(lseek(fd, 0, SEEK_SET) == 0);
	cl_assert(read_in_full(fd, actual, sizeof(actual)) ==
		  (ssize_t)sizeof(actual));
	cl_assert(!memcmp(data, actual, sizeof(data)));
}

void test_wrapper__write_in_full_empty(void)
{
	cl_assert_equal_i(write_in_full(-1, NULL, 0), 0);
}

void test_wrapper__write_in_full_error(void)
{
	errno = 0;
	cl_assert_equal_i(write_in_full(-1, data, 1), -1);
	cl_assert_equal_i(errno, EBADF);
}

void test_wrapper__read_in_full_or_error(void)
{
	int fd = get_tempfile_fd(file);

	cl_assert_equal_i(write_in_full(fd, data, sizeof(data)), 0);
	cl_assert(lseek(fd, 0, SEEK_SET) == 0);
	cl_assert_equal_i(read_in_full_or_error(fd, actual, sizeof(actual)), 0);
	cl_assert(!memcmp(data, actual, sizeof(data)));
}

void test_wrapper__read_in_full_or_error_empty(void)
{
	cl_assert_equal_i(read_in_full_or_error(-1, NULL, 0), 0);
}

void test_wrapper__read_in_full_or_error_short(void)
{
	int fd = get_tempfile_fd(file);

	cl_assert_equal_i(write_in_full(fd, "abc", 3), 0);
	cl_assert(lseek(fd, 0, SEEK_SET) == 0);
	errno = 0;
	cl_assert_equal_i(read_in_full_or_error(fd, actual, sizeof(actual)),
			  -1);
	cl_assert_equal_i(errno, EIO);
	cl_assert(!memcmp(actual, "abc", 3));
}

void test_wrapper__read_in_full_or_error_bad_fd(void)
{
	errno = 0;
	cl_assert_equal_i(read_in_full_or_error(-1, actual, 1), -1);
	cl_assert_equal_i(errno, EBADF);
}

void test_wrapper__read_in_full_short(void)
{
	int fd = get_tempfile_fd(file);

	cl_assert_equal_i(write_in_full(fd, "abc", 3), 0);
	cl_assert(lseek(fd, 0, SEEK_SET) == 0);
	cl_assert_equal_i(read_in_full(fd, actual, sizeof(actual)), 3);
	cl_assert(!memcmp(actual, "abc", 3));
}

void test_wrapper__read_in_full_empty(void)
{
	cl_assert_equal_i(read_in_full(-1, NULL, 0), 0);
}

void test_wrapper__read_in_full_error(void)
{
	errno = 0;
	cl_assert_equal_i(read_in_full(-1, actual, 1), -1);
	cl_assert_equal_i(errno, EBADF);
}
