/*
 *  LibXDiff by Davide Libenzi ( File Differential Library )
 *  Copyright (C) 2003  Davide Libenzi
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, see
 *  <http://www.gnu.org/licenses/>.
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include "xinclude.h"


#define XDL_KPDIS_RUN 4
#define XDL_MAX_EQLIMIT 1024
#define XDL_SIMSCAN_WINDOW 100
#define XDL_GUESS_NLINES1 256
#define XDL_GUESS_NLINES2 20

#define DISCARD 0
#define KEEP 1
#define INVESTIGATE 2

typedef struct s_xdlclass {
	uint64_t line_hash;
	struct s_xdlclass *next;
	const uint8_t *ptr;
	size_t size;
	long idx;
	long len1, len2;
} xdlclass_t;

typedef struct s_xdlclassifier {
	unsigned int hbits;
	long hsize;
	xdlclass_t **rchash;
	chastore_t ncha;
	xdlclass_t **rcrecs;
	long alloc;
	long count;
	long flags;
} xdlclassifier_t;




static int xdl_init_classifier(xdlclassifier_t *cf, long size, long flags) {
	cf->flags = flags;

	cf->hbits = xdl_hashbits((unsigned int) size);
	cf->hsize = 1 << cf->hbits;

	if (xdl_cha_init(&cf->ncha, sizeof(xdlclass_t), size / 4 + 1) < 0) {

		return -1;
	}
	if (!XDL_CALLOC_ARRAY(cf->rchash, cf->hsize)) {

		xdl_cha_free(&cf->ncha);
		return -1;
	}

	cf->alloc = size;
	if (!XDL_ALLOC_ARRAY(cf->rcrecs, cf->alloc)) {

		xdl_free(cf->rchash);
		xdl_cha_free(&cf->ncha);
		return -1;
	}

	cf->count = 0;

	return 0;
}


static void xdl_free_classifier(xdlclassifier_t *cf) {

	xdl_free(cf->rcrecs);
	xdl_free(cf->rchash);
	xdl_cha_free(&cf->ncha);
}


static int xdl_classify_record(unsigned int pass, xdlclassifier_t *cf, xrecord_t *rec,
			       uint64_t line_hash) {
	size_t hi;
	xdlclass_t *rcrec;

	hi = XDL_HASHLONG(line_hash, cf->hbits);
	for (rcrec = cf->rchash[hi]; rcrec; rcrec = rcrec->next)
		if (rcrec->line_hash == line_hash &&
				xdl_recmatch((const char *)rcrec->ptr, (long)rcrec->size,
					(const char *)rec->ptr, (long)rec->size, cf->flags))
			break;

	if (!rcrec) {
		if (!(rcrec = xdl_cha_alloc(&cf->ncha))) {

			return -1;
		}
		rcrec->idx = cf->count++;
		if (XDL_ALLOC_GROW(cf->rcrecs, cf->count, cf->alloc))
				return -1;
		cf->rcrecs[rcrec->idx] = rcrec;
		rcrec->line_hash = line_hash;
		rcrec->ptr = rec->ptr;
		rcrec->size = rec->size;
		rcrec->len1 = rcrec->len2 = 0;
		rcrec->next = cf->rchash[hi];
		cf->rchash[hi] = rcrec;
	}

	(pass == 1) ? rcrec->len1++ : rcrec->len2++;

	rec->minimal_perfect_hash = (size_t)rcrec->idx;

	return 0;
}


static void xdl_free_ctx(xdfile_t *xdf)
{
	xdl_free(xdf->reference_index);
	xdl_free(xdf->changed - 1);
	xdl_free(xdf->recs);
}


static int xdl_prepare_ctx(unsigned int pass, mmfile_t *mf, long narec, xpparam_t const *xpp,
			   xdlclassifier_t *cf, xdfile_t *xdf) {
	long bsize;
	uint64_t hav;
	uint8_t const *blk, *cur, *top, *prev;
	xrecord_t *crec;

	xdf->reference_index = NULL;
	xdf->changed = NULL;
	xdf->recs = NULL;

	if (!XDL_ALLOC_ARRAY(xdf->recs, narec))
		goto abort;

	xdf->nrec = 0;
	if ((cur = blk = xdl_mmfile_first(mf, &bsize))) {
		for (top = blk + bsize; cur < top; ) {
			prev = cur;
			hav = xdl_hash_record(&cur, top, xpp->flags);
			if (XDL_ALLOC_GROW(xdf->recs, (long)xdf->nrec + 1, narec))
				goto abort;
			crec = &xdf->recs[xdf->nrec++];
			crec->ptr = prev;
			crec->size = cur - prev;
			if (xdl_classify_record(pass, cf, crec, hav) < 0)
				goto abort;
		}
	}

	if (!XDL_CALLOC_ARRAY(xdf->changed, xdf->nrec + 2))
		goto abort;

	if ((XDF_DIFF_ALG(xpp->flags) != XDF_PATIENCE_DIFF) &&
	    (XDF_DIFF_ALG(xpp->flags) != XDF_HISTOGRAM_DIFF)) {
		if (!XDL_ALLOC_ARRAY(xdf->reference_index, xdf->nrec + 1))
			goto abort;
	}

	xdf->changed += 1;
	xdf->nreff = 0;
	xdf->dstart = 0;
	xdf->dend = xdf->nrec - 1;

	return 0;

abort:
	xdl_free_ctx(xdf);
	return -1;
}


void xdl_free_env(xdfenv_t *xe) {

	xdl_free_ctx(&xe->xdf2);
	xdl_free_ctx(&xe->xdf1);
}


static bool xdl_clean_mmatch(uint8_t const *action, long i, long s, long e) {
	long r, rdis0, rpdis0, rdis1, rpdis1;

	/*
	 * Limits the window that is examined during the similar-lines
	 * scan. The loops below stops when action[i - r] == KEEP
	 * (line that has no match), but there are corner cases where
	 * the loop proceed all the way to the extremities by causing
	 * huge performance penalties in case of big files.
	 */
	if (i - s > XDL_SIMSCAN_WINDOW)
		s = i - XDL_SIMSCAN_WINDOW;
	if (e - i > XDL_SIMSCAN_WINDOW)
		e = i + XDL_SIMSCAN_WINDOW;

	/*
	 * Scans the lines before 'i' to find a run of lines that either
	 * have no match (action[j] == DISCARD) or have multiple matches
	 * (action[j] == INVESTIGATE). Note that we always call this
	 * function with action[i] == INVESTIGATE, so the current line
	 * (i) is already a multimatch line.
	 */
	for (r = 1, rdis0 = 0, rpdis0 = 1; (i - r) >= s; r++) {
		if (action[i - r] == DISCARD)
			rdis0++;
		else if (action[i - r] == INVESTIGATE)
			rpdis0++;
		else if (action[i - r] == KEEP)
			break;
		else
			BUG("Illegal value for action[i - r]");
	}
	/*
	 * If the run before the line 'i' found only multimatch lines,
	 * we return false and hence we don't make the current line (i)
	 * discarded. We want to discard multimatch lines only when
	 * they appear in the middle of runs with nomatch lines
	 * (action[j] == DISCARD).
	 */
	if (rdis0 == 0)
		return 0;
	for (r = 1, rdis1 = 0, rpdis1 = 1; (i + r) <= e; r++) {
		if (action[i + r] == DISCARD)
			rdis1++;
		else if (action[i + r] == INVESTIGATE)
			rpdis1++;
		else if (action[i + r] == KEEP)
			break;
		else
			BUG("Illegal value for action[i + r]");
	}
	/*
	 * If the run after the line 'i' found only multimatch lines,
	 * we return false and hence we don't make the current line (i)
	 * discarded.
	 */
	if (rdis1 == 0)
		return false;
	rdis1 += rdis0;
	rpdis1 += rpdis0;

	return rpdis1 * XDL_KPDIS_RUN < (rpdis1 + rdis1);
}


/*
 * Try to reduce the problem complexity, discard records that have no
 * matches on the other file. Also, lines that have multiple matches
 * might be potentially discarded if they appear in a run of discardable.
 */
static int xdl_cleanup_records(xdlclassifier_t *cf, xdfile_t *xdf1, xdfile_t *xdf2) {
	long i, nm, mlim;
	xrecord_t *recs;
	xdlclass_t *rcrec;
	uint8_t *action1 = NULL, *action2 = NULL;
	bool need_min = !!(cf->flags & XDF_NEED_MINIMAL);
	int ret = 0;

	/*
	 * Create temporary arrays that will help us decide if
	 * changed[i] should remain false, or become true.
	 */
	if (!XDL_CALLOC_ARRAY(action1, xdf1->nrec + 1)) {
		ret = -1;
		goto cleanup;
	}
	if (!XDL_CALLOC_ARRAY(action2, xdf2->nrec + 1)) {
		ret = -1;
		goto cleanup;
	}

	/*
	 * Initialize temporary arrays with DISCARD, KEEP, or INVESTIGATE.
	 */
	if ((mlim = xdl_bogosqrt((long)xdf1->nrec)) > XDL_MAX_EQLIMIT)
		mlim = XDL_MAX_EQLIMIT;
	for (i = xdf1->dstart, recs = &xdf1->recs[xdf1->dstart]; i <= xdf1->dend; i++, recs++) {
		rcrec = cf->rcrecs[recs->minimal_perfect_hash];
		nm = rcrec ? rcrec->len2 : 0;
		action1[i] = (nm == 0) ? DISCARD: (nm >= mlim && !need_min) ? INVESTIGATE: KEEP;
	}

	if ((mlim = xdl_bogosqrt((long)xdf2->nrec)) > XDL_MAX_EQLIMIT)
		mlim = XDL_MAX_EQLIMIT;
	for (i = xdf2->dstart, recs = &xdf2->recs[xdf2->dstart]; i <= xdf2->dend; i++, recs++) {
		rcrec = cf->rcrecs[recs->minimal_perfect_hash];
		nm = rcrec ? rcrec->len1 : 0;
		action2[i] = (nm == 0) ? DISCARD: (nm >= mlim && !need_min) ? INVESTIGATE: KEEP;
	}

	/*
	 * Use temporary arrays to decide if changed[i] should remain
	 * false, or become true.
	 */
	xdf1->nreff = 0;
	for (i = xdf1->dstart, recs = &xdf1->recs[xdf1->dstart];
	     i <= xdf1->dend; i++, recs++) {
		if (action1[i] == KEEP ||
		    (action1[i] == INVESTIGATE && !xdl_clean_mmatch(action1, i, xdf1->dstart, xdf1->dend))) {
			xdf1->reference_index[xdf1->nreff++] = i;
			/* changed[i] remains false, i.e. keep */
		} else
			xdf1->changed[i] = true;
			/* i.e. discard */
	}

	xdf2->nreff = 0;
	for (i = xdf2->dstart, recs = &xdf2->recs[xdf2->dstart];
	     i <= xdf2->dend; i++, recs++) {
		if (action2[i] == KEEP ||
		    (action2[i] == INVESTIGATE && !xdl_clean_mmatch(action2, i, xdf2->dstart, xdf2->dend))) {
			xdf2->reference_index[xdf2->nreff++] = i;
			/* changed[i] remains false, i.e. keep */
		} else
			xdf2->changed[i] = true;
			/* i.e. discard */
	}

cleanup:
	xdl_free(action1);
	xdl_free(action2);

	return ret;
}


/*
 * Early trim initial and terminal matching records.
 */
static int xdl_trim_ends(xdfile_t *xdf1, xdfile_t *xdf2) {
	long i, lim;
	xrecord_t *recs1, *recs2;

	recs1 = xdf1->recs;
	recs2 = xdf2->recs;
	for (i = 0, lim = (long)XDL_MIN(xdf1->nrec, xdf2->nrec); i < lim;
	     i++, recs1++, recs2++)
		if (recs1->minimal_perfect_hash != recs2->minimal_perfect_hash)
			break;

	xdf1->dstart = xdf2->dstart = i;

	recs1 = xdf1->recs + xdf1->nrec - 1;
	recs2 = xdf2->recs + xdf2->nrec - 1;
	for (lim -= i, i = 0; i < lim; i++, recs1--, recs2--)
		if (recs1->minimal_perfect_hash != recs2->minimal_perfect_hash)
			break;

	xdf1->dend = (long)xdf1->nrec - i - 1;
	xdf2->dend = (long)xdf2->nrec - i - 1;

	return 0;
}


static int xdl_optimize_ctxs(xdlclassifier_t *cf, xdfile_t *xdf1, xdfile_t *xdf2) {

	if (xdl_trim_ends(xdf1, xdf2) < 0 ||
	    xdl_cleanup_records(cf, xdf1, xdf2) < 0) {

		return -1;
	}

	return 0;
}

int xdl_prepare_env(mmfile_t *mf1, mmfile_t *mf2, xpparam_t const *xpp,
		    xdfenv_t *xe) {
	long enl1, enl2, sample;
	xdlclassifier_t cf;

	memset(&cf, 0, sizeof(cf));

	/*
	 * For histogram diff, we can afford a smaller sample size and
	 * thus a poorer estimate of the number of lines, as the hash
	 * table (rhash) won't be filled up/grown. The number of lines
	 * (nrecs) will be updated correctly anyway by
	 * xdl_prepare_ctx().
	 */
	sample = (XDF_DIFF_ALG(xpp->flags) == XDF_HISTOGRAM_DIFF
		  ? XDL_GUESS_NLINES2 : XDL_GUESS_NLINES1);

	enl1 = xdl_guess_lines(mf1, sample) + 1;
	enl2 = xdl_guess_lines(mf2, sample) + 1;

	if (xdl_init_classifier(&cf, enl1 + enl2 + 1, xpp->flags) < 0)
		return -1;

	if (xdl_prepare_ctx(1, mf1, enl1, xpp, &cf, &xe->xdf1) < 0) {

		xdl_free_classifier(&cf);
		return -1;
	}
	if (xdl_prepare_ctx(2, mf2, enl2, xpp, &cf, &xe->xdf2) < 0) {

		xdl_free_ctx(&xe->xdf1);
		xdl_free_classifier(&cf);
		return -1;
	}

	if ((XDF_DIFF_ALG(xpp->flags) != XDF_PATIENCE_DIFF) &&
	    (XDF_DIFF_ALG(xpp->flags) != XDF_HISTOGRAM_DIFF) &&
	    xdl_optimize_ctxs(&cf, &xe->xdf1, &xe->xdf2) < 0) {

		xdl_free_ctx(&xe->xdf2);
		xdl_free_ctx(&xe->xdf1);
		xdl_free_classifier(&cf);
		return -1;
	}

	xdl_free_classifier(&cf);

	return 0;
}
