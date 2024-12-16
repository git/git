#include "test-tool.h"
#include "hash.h"

#define NUM_SECONDS 3

static inline void compute_hash(const struct git_hash_algo *algo, git_hash_ctx *ctx, uint8_t *final, const void *p, size_t len)
{
	algo->init_fn(ctx);
	algo->update_fn(ctx, p, len);
	algo->final_fn(final, ctx);
}

int cmd__hash_speed(int ac, const char **av)
{
	git_hash_ctx ctx;
	unsigned char hash[GIT_MAX_RAWSZ];
	clock_t initial, start, end;
	unsigned bufsizes[] = { 64, 256, 1024, 8192, 16384 };
	void *p;
	const struct git_hash_algo *algo = NULL;

	if (ac == 2) {
		for (size_t i = 1; i < GIT_HASH_NALGOS; i++) {
			if (!strcmp(av[1], hash_algos[i].name)) {
				algo = &hash_algos[i];
				break;
			}
		}
	}
	if (!algo)
		die("usage: test-tool hash-speed algo_name");

	/* Use this as an offset to make overflow less likely. */
	initial = clock();

	printf("algo: %s\n", algo->name);

	for (size_t i = 0; i < ARRAY_SIZE(bufsizes); i++) {
		unsigned long j, kb;
		double kb_per_sec;
		p = xcalloc(1, bufsizes[i]);
		start = end = clock() - initial;
		for (j = 0; ((end - start) / CLOCKS_PER_SEC) < NUM_SECONDS; j++) {
			compute_hash(algo, &ctx, hash, p, bufsizes[i]);

			/*
			 * Only check elapsed time every 128 iterations to avoid
			 * dominating the runtime with system calls.
			 */
			if (!(j & 127))
				end = clock() - initial;
		}
		kb = j * bufsizes[i];
		kb_per_sec = kb / (1024 * ((double)end - start) / CLOCKS_PER_SEC);
		printf("size %u: %lu iters; %lu KiB; %0.2f KiB/s\n", bufsizes[i], j, kb, kb_per_sec);
		free(p);
	}

	return 0;
}
