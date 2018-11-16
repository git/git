/*
 * Memory Pool implementation logic.
 */

#include "git-compat-util.h"
#include "mem-pool.h"
#include "gettext.h"
#include "trace.h"

static struct trace_key trace_mem_pool = TRACE_KEY_INIT(MEMPOOL);
#define BLOCK_GROWTH_SIZE (1024 * 1024 - sizeof(struct mp_block))

/*
 * The inner union is an approximation for C11's max_align_t, and the
 * struct + offsetof computes _Alignof. This can all just be replaced
 * with _Alignof(max_align_t) if/when C11 is part of the baseline.
 * Note that _Alignof(X) need not be the same as sizeof(X); it's only
 * required to be a (possibly trivial) factor. They are the same for
 * most architectures, but m68k for example has only 2-byte alignment
 * for its 4-byte and 8-byte types, so using sizeof would waste space.
 *
 * Add more types to the union if the current set is insufficient.
 */
struct git_max_alignment {
	char unalign;
	union {
		uintmax_t max_align_uintmax;
		void *max_align_pointer;
	} aligned;
};
#define GIT_MAX_ALIGNMENT offsetof(struct git_max_alignment, aligned)

/*
 * Allocate a new mp_block and insert it after the block specified in
 * `insert_after`. If `insert_after` is NULL, then insert block at the
 * head of the linked list.
 */
static struct mp_block *mem_pool_alloc_block(struct mem_pool *pool,
					     size_t block_alloc,
					     struct mp_block *insert_after)
{
	struct mp_block *p;

	pool->pool_alloc += sizeof(struct mp_block) + block_alloc;
	p = xmalloc(st_add(sizeof(struct mp_block), block_alloc));

	p->next_free = (char *)p->space;
	p->end = p->next_free + block_alloc;

	if (insert_after) {
		p->next_block = insert_after->next_block;
		insert_after->next_block = p;
	} else {
		p->next_block = pool->mp_block;
		pool->mp_block = p;
	}

	return p;
}

void mem_pool_init(struct mem_pool *pool, size_t initial_size)
{
	memset(pool, 0, sizeof(*pool));
	pool->block_alloc = BLOCK_GROWTH_SIZE;

	if (initial_size > 0)
		mem_pool_alloc_block(pool, initial_size, NULL);

	trace_printf_key(&trace_mem_pool,
		"mem_pool (%p): init (%"PRIuMAX") initial size\n",
		(void *)pool, (uintmax_t)initial_size);
}

void mem_pool_discard(struct mem_pool *pool, int invalidate_memory)
{
	struct mp_block *block, *block_to_free;

	trace_printf_key(&trace_mem_pool,
		"mem_pool (%p): discard (%"PRIuMAX") unused\n",
		(void *)pool,
		(uintmax_t)(pool->mp_block->end - pool->mp_block->next_free));
	block = pool->mp_block;
	while (block)
	{
		block_to_free = block;
		block = block->next_block;

		if (invalidate_memory)
			memset(block_to_free->space, 0xDD, ((char *)block_to_free->end) - ((char *)block_to_free->space));

		free(block_to_free);
	}

	pool->mp_block = NULL;
	pool->pool_alloc = 0;
}

void *mem_pool_alloc(struct mem_pool *pool, size_t len)
{
	struct mp_block *p = NULL;
	void *r;

	len = DIV_ROUND_UP(len, GIT_MAX_ALIGNMENT) * GIT_MAX_ALIGNMENT;

	if (pool->mp_block &&
	    pool->mp_block->end - pool->mp_block->next_free >= len)
		p = pool->mp_block;

	if (!p) {
		if (len >= (pool->block_alloc / 2))
			p = mem_pool_alloc_block(pool, len, pool->mp_block);
		else
			p = mem_pool_alloc_block(pool, pool->block_alloc, NULL);
	}

	r = p->next_free;
	p->next_free += len;
	return r;
}

static char *mem_pool_strvfmt(struct mem_pool *pool, const char *fmt,
			      va_list ap)
{
	struct mp_block *block = pool->mp_block;
	char *next_free = block ? block->next_free : NULL;
	size_t available = block ? block->end - block->next_free : 0;
	va_list cp;
	int len, len2;
	size_t size;
	char *ret;

	va_copy(cp, ap);
	len = vsnprintf(next_free, available, fmt, cp);
	va_end(cp);
	if (len < 0)
		die(_("unable to format message: %s"), fmt);

	size = st_add(len, 1); /* 1 for NUL */
	ret = mem_pool_alloc(pool, size);

	/* Shortcut; relies on mem_pool_alloc() not touching buffer contents. */
	if (ret == next_free)
		return ret;

	len2 = vsnprintf(ret, size, fmt, ap);
	if (len2 != len)
		BUG("your vsnprintf is broken (returns inconsistent lengths)");
	return ret;
}

char *mem_pool_strfmt(struct mem_pool *pool, const char *fmt, ...)
{
	va_list ap;
	char *ret;

	va_start(ap, fmt);
	ret = mem_pool_strvfmt(pool, fmt, ap);
	va_end(ap);
	return ret;
}

void *mem_pool_calloc(struct mem_pool *pool, size_t count, size_t size)
{
	size_t len = st_mult(count, size);
	void *r = mem_pool_alloc(pool, len);
	memset(r, 0, len);
	return r;
}

char *mem_pool_strdup(struct mem_pool *pool, const char *str)
{
	size_t len = strlen(str) + 1;
	char *ret = mem_pool_alloc(pool, len);

	return memcpy(ret, str, len);
}

char *mem_pool_strndup(struct mem_pool *pool, const char *str, size_t len)
{
	char *p = memchr(str, '\0', len);
	size_t actual_len = (p ? p - str : len);
	char *ret = mem_pool_alloc(pool, actual_len+1);

	ret[actual_len] = '\0';
	return memcpy(ret, str, actual_len);
}

int mem_pool_contains(struct mem_pool *pool, void *mem)
{
	struct mp_block *p;

	/* Check if memory is allocated in a block */
	for (p = pool->mp_block; p; p = p->next_block)
		if ((mem >= ((void *)p->space)) &&
		    (mem < ((void *)p->end)))
			return 1;

	return 0;
}

void mem_pool_combine(struct mem_pool *dst, struct mem_pool *src)
{
	struct mp_block *p;

	/* Append the blocks from src to dst */
	if (dst->mp_block && src->mp_block) {
		/*
		 * src and dst have blocks, append
		 * blocks from src to dst.
		 */
		p = dst->mp_block;
		while (p->next_block)
			p = p->next_block;

		p->next_block = src->mp_block;
	} else if (src->mp_block) {
		/*
		 * src has blocks, dst is empty.
		 */
		dst->mp_block = src->mp_block;
	} else {
		/* src is empty, nothing to do. */
	}

	dst->pool_alloc += src->pool_alloc;
	src->pool_alloc = 0;
	src->mp_block = NULL;
}
