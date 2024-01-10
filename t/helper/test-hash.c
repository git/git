#include "test-tool.h"
#include "hex.h"

int cmd_hash_impl(int ac, const char **av, int algo)
{
	git_hash_ctx ctx;
	unsigned char hash[GIT_MAX_HEXSZ];
	unsigned bufsz = 8192;
	int binary = 0;
	char *buffer;
	const struct git_hash_algo *algop = &hash_algos[algo];

	if (ac == 2) {
		if (!strcmp(av[1], "-b"))
			binary = 1;
		else
			bufsz = strtoul(av[1], NULL, 10) * 1024 * 1024;
	}

	if (!bufsz)
		bufsz = 8192;

	while ((buffer = malloc(bufsz)) == NULL) {
		fprintf(stderr, "bufsz %u is too big, halving...\n", bufsz);
		bufsz /= 2;
		if (bufsz < 1024)
			die("OOPS");
	}

	algop->init_fn(&ctx);

	while (1) {
		ssize_t sz, this_sz;
		char *cp = buffer;
		unsigned room = bufsz;
		this_sz = 0;
		while (room) {
			sz = xread(0, cp, room);
			if (sz == 0)
				break;
			if (sz < 0)
				die_errno("test-hash");
			this_sz += sz;
			cp += sz;
			room -= sz;
		}
		if (this_sz == 0)
			break;
		algop->update_fn(&ctx, buffer, this_sz);
	}
	algop->final_fn(hash, &ctx);

	if (binary)
		fwrite(hash, 1, algop->rawsz, stdout);
	else
		puts(hash_to_hex_algop(hash, algop));
	free(buffer);
	return 0;
}
