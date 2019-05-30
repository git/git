#include "cache.h"
#include "trace2/tr2_tbuf.h"
#include "trace2/tr2_sid.h"

#define TR2_ENVVAR_PARENT_SID "GIT_TRACE2_PARENT_SID"

static struct strbuf tr2sid_buf = STRBUF_INIT;
static int tr2sid_nr_git_parents;

/*
 * Compute the final component of the SID representing the current process.
 * This should uniquely identify the process and be a valid filename (to
 * allow writing trace2 data to per-process files).  It should also be fixed
 * length for possible use as a database key.
 *
 * "<yyyymmdd>T<hhmmss>.<fraction>Z-<host>-<process>"
 *
 * where <host> is a 9 character string:
 *    "H<first_8_chars_of_sha1_of_hostname>"
 *    "Localhost" when no hostname.
 *
 * where <process> is a 9 character string containing the least signifcant
 * 32 bits in the process-id.
 *    "P<pid>"
 * (This is an abribrary choice.  On most systems pid_t is a 32 bit value,
 * so limit doesn't matter.  On larger systems, a truncated value is fine
 * for our purposes here.)
 */
static void tr2_sid_append_my_sid_component(void)
{
	const struct git_hash_algo *algo = &hash_algos[GIT_HASH_SHA1];
	struct tr2_tbuf tb_now;
	git_hash_ctx ctx;
	pid_t pid = getpid();
	unsigned char hash[GIT_MAX_RAWSZ + 1];
	char hex[GIT_MAX_HEXSZ + 1];
	char hostname[HOST_NAME_MAX + 1];

	tr2_tbuf_utc_datetime(&tb_now);
	strbuf_addstr(&tr2sid_buf, tb_now.buf);

	strbuf_addch(&tr2sid_buf, '-');
	if (xgethostname(hostname, sizeof(hostname)))
		strbuf_add(&tr2sid_buf, "Localhost", 9);
	else {
		algo->init_fn(&ctx);
		algo->update_fn(&ctx, hostname, strlen(hostname));
		algo->final_fn(hash, &ctx);
		hash_to_hex_algop_r(hex, hash, algo);
		strbuf_addch(&tr2sid_buf, 'H');
		strbuf_add(&tr2sid_buf, hex, 8);
	}

	strbuf_addf(&tr2sid_buf, "-P%08"PRIx32, (uint32_t)pid);
}

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

	tr2_sid_append_my_sid_component();

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
