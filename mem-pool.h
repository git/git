#ifndef MEM_POOL_H
#define MEM_POOL_H

struct mp_block {
	struct mp_block *next_block;
	char *next_free;
	char *end;
	uintmax_t space[FLEX_ARRAY]; /* more */
};

struct mem_pool {
	struct mp_block *mp_block;

	/*
	 * The amount of available memory to grow the pool by.
	 * This size does not include the overhead for the mp_block.
	 */
	size_t block_alloc;

	/* The total amount of memory allocated by the pool. */
	size_t pool_alloc;
};

/*
 * Alloc memory from the mem_pool.
 */
void *mem_pool_alloc(struct mem_pool *pool, size_t len);

/*
 * Allocate and zero memory from the memory pool.
 */
void *mem_pool_calloc(struct mem_pool *pool, size_t count, size_t size);

#endif
