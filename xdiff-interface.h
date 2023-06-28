#ifndef XDIFF_INTERFACE_H
#define XDIFF_INTERFACE_H

#include "hash-ll.h"
#include "xdiff/xdiff.h"

/*
 * xdiff isn't equipped to handle content over a gigabyte;
 * we make the cutoff 1GB - 1MB to give some breathing
 * room for constant-sized additions (e.g., merge markers)
 */
#define MAX_XDIFF_SIZE (1024UL * 1024 * 1023)

/**
 * The `xdiff_emit_line_fn` function can return 1 to abort early, or 0
 * to continue processing. Note that doing so is an all-or-nothing
 * affair, as returning 1 will return all the way to the top-level,
 * e.g. the xdi_diff_outf() call to generate the diff.
 *
 * Thus returning 1 means you won't be getting any more diff lines. If
 * you need something in-between those two options you'll to use
 * `xdl_emit_hunk_consume_func_t` and implement your own version of
 * xdl_emit_diff().
 *
 * We may extend the interface in the future to understand other more
 * granular return values. While you should return 1 to exit early,
 * doing so will currently make your early return indistinguishable
 * from an error internal to xdiff, xdiff itself will see that
 * non-zero return and translate it to -1.
 *
 * See "diff_grep" in diffcore-pickaxe.c for a trick to work around
 * this, i.e. using the "consume_callback_data" to note the desired
 * early return.
 */
typedef int (*xdiff_emit_line_fn)(void *, char *, unsigned long);
typedef void (*xdiff_emit_hunk_fn)(void *data,
				   long old_begin, long old_nr,
				   long new_begin, long new_nr,
				   const char *func, long funclen);

int xdi_diff(mmfile_t *mf1, mmfile_t *mf2, xpparam_t const *xpp, xdemitconf_t const *xecfg, xdemitcb_t *ecb);
int xdi_diff_outf(mmfile_t *mf1, mmfile_t *mf2,
		  xdiff_emit_hunk_fn hunk_fn,
		  xdiff_emit_line_fn line_fn,
		  void *consume_callback_data,
		  xpparam_t const *xpp, xdemitconf_t const *xecfg);
int read_mmfile(mmfile_t *ptr, const char *filename);
void read_mmblob(mmfile_t *ptr, const struct object_id *oid);
int buffer_is_binary(const char *ptr, unsigned long size);

void xdiff_set_find_func(xdemitconf_t *xecfg, const char *line, int cflags);
void xdiff_clear_find_func(xdemitconf_t *xecfg);
struct config_context;
int git_xmerge_config(const char *var, const char *value,
		      const struct config_context *ctx, void *cb);
extern int git_xmerge_style;

/*
 * Compare the strings l1 with l2 which are of size s1 and s2 respectively.
 * Returns 1 if the strings are deemed equal, 0 otherwise.
 * The `flags` given as XDF_WHITESPACE_FLAGS determine how white spaces
 * are treated for the comparison.
 */
int xdiff_compare_lines(const char *l1, long s1,
			const char *l2, long s2, long flags);

/*
 * Returns a hash of the string s of length len.
 * The `flags` given as XDF_WHITESPACE_FLAGS determine how white spaces
 * are treated for the hash.
 */
unsigned long xdiff_hash_string(const char *s, size_t len, long flags);

#endif
