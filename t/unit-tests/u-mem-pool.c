#include "unit-test.h"
#include "mem-pool.h"

static void t_calloc_100(size_t block_alloc)
{
	struct mem_pool pool = { .block_alloc = block_alloc };
	size_t size = 100;
	char *buffer = mem_pool_calloc(&pool, 1, size);
	for (size_t i = 0; i < size; i++)
		cl_assert_equal_i(0, buffer[i]);
	cl_assert(pool.mp_block != NULL);
	cl_assert(pool.mp_block->next_free != NULL);
	cl_assert(pool.mp_block->end != NULL);
	mem_pool_discard(&pool, 0);
}

void test_mem_pool__big_block(void)
{
	t_calloc_100(1024 * 1024);

}

void test_mem_pool__tiny_block(void)
{
	t_calloc_100(1);
}
