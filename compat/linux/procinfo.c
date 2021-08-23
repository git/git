#include "cache.h"

#include "strbuf.h"
#include "strvec.h"
#include "trace2.h"

static void get_ancestry_names(struct strvec *names)
{
	/*
	 * NEEDSWORK: We could gather the entire pstree into an array to match
	 * functionality with compat/win32/trace2_win32_process_info.c.
	 * To do so, we may want to examine /proc/<pid>/stat. For now, just
	 * gather the immediate parent name which is readily accessible from
	 * /proc/$(getppid())/comm.
	 */
	struct strbuf procfs_path = STRBUF_INIT;
	struct strbuf name = STRBUF_INIT;

	/* try to use procfs if it's present. */
	strbuf_addf(&procfs_path, "/proc/%d/comm", getppid());
	if (strbuf_read_file(&name, procfs_path.buf, 0)) {
		strbuf_release(&procfs_path);
		strbuf_trim_trailing_newline(&name);
		strvec_push(names, strbuf_detach(&name, NULL));
	}

	return;
	/* NEEDSWORK: add non-procfs-linux implementations here */
}

void trace2_collect_process_info(enum trace2_process_info_reason reason)
{
	if (!trace2_is_enabled())
		return;

	/* someday we may want to write something extra here, but not today */
	if (reason == TRACE2_PROCESS_INFO_EXIT)
		return;

	if (reason == TRACE2_PROCESS_INFO_STARTUP) {
		/*
		 * NEEDSWORK: we could do the entire ptree in an array instead,
		 * see compat/win32/trace2_win32_process_info.c.
		 */
		struct strvec names = STRVEC_INIT;

		get_ancestry_names(&names);

		if (names.nr)
			trace2_cmd_ancestry(names.v);
		strvec_clear(&names);
	}

	return;
}
