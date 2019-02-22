#include "cache.h"
#include "thread-utils.h"
#include "trace2/tr2_tls.h"

/*
 * Initialize size of the thread stack for nested regions.
 * This is used to store nested region start times.  Note that
 * this stack is per-thread and not per-trace-key.
 */
#define TR2_REGION_NESTING_INITIAL_SIZE (100)

static struct tr2tls_thread_ctx *tr2tls_thread_main;
static uint64_t tr2tls_us_start_main;

static pthread_mutex_t tr2tls_mutex;
static pthread_key_t tr2tls_key;

static int tr2_next_thread_id; /* modify under lock */

struct tr2tls_thread_ctx *tr2tls_create_self(const char *thread_name)
{
	uint64_t us_now = getnanotime() / 1000;
	struct tr2tls_thread_ctx *ctx = xcalloc(1, sizeof(*ctx));

	/*
	 * Implicitly "tr2tls_push_self()" to capture the thread's start
	 * time in array_us_start[0].  For the main thread this gives us the
	 * application run time.
	 */
	ctx->alloc = TR2_REGION_NESTING_INITIAL_SIZE;
	ctx->array_us_start = (uint64_t *)xcalloc(ctx->alloc, sizeof(uint64_t));
	ctx->array_us_start[ctx->nr_open_regions++] = us_now;

	ctx->thread_id = tr2tls_locked_increment(&tr2_next_thread_id);

	strbuf_init(&ctx->thread_name, 0);
	if (ctx->thread_id)
		strbuf_addf(&ctx->thread_name, "th%02d:", ctx->thread_id);
	strbuf_addstr(&ctx->thread_name, thread_name);
	if (ctx->thread_name.len > TR2_MAX_THREAD_NAME)
		strbuf_setlen(&ctx->thread_name, TR2_MAX_THREAD_NAME);

	pthread_setspecific(tr2tls_key, ctx);

	return ctx;
}

struct tr2tls_thread_ctx *tr2tls_get_self(void)
{
	struct tr2tls_thread_ctx *ctx = pthread_getspecific(tr2tls_key);

	/*
	 * If the thread-proc did not call trace2_thread_start(), we won't
	 * have any TLS data associated with the current thread.  Fix it
	 * here and silently continue.
	 */
	if (!ctx)
		ctx = tr2tls_create_self("unknown");

	return ctx;
}

int tr2tls_is_main_thread(void)
{
	struct tr2tls_thread_ctx *ctx = pthread_getspecific(tr2tls_key);

	return ctx == tr2tls_thread_main;
}

void tr2tls_unset_self(void)
{
	struct tr2tls_thread_ctx *ctx;

	ctx = tr2tls_get_self();

	pthread_setspecific(tr2tls_key, NULL);

	free(ctx->array_us_start);
	free(ctx);
}

void tr2tls_push_self(uint64_t us_now)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();

	ALLOC_GROW(ctx->array_us_start, ctx->nr_open_regions + 1, ctx->alloc);
	ctx->array_us_start[ctx->nr_open_regions++] = us_now;
}

void tr2tls_pop_self(void)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();

	if (!ctx->nr_open_regions)
		BUG("no open regions in thread '%s'", ctx->thread_name.buf);

	ctx->nr_open_regions--;
}

void tr2tls_pop_unwind_self(void)
{
	struct tr2tls_thread_ctx *ctx = tr2tls_get_self();

	while (ctx->nr_open_regions > 1)
		tr2tls_pop_self();
}

uint64_t tr2tls_region_elasped_self(uint64_t us)
{
	struct tr2tls_thread_ctx *ctx;
	uint64_t us_start;

	ctx = tr2tls_get_self();
	if (!ctx->nr_open_regions)
		return 0;

	us_start = ctx->array_us_start[ctx->nr_open_regions - 1];

	return us - us_start;
}

uint64_t tr2tls_absolute_elapsed(uint64_t us)
{
	if (!tr2tls_thread_main)
		return 0;

	return us - tr2tls_us_start_main;
}

void tr2tls_init(void)
{
	pthread_key_create(&tr2tls_key, NULL);
	init_recursive_mutex(&tr2tls_mutex);

	tr2tls_thread_main = tr2tls_create_self("main");
	/*
	 * Keep a copy of the absolute start time of the main thread
	 * in a fixed variable since other threads need to access it.
	 * This also eliminates the need to lock accesses to the main
	 * thread's array (because of reallocs).
	 */
	tr2tls_us_start_main = tr2tls_thread_main->array_us_start[0];
}

void tr2tls_release(void)
{
	tr2tls_unset_self();
	tr2tls_thread_main = NULL;

	pthread_mutex_destroy(&tr2tls_mutex);
	pthread_key_delete(tr2tls_key);
}

int tr2tls_locked_increment(int *p)
{
	int current_value;

	pthread_mutex_lock(&tr2tls_mutex);
	current_value = *p;
	*p = current_value + 1;
	pthread_mutex_unlock(&tr2tls_mutex);

	return current_value;
}
