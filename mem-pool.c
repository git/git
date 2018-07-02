/*
 * Memory Pool implementation logic.
 */

#include "cache.h"
#include "mem-pool.h"

static struct mp_block *mem_pool_alloc_block(struct mem_pool *mem_pool, size_t block_alloc)
{
	struct mp_block *p;

	mem_pool->pool_alloc += sizeof(struct mp_block) + block_alloc;
	p = xmalloc(st_add(sizeof(struct mp_block), block_alloc));
	p->next_block = mem_pool->mp_block;
	p->next_free = (char *)p->space;
	p->end = p->next_free + block_alloc;
	mem_pool->mp_block = p;

	return p;
}

void *mem_pool_alloc(struct mem_pool *mem_pool, size_t len)
{
	struct mp_block *p = NULL;
	void *r;

	/* round up to a 'uintmax_t' alignment */
	if (len & (sizeof(uintmax_t) - 1))
		len += sizeof(uintmax_t) - (len & (sizeof(uintmax_t) - 1));

	if (mem_pool->mp_block &&
	    mem_pool->mp_block->end - mem_pool->mp_block->next_free >= len)
		p = mem_pool->mp_block;

	if (!p) {
		if (len >= (mem_pool->block_alloc / 2)) {
			mem_pool->pool_alloc += len;
			return xmalloc(len);
		}

		p = mem_pool_alloc_block(mem_pool, mem_pool->block_alloc);
	}

	r = p->next_free;
	p->next_free += len;
	return r;
}

void *mem_pool_calloc(struct mem_pool *mem_pool, size_t count, size_t size)
{
	size_t len = st_mult(count, size);
	void *r = mem_pool_alloc(mem_pool, len);
	memset(r, 0, len);
	return r;
}
