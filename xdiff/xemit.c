/*
 *  LibXDiff by Davide Libenzi ( File Differential Library )
 *  Copyright (C) 2003	Davide Libenzi
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
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include "xinclude.h"




static long xdl_get_rec(xdfile_t *xdf, long ri, char const **rec);
static int xdl_emit_record(xdfile_t *xdf, long ri, char const *pre, xdemitcb_t *ecb);




static long xdl_get_rec(xdfile_t *xdf, long ri, char const **rec) {

	*rec = xdf->recs[ri]->ptr;

	return xdf->recs[ri]->size;
}


static int xdl_emit_record(xdfile_t *xdf, long ri, char const *pre, xdemitcb_t *ecb) {
	long size, psize = strlen(pre);
	char const *rec;

	size = xdl_get_rec(xdf, ri, &rec);
	if (xdl_emit_diffrec(rec, size, pre, psize, ecb) < 0) {

		return -1;
	}

	return 0;
}


/*
 * Starting at the passed change atom, find the latest change atom to be included
 * inside the differential hunk according to the specified configuration.
 * Also advance xscr if the first changes must be discarded.
 */
xdchange_t *xdl_get_hunk(xdchange_t **xscr, xdemitconf_t const *xecfg)
{
	xdchange_t *xch, *xchp, *lxch;
	long max_common = 2 * xecfg->ctxlen + xecfg->interhunkctxlen;
	long max_ignorable = xecfg->ctxlen;
	unsigned long ignored = 0; /* number of ignored blank lines */

	/* remove ignorable changes that are too far before other changes */
	for (xchp = *xscr; xchp && xchp->ignore; xchp = xchp->next) {
		xch = xchp->next;

		if (xch == NULL ||
		    xch->i1 - (xchp->i1 + xchp->chg1) >= max_ignorable)
			*xscr = xch;
	}

	if (*xscr == NULL)
		return NULL;

	lxch = *xscr;

	for (xchp = *xscr, xch = xchp->next; xch; xchp = xch, xch = xch->next) {
		long distance = xch->i1 - (xchp->i1 + xchp->chg1);
		if (distance > max_common)
			break;

		if (distance < max_ignorable && (!xch->ignore || lxch == xchp)) {
			lxch = xch;
			ignored = 0;
		} else if (distance < max_ignorable && xch->ignore) {
			ignored += xch->chg2;
		} else if (lxch != xchp &&
			   xch->i1 + ignored - (lxch->i1 + lxch->chg1) > max_common) {
			break;
		} else if (!xch->ignore) {
			lxch = xch;
			ignored = 0;
		} else {
			ignored += xch->chg2;
		}
	}

	return lxch;
}


static long def_ff(const char *rec, long len, char *buf, long sz, void *priv)
{
	if (len > 0 &&
			(isalpha((unsigned char)*rec) || /* identifier? */
			 *rec == '_' || /* also identifier? */
			 *rec == '$')) { /* identifiers from VMS and other esoterico */
		if (len > sz)
			len = sz;
		while (0 < len && isspace((unsigned char)rec[len - 1]))
			len--;
		memcpy(buf, rec, len);
		return len;
	}
	return -1;
}

static int xdl_emit_common(xdfenv_t *xe, xdchange_t *xscr, xdemitcb_t *ecb,
                           xdemitconf_t const *xecfg) {
	xdfile_t *xdf = &xe->xdf2;
	const char *rchg = xdf->rchg;
	long ix;

	for (ix = 0; ix < xdf->nrec; ix++) {
		if (rchg[ix])
			continue;
		if (xdl_emit_record(xdf, ix, "", ecb))
			return -1;
	}
	return 0;
}

struct func_line {
	long len;
	char buf[80];
};

static long get_func_line(xdfenv_t *xe, xdemitconf_t const *xecfg,
			  struct func_line *func_line, long start, long limit)
{
	find_func_t ff = xecfg->find_func ? xecfg->find_func : def_ff;
	long l, size, step = (start > limit) ? -1 : 1;
	char *buf, dummy[1];

	buf = func_line ? func_line->buf : dummy;
	size = func_line ? sizeof(func_line->buf) : sizeof(dummy);

	for (l = start; l != limit && 0 <= l && l < xe->xdf1.nrec; l += step) {
		const char *rec;
		long reclen = xdl_get_rec(&xe->xdf1, l, &rec);
		long len = ff(rec, reclen, buf, size, xecfg->find_func_priv);
		if (len >= 0) {
			if (func_line)
				func_line->len = len;
			return l;
		}
	}
	return -1;
}

int xdl_emit_diff(xdfenv_t *xe, xdchange_t *xscr, xdemitcb_t *ecb,
		  xdemitconf_t const *xecfg) {
	long s1, s2, e1, e2, lctx;
	xdchange_t *xch, *xche;
	long funclineprev = -1;
	struct func_line func_line = { 0 };

	if (xecfg->flags & XDL_EMIT_COMMON)
		return xdl_emit_common(xe, xscr, ecb, xecfg);

	for (xch = xscr; xch; xch = xche->next) {
		xche = xdl_get_hunk(&xch, xecfg);
		if (!xch)
			break;

		s1 = XDL_MAX(xch->i1 - xecfg->ctxlen, 0);
		s2 = XDL_MAX(xch->i2 - xecfg->ctxlen, 0);

		if (xecfg->flags & XDL_EMIT_FUNCCONTEXT) {
			long fs1 = get_func_line(xe, xecfg, NULL, xch->i1, -1);
			if (fs1 < 0)
				fs1 = 0;
			if (fs1 < s1) {
				s2 -= s1 - fs1;
				s1 = fs1;
			}
		}

 again:
		lctx = xecfg->ctxlen;
		lctx = XDL_MIN(lctx, xe->xdf1.nrec - (xche->i1 + xche->chg1));
		lctx = XDL_MIN(lctx, xe->xdf2.nrec - (xche->i2 + xche->chg2));

		e1 = xche->i1 + xche->chg1 + lctx;
		e2 = xche->i2 + xche->chg2 + lctx;

		if (xecfg->flags & XDL_EMIT_FUNCCONTEXT) {
			long fe1 = get_func_line(xe, xecfg, NULL,
						 xche->i1 + xche->chg1,
						 xe->xdf1.nrec);
			if (fe1 < 0)
				fe1 = xe->xdf1.nrec;
			if (fe1 > e1) {
				e2 += fe1 - e1;
				e1 = fe1;
			}

			/*
			 * Overlap with next change?  Then include it
			 * in the current hunk and start over to find
			 * its new end.
			 */
			if (xche->next) {
				long l = xche->next->i1;
				if (l <= e1 ||
				    get_func_line(xe, xecfg, NULL, l, e1) < 0) {
					xche = xche->next;
					goto again;
				}
			}
		}

		/*
		 * Emit current hunk header.
		 */

		if (xecfg->flags & XDL_EMIT_FUNCNAMES) {
			get_func_line(xe, xecfg, &func_line,
				      s1 - 1, funclineprev);
			funclineprev = s1 - 1;
		}
		if (xdl_emit_hunk_hdr(s1 + 1, e1 - s1, s2 + 1, e2 - s2,
				      func_line.buf, func_line.len, ecb) < 0)
			return -1;

		/*
		 * Emit pre-context.
		 */
		for (; s2 < xch->i2; s2++)
			if (xdl_emit_record(&xe->xdf2, s2, " ", ecb) < 0)
				return -1;

		for (s1 = xch->i1, s2 = xch->i2;; xch = xch->next) {
			/*
			 * Merge previous with current change atom.
			 */
			for (; s1 < xch->i1 && s2 < xch->i2; s1++, s2++)
				if (xdl_emit_record(&xe->xdf2, s2, " ", ecb) < 0)
					return -1;

			/*
			 * Removes lines from the first file.
			 */
			for (s1 = xch->i1; s1 < xch->i1 + xch->chg1; s1++)
				if (xdl_emit_record(&xe->xdf1, s1, "-", ecb) < 0)
					return -1;

			/*
			 * Adds lines from the second file.
			 */
			for (s2 = xch->i2; s2 < xch->i2 + xch->chg2; s2++)
				if (xdl_emit_record(&xe->xdf2, s2, "+", ecb) < 0)
					return -1;

			if (xch == xche)
				break;
			s1 = xch->i1 + xch->chg1;
			s2 = xch->i2 + xch->chg2;
		}

		/*
		 * Emit post-context.
		 */
		for (s2 = xche->i2 + xche->chg2; s2 < e2; s2++)
			if (xdl_emit_record(&xe->xdf2, s2, " ", ecb) < 0)
				return -1;
	}

	return 0;
}
