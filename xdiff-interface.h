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

int xdi_diff(mmfile_t *mf1, mmfile_t *mf2, xpparam_t const *xpp, xdemitconf_t const *xecfg, xdemitcb_t *ecb);
int xdiff_outf(void *priv_, mmbuffer_t *mb, int nbuf);
int parse_hunk_header(char *line, int len,
		      int *ob, int *on,
		      int *nb, int *nn);
int read_mmfile(mmfile_t *ptr, const char *filename);
int buffer_is_binary(const char *ptr, unsigned long size);

extern void xdiff_set_find_func(xdemitconf_t *xecfg, const char *line);

#endif
