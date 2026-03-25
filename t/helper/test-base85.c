#include "test-tool.h"
#include "base85.h"

#define MAX_INPUT_BUF BASE85_CHUNK_SIZE * 1024

static const char *base85_usage = "test-tool base85 chunk-encode";

static void print_binary_diff_line(char line[71], int len, void *output_data UNUSED)
{
	fwrite(line, 1, len, stdout);
}

static void base85_chunk(void)
{
	unsigned char *input = xmalloc(MAX_INPUT_BUF);

	for (;;) {
		ssize_t len = read_in_full(0, input, MAX_INPUT_BUF);
		if (len < 0)
			die_errno("read error");

		chunk_and_encode85(input, len, print_binary_diff_line, NULL);
		if (len < MAX_INPUT_BUF)
			break;
	}

	putc('\n', stdout);
	free(input);
}

int cmd__base85(int argc, const char **argv)
{
	if (argc != 2)
		usage(base85_usage);

	if (!strcmp(argv[1], "chunk-encode")) {
		base85_chunk();
	} else {
		error("unknown subcommand: %s", argv[1]);
		usage(base85_usage);
	}

	return 0;
}
