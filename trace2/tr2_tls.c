#include "git-compat-util.h"
#include "strbuf.h"
#include "thread-utils.h"
#include "trace.h"
#include "trace2/tr2_tls.h"

/*
 * Initialize size of the thread stack for nested regions.
 * This is used to store nested region start times.  Note that
 * this stack is per-thread and not per-trace-key.
 */
#define TR2_REGION_NESTING_INITIAL_SIZE (100)

static struct tr2tls_thread_ctx *tr2tls_thread_main;
static uint64_t tr2tls_us_start_process;

static pthread_mutex_t tr2tls_mutex;
static pthread_key_t tr2tls_key;

static int tr2_next_thread_id; /* modify under lock */

void tr2tls_start_process_clock(void)
{
	if (tr2tls_us_start_process)
		return;

	/*
	 * Keep the absolute start time of the process (i.e. the main
	 * process) in a fixed variable since other threads need to
	 * access it.  This allows them to do that without a lock on
	 * main thread's array data (because of reallocs).
	 */
	tr2tls_us_start_process = getnanotime() / 1000;
}

struct tr2tls_thread_ctx *tr2tls_create_self(const char *thread_base_name,
					     uint64_t us_thread_start)
{
	struct tr2tls_thread_ctx *ctx = xcalloc(1, sizeof(*ctx));
	struct strbuf buf = STRBUF_INIT;

	/*
	 * Implicitly "tr2tls_push_self()" to capture the thread's start
	 * time in array_us_start[0].  For the main thread this gives us the
	 * application run time.
	 */
	ctx->alloc = TR2_REGION_NESTING_INITIAL_SIZE;
	ctx->array_us_start = (uint64_t *)xcalloc(ctx->alloc, sizeof(uint64_t));
	ctx->array_us_start[ctx->nr_open_regions++] = us_thread_start;

	ctx->thread_id = tr2tls_locked_increment(&tr2_next_thread_id);

	strbuf_init(&buf, 0);
	if (ctx->thread_id)
		strbuf_addf(&buf, "th%02d:", ctx->thread_id);
	strbuf_addstr(&buf, thread_base_name);
	if (buf.len > TR2_MAX_THREAD_NAME)
		strbuf_setlen(&buf, TR2_MAX_THREAD_NAME);
	ctx->thread_name = strbuf_detach(&buf, NULL);

	pthread_setspecific(tr2tls_key, ctx);

	return ctx;
}

struct tr2tls_thread_ctx *tr2tls_get_self(void)
{
	struct tr2tls_thread_ctx *ctx;

	if (!HAVE_THREADS)
		return tr2tls_thread_main;

	ctx = pthread_getspecific(tr2tls_key);

	/*
	 * If the current thread's thread-proc did not call
	 * trace2_thread_start(), then the thread will not have any
	 * thread-local storage.  Create it now and silently continue.
	 */
	if (!ctx)
		ctx = tr2tls_create_self("unknown", getnanotime() / 1000);

	return ctx;
}

int tr2tls_is_main_thread(void)
{
	if (!HAVE_THREADS)
		return 1;

	return pthread_getspecific(tr2tls_key) == tr2tls_thread_main;
}

void tr2tls_unset_self(void)
{
	struct tr2tls_thread_ctx *ctx;

	ctx = tr2tls_get_self();

	pthread_setspecific(tr2tls_key, NULL);

	free((char *)ctx->thread_name);
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
		BUG("no open regions in thread '%s'", ctx->thread_name);

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

	return us - tr2tls_us_start_process;
}

static void tr2tls_key_destructor(void *payload)
{
	struct tr2tls_thread_ctx *ctx = payload;
	free((char *)ctx->thread_name);
	free(ctx->array_us_start);
	free(ctx);
}

void tr2tls_init(void)
{
	tr2tls_start_process_clock();

	pthread_key_create(&tr2tls_key, tr2tls_key_destructor);
	init_recursive_mutex(&tr2tls_mutex);

	tr2tls_thread_main =
		tr2tls_create_self("main", tr2tls_us_start_process);
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

void tr2tls_lock(void)
{
	pthread_mutex_lock(&tr2tls_mutex);
}

void tr2tls_unlock(void)
{
	pthread_mutex_unlock(&tr2tls_mutex);
}
