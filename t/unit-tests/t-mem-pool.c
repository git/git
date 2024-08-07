#include "test-lib.h"
#include "mem-pool.h"

static void setup_static(void (*f)(struct mem_pool *), size_t block_alloc)
{
	struct mem_pool pool = { .block_alloc = block_alloc };
	f(&pool);
	mem_pool_discard(&pool, 0);
}

static void t_calloc_100(struct mem_pool *pool)
{
	size_t size = 100;
	char *buffer = mem_pool_calloc(pool, 1, size);
	for (size_t i = 0; i < size; i++)
		check_int(buffer[i], ==, 0);
	if (!check(pool->mp_block != NULL))
		return;
	check(pool->mp_block->next_free != NULL);
	check(pool->mp_block->end != NULL);
}

int cmd_main(int argc, const char **argv)
{
	TEST(setup_static(t_calloc_100, 1024 * 1024),
	     "mem_pool_calloc returns 100 zeroed bytes with big block");
	TEST(setup_static(t_calloc_100, 1),
	     "mem_pool_calloc returns 100 zeroed bytes with tiny block");

	return test_done();
}
