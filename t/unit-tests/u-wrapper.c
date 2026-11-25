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
