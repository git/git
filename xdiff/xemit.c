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
 */
xdchange_t *xdl_get_hunk(xdchange_t *xscr, xdemitconf_t const *xecfg) {
	xdchange_t *xch, *xchp;
	long max_common = 2 * xecfg->ctxlen + xecfg->interhunkctxlen;

	for (xchp = xscr, xch = xscr->next; xch; xchp = xch, xch = xch->next)
		if (xch->i1 - (xchp->i1 + xchp->chg1) > max_common)
			break;

	return xchp;
}


static long def_ff(const char *rec, long len, char *buf, long sz, void *priv)
{
	if (len > 0 &&
			(isalpha((unsigned char)*rec) || /* identifier? */
			 *rec == '_' ||	/* also identifier? */
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

static void xdl_find_func(xdfile_t *xf, long i, char *buf, long sz, long *ll,
		find_func_t ff, void *ff_priv) {

	/*
	 * Be quite stupid about this for now.  Find a line in the old file
	 * before the start of the hunk (and context) which starts with a
	 * plausible character.
	 */

	const char *rec;
	long len;

	while (i-- > 0) {
		len = xdl_get_rec(xf, i, &rec);
		if ((*ll = ff(rec, len, buf, sz, ff_priv)) >= 0)
			return;
	}
	*ll = 0;
}


static int xdl_emit_common(xdfenv_t *xe, xdchange_t *xscr, xdemitcb_t *ecb,
                           xdemitconf_t const *xecfg) {
	xdfile_t *xdf = &xe->xdf1;
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

int xdl_emit_diff(xdfenv_t *xe, xdchange_t *xscr, xdemitcb_t *ecb,
		  xdemitconf_t const *xecfg) {
	long s1, s2, e1, e2, lctx;
	xdchange_t *xch, *xche;
	char funcbuf[80];
	long funclen = 0;
	find_func_t ff = xecfg->find_func ?  xecfg->find_func : def_ff;

	if (xecfg->flags & XDL_EMIT_COMMON)
		return xdl_emit_common(xe, xscr, ecb, xecfg);

	for (xch = xscr; xch; xch = xche->next) {
		xche = xdl_get_hunk(xch, xecfg);

		s1 = XDL_MAX(xch->i1 - xecfg->ctxlen, 0);
		s2 = XDL_MAX(xch->i2 - xecfg->ctxlen, 0);

		lctx = xecfg->ctxlen;
		lctx = XDL_MIN(lctx, xe->xdf1.nrec - (xche->i1 + xche->chg1));
		lctx = XDL_MIN(lctx, xe->xdf2.nrec - (xche->i2 + xche->chg2));

		e1 = xche->i1 + xche->chg1 + lctx;
		e2 = xche->i2 + xche->chg2 + lctx;

		/*
		 * Emit current hunk header.
		 */

		if (xecfg->flags & XDL_EMIT_FUNCNAMES) {
			xdl_find_func(&xe->xdf1, s1, funcbuf,
				      sizeof(funcbuf), &funclen,
				      ff, xecfg->find_func_priv);
		}
		if (xdl_emit_hunk_hdr(s1 + 1, e1 - s1, s2 + 1, e2 - s2,
				      funcbuf, funclen, ecb) < 0)
			return -1;

		/*
		 * Emit pre-context.
		 */
		for (; s1 < xch->i1; s1++)
			if (xdl_emit_record(&xe->xdf1, s1, " ", ecb) < 0)
				return -1;

		for (s1 = xch->i1, s2 = xch->i2;; xch = xch->next) {
			/*
			 * Merge previous with current change atom.
			 */
			for (; s1 < xch->i1 && s2 < xch->i2; s1++, s2++)
				if (xdl_emit_record(&xe->xdf1, s1, " ", ecb) < 0)
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
		for (s1 = xche->i1 + xche->chg1; s1 < e1; s1++)
			if (xdl_emit_record(&xe->xdf1, s1, " ", ecb) < 0)
				return -1;
	}

	return 0;
}
