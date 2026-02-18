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
#include "compat/ivec.h"


typedef struct s_xdlclass {
	struct s_xdlclass *next;
	xrecord_t rec;
	long idx;
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
	memset(cf, 0, sizeof(xdlclassifier_t));

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


static int xdl_classify_record(xdlclassifier_t *cf, xrecord_t *rec) {
	size_t hi;
	xdlclass_t *rcrec;

	hi = XDL_HASHLONG(rec->line_hash, cf->hbits);
	for (rcrec = cf->rchash[hi]; rcrec; rcrec = rcrec->next)
		if (rcrec->rec.line_hash == rec->line_hash &&
				xdl_recmatch((const char *)rcrec->rec.ptr, (long)rcrec->rec.size,
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
		rcrec->rec = *rec;
		rcrec->next = cf->rchash[hi];
		cf->rchash[hi] = rcrec;
	}

	rec->minimal_perfect_hash = (size_t)rcrec->idx;

	return 0;
}


static void xdl_free_ctx(xdfile_t *xdf)
{
	xdl_free(xdf->reference_index);
	xdl_free(xdf->changed - 1);
	xdl_free(xdf->recs);
}


static int xdl_prepare_ctx(mmfile_t *mf, xdfile_t *xdf, uint64_t flags) {
	long bsize;
	uint64_t hav;
	uint8_t const *blk, *cur, *top, *prev;
	xrecord_t *crec;
	long narec = 8;

	xdf->reference_index = NULL;
	xdf->changed = NULL;
	xdf->recs = NULL;

	if (!XDL_ALLOC_ARRAY(xdf->recs, narec))
		goto abort;

	xdf->nrec = 0;
	if ((cur = blk = xdl_mmfile_first(mf, &bsize))) {
		for (top = blk + bsize; cur < top; ) {
			prev = cur;
			hav = xdl_hash_record(&cur, top, flags);
			if (XDL_ALLOC_GROW(xdf->recs, (long)xdf->nrec + 1, narec))
				goto abort;
			crec = &xdf->recs[xdf->nrec++];
			crec->ptr = prev;
			crec->size = cur - prev;
			crec->line_hash = hav;
		}
	}

	if (!XDL_CALLOC_ARRAY(xdf->changed, xdf->nrec + 2))
		goto abort;

	if ((XDF_DIFF_ALG(flags) != XDF_PATIENCE_DIFF) &&
	    (XDF_DIFF_ALG(flags) != XDF_HISTOGRAM_DIFF)) {
		if (!XDL_ALLOC_ARRAY(xdf->reference_index, xdf->nrec + 1))
			goto abort;
	}

	xdf->changed += 1;
	xdf->nreff = 0;

	return 0;

abort:
	xdl_free_ctx(xdf);
	return -1;
}


void xdl_free_env(xdfenv_t *xe) {

	xdl_free_ctx(&xe->xdf2);
	xdl_free_ctx(&xe->xdf1);
}


/*
 * Early trim initial and terminal matching records.
 */
static void xdl_trim_ends(xdfenv_t *xe)
{
	size_t lim = XDL_MIN(xe->xdf1.nrec, xe->xdf2.nrec);

	for (size_t i = 0; i < lim; i++) {
		size_t mph1 = xe->xdf1.recs[i].minimal_perfect_hash;
		size_t mph2 = xe->xdf2.recs[i].minimal_perfect_hash;
		if (mph1 != mph2) {
			xe->delta_start = (ssize_t)i;
			lim -= i;
			break;
		}
	}

	for (size_t i = 0; i < lim; i++) {
		size_t mph1 = xe->xdf1.recs[xe->xdf1.nrec - 1 - i].minimal_perfect_hash;
		size_t mph2 = xe->xdf2.recs[xe->xdf2.nrec - 1 - i].minimal_perfect_hash;
		if (mph1 != mph2) {
			xe->delta_end = i;
			break;
		}
	}
}


int xdl_prepare_env(mmfile_t *mf1, mmfile_t *mf2, xpparam_t const *xpp,
		    xdfenv_t *xe) {
	xdlclassifier_t cf;

	xe->delta_start = 0;
	xe->delta_end = 0;

	if (xdl_prepare_ctx(mf1, &xe->xdf1, xpp->flags) < 0) {

		return -1;
	}
	if (xdl_prepare_ctx(mf2, &xe->xdf2, xpp->flags) < 0) {

		xdl_free_ctx(&xe->xdf1);
		return -1;
	}

	if (xdl_init_classifier(&cf, xe->xdf1.nrec + xe->xdf2.nrec + 1, xpp->flags) < 0)
		return -1;

	for (size_t i = 0; i < xe->xdf1.nrec; i++) {
		xrecord_t *rec = &xe->xdf1.recs[i];
		xdl_classify_record(&cf, rec);
	}

	for (size_t i = 0; i < xe->xdf2.nrec; i++) {
		xrecord_t *rec = &xe->xdf2.recs[i];
		xdl_classify_record(&cf, rec);
	}

	xe->mph_size = cf.count;
	xdl_free_classifier(&cf);

	xdl_trim_ends(xe);

	return 0;
}
