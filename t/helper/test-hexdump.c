#include "test-tool.h"
#include "git-compat-util.h"

/*
 * Read stdin and print a hexdump to stdout.
 */
int cmd__hexdump(int argc UNUSED, const char **argv UNUSED)
{
	char buf[1024];
	ssize_t i, len;
	int have_data = 0;

	for (;;) {
		len = xread(0, buf, sizeof(buf));
		if (len < 0)
			die_errno("failure reading stdin");
		if (!len)
			break;

		have_data = 1;

		for (i = 0; i < len; i++)
			printf("%02x ", (unsigned char)buf[i]);
	}

	if (have_data)
		putchar('\n');

	return 0;
}
