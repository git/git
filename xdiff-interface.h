#ifndef XDIFF_INTERFACE_H
#define XDIFF_INTERFACE_H

#include "xdiff/xdiff.h"

struct xdiff_emit_state;

typedef void (*xdiff_emit_consume_fn)(void *, char *, unsigned long);

struct xdiff_emit_state {
	xdiff_emit_consume_fn consume;
	char *remainder;
	unsigned long remainder_size;
};

int xdiff_outf(void *priv_, mmbuffer_t *mb, int nbuf);
int parse_hunk_header(char *line, int len,
		      int *ob, int *on,
		      int *nb, int *nn);
int read_mmfile(mmfile_t *ptr, const char *filename);

#endif
