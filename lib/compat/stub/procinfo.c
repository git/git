#include "git-compat-util.h"

#include "trace2.h"

/*
 * Stub. See sample implementations in compat/linux/procinfo.c and
 * compat/win32/trace2_win32_process_info.c.
 */
void trace2_collect_process_info(enum trace2_process_info_reason reason UNUSED)
{
}
