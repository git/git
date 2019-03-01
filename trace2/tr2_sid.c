#include "cache.h"
#include "trace2/tr2_sid.h"

#define TR2_ENVVAR_PARENT_SID "GIT_TR2_PARENT_SID"

static struct strbuf tr2sid_buf = STRBUF_INIT;
static int tr2sid_nr_git_parents;

/*
 * Compute a "unique" session id (SID) for the current process.  This allows
 * all events from this process to have a single label (much like a PID).
 *
 * Export this into our environment so that all child processes inherit it.
 *
 * If we were started by another git instance, use our parent's SID as a
 * prefix.  (This lets us track parent/child relationships even if there
 * is an intermediate shell process.)
 *
 * Additionally, count the number of nested git processes.
 */
static void tr2_sid_compute(void)
{
	uint64_t us_now;
	const char *parent_sid;

	if (tr2sid_buf.len)
		return;

	parent_sid = getenv(TR2_ENVVAR_PARENT_SID);
	if (parent_sid && *parent_sid) {
		const char *p;
		for (p = parent_sid; *p; p++)
			if (*p == '/')
				tr2sid_nr_git_parents++;

		strbuf_addstr(&tr2sid_buf, parent_sid);
		strbuf_addch(&tr2sid_buf, '/');
		tr2sid_nr_git_parents++;
	}

	us_now = getnanotime() / 1000;
	strbuf_addf(&tr2sid_buf, "%" PRIuMAX "-%" PRIdMAX, (uintmax_t)us_now,
		    (intmax_t)getpid());

	setenv(TR2_ENVVAR_PARENT_SID, tr2sid_buf.buf, 1);
}

const char *tr2_sid_get(void)
{
	if (!tr2sid_buf.len)
		tr2_sid_compute();

	return tr2sid_buf.buf;
}

int tr2_sid_depth(void)
{
	if (!tr2sid_buf.len)
		tr2_sid_compute();

	return tr2sid_nr_git_parents;
}

void tr2_sid_release(void)
{
	strbuf_release(&tr2sid_buf);
}
