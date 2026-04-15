#ifndef TR2_DST_H
#define TR2_DST_H

struct strbuf;
#include "trace2/tr2_sysenv.h"

struct tr2_dst {
	enum tr2_sysenv_variable sysenv_var;
	int fd;
	unsigned int initialized : 1;
	unsigned int need_close : 1;
	unsigned int too_many_files : 1;
};

/*
 * Disable TRACE2 on the destination.  In TRACE2 a destination (DST)
 * wraps a file descriptor; it is associated with a TARGET which
 * defines the formatting.
 */
void tr2_dst_trace_disable(struct tr2_dst *dst);

/*
 * Return the file descriptor for the DST.
 * If 0, the dst is closed or disabled.
 */
int tr2_dst_get_trace_fd(struct tr2_dst *dst);

/*
 * Return true if the DST is opened for writing.
 */
int tr2_dst_trace_want(struct tr2_dst *dst);

/*
 * Write a single line/message to the trace file.
 */
void tr2_dst_write_line(struct tr2_dst *dst, struct strbuf *buf_line);

#endif /* TR2_DST_H */
