#include "test-tool.h"
#include "git-compat-util.h"


int cmd__csprng(int argc, const char **argv)
{
	unsigned long count;
	unsigned char buf[1024];

	if (argc > 2) {
		fprintf(stderr, "usage: %s [<size>]\n", argv[0]);
		return 2;
	}

	count = (argc == 2) ? strtoul(argv[1], NULL, 0) : -1L;

	while (count) {
		unsigned long chunk = count < sizeof(buf) ? count : sizeof(buf);
		if (csprng_bytes(buf, chunk) < 0) {
			perror("failed to read");
			return 5;
		}
		if (fwrite(buf, chunk, 1, stdout) != chunk)
			return 1;
		count -= chunk;
	}

	return 0;
}
