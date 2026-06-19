#ifndef DIFF_PROCESS_H
#define DIFF_PROCESS_H

#include "xdiff/xdiff.h"

struct diff_options;

enum diff_process_result {
	DIFF_PROCESS_ERROR = -1, /* tool failure: warned, fell back */
	DIFF_PROCESS_OK = 0,     /* hunks populated in xpp */
	DIFF_PROCESS_SKIP,       /* no process configured: use builtin */
	DIFF_PROCESS_EQUIVALENT, /* tool says files are equivalent */
};

/*
 * Consult the diff process configured for 'path' and populate
 * xpp->external_hunks with the returned hunks.
 *
 * Handles driver lookup, flag checks (--no-ext-diff,
 * --diff-algorithm), subprocess management, and error reporting.
 *
 * Returns DIFF_PROCESS_OK when hunks are populated in xpp.
 * The caller owns xpp->external_hunks and must free() it.
 *
 * Returns DIFF_PROCESS_EQUIVALENT when the tool returns no hunks
 * (files are considered identical); caller should skip diff/blame.
 * Returns DIFF_PROCESS_SKIP when no process applies; caller
 * should use the builtin diff algorithm.
 * Returns DIFF_PROCESS_ERROR on tool failure (already warned);
 * caller should fall back to the builtin diff algorithm.
 */
enum diff_process_result diff_process_fill_hunks(
		struct diff_options *diffopt,
		const char *path,
		const mmfile_t *file_a,
		const mmfile_t *file_b,
		xpparam_t *xpp);

#endif /* DIFF_PROCESS_H */
