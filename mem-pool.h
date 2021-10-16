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
 * Initialize mem_pool with specified initial size.
 */
void mem_pool_init(struct mem_pool *pool, size_t initial_size);

/*
 * Discard all the memory the memory pool is responsible for.
 */
void mem_pool_discard(struct mem_pool *pool, int invalidate_memory);

/*
 * Alloc memory from the mem_pool.
 */
void *mem_pool_alloc(struct mem_pool *pool, size_t len);

/*
 * Allocate and zero memory from the memory pool.
 */
void *mem_pool_calloc(struct mem_pool *pool, size_t count, size_t size);

/*
 * Allocate memory from the memory pool and copy str into it.
 */
char *mem_pool_strdup(struct mem_pool *pool, const char *str);
char *mem_pool_strndup(struct mem_pool *pool, const char *str, size_t len);

/*
 * Move the memory associated with the 'src' pool to the 'dst' pool. The 'src'
 * pool will be empty and not contain any memory. It still needs to be free'd
 * with a call to `mem_pool_discard`.
 */
void mem_pool_combine(struct mem_pool *dst, struct mem_pool *src);

/*
 * Check if a memory pointed at by 'mem' is part of the range of
 * memory managed by the specified mem_pool.
 */
int mem_pool_contains(struct mem_pool *pool, void *mem);

#endif
