/*
 *  LibXDiff by Davide Libenzi ( File Differential Library )
 *  Copyright (C) 2003-2006 Davide Libenzi, Johannes E. Schindelin
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

typedef struct s_xdmerge {
	struct s_xdmerge *next;
	/*
	 * 0 = conflict,
	 * 1 = no conflict, take first,
	 * 2 = no conflict, take second.
	 */
	int mode;
	long i1, i2;
	long chg1, chg2;
} xdmerge_t;

static int xdl_append_merge(xdmerge_t **merge, int mode,
		long i1, long chg1, long i2, long chg2)
{
	xdmerge_t *m = *merge;
	if (m && mode == m->mode &&
			(i1 == m->i1 + m->chg1 || i2 == m->i2 + m->chg2)) {
		m->chg1 = i1 + chg1 - m->i1;
		m->chg2 = i2 + chg2 - m->i2;
	} else {
		m = xdl_malloc(sizeof(xdmerge_t));
		if (!m)
			return -1;
		m->next = NULL;
		m->mode = mode;
		m->i1 = i1;
		m->chg1 = chg1;
		m->i2 = i2;
		m->chg2 = chg2;
		if (*merge)
			(*merge)->next = m;
		*merge = m;
	}
	return 0;
}

static int xdl_cleanup_merge(xdmerge_t *c)
{
	int count = 0;
	xdmerge_t *next_c;

	/* were there conflicts? */
	for (; c; c = next_c) {
		if (c->mode == 0)
			count++;
		next_c = c->next;
		free(c);
	}
	return count;
}

static int xdl_merge_cmp_lines(xdfenv_t *xe1, int i1, xdfenv_t *xe2, int i2,
		int line_count, long flags)
{
	int i;
	xrecord_t **rec1 = xe1->xdf2.recs + i1;
	xrecord_t **rec2 = xe2->xdf2.recs + i2;

	for (i = 0; i < line_count; i++) {
		int result = xdl_recmatch(rec1[i]->ptr, rec1[i]->size,
			rec2[i]->ptr, rec2[i]->size, flags);
		if (!result)
			return -1;
	}
	return 0;
}

static int xdl_recs_copy(xdfenv_t *xe, int i, int count, int add_nl, char *dest)
{
	xrecord_t **recs = xe->xdf2.recs + i;
	int size = 0;

	if (count < 1)
		return 0;

	for (i = 0; i < count; size += recs[i++]->size)
		if (dest)
			memcpy(dest + size, recs[i]->ptr, recs[i]->size);
	if (add_nl) {
		i = recs[count - 1]->size;
		if (i == 0 || recs[count - 1]->ptr[i - 1] != '\n') {
			if (dest)
				dest[size] = '\n';
			size++;
		}
	}
	return size;
}

static int xdl_fill_merge_buffer(xdfenv_t *xe1, const char *name1,
		xdfenv_t *xe2, const char *name2, xdmerge_t *m, char *dest)
{
	const int marker_size = 7;
	int marker1_size = (name1 ? strlen(name1) + 1 : 0);
	int marker2_size = (name2 ? strlen(name2) + 1 : 0);
	int conflict_marker_size = 3 * (marker_size + 1)
		+ marker1_size + marker2_size;
	int size, i1, j;

	for (size = i1 = 0; m; m = m->next) {
		if (m->mode == 0) {
			size += xdl_recs_copy(xe1, i1, m->i1 - i1, 0,
					dest ? dest + size : NULL);
			if (dest) {
				for (j = 0; j < marker_size; j++)
					dest[size++] = '<';
				if (marker1_size) {
					dest[size] = ' ';
					memcpy(dest + size + 1, name1,
							marker1_size - 1);
					size += marker1_size;
				}
				dest[size++] = '\n';
			} else
				size += conflict_marker_size;
			size += xdl_recs_copy(xe1, m->i1, m->chg1, 1,
					dest ? dest + size : NULL);
			if (dest) {
				for (j = 0; j < marker_size; j++)
					dest[size++] = '=';
				dest[size++] = '\n';
			}
			size += xdl_recs_copy(xe2, m->i2, m->chg2, 1,
					dest ? dest + size : NULL);
			if (dest) {
				for (j = 0; j < marker_size; j++)
					dest[size++] = '>';
				if (marker2_size) {
					dest[size] = ' ';
					memcpy(dest + size + 1, name2,
							marker2_size - 1);
					size += marker2_size;
				}
				dest[size++] = '\n';
			}
		} else if (m->mode == 1)
			size += xdl_recs_copy(xe1, i1, m->i1 + m->chg1 - i1, 0,
					dest ? dest + size : NULL);
		else if (m->mode == 2)
			size += xdl_recs_copy(xe2, m->i2 - m->i1 + i1,
					m->i1 + m->chg2 - i1, 0,
					dest ? dest + size : NULL);
		i1 = m->i1 + m->chg1;
	}
	size += xdl_recs_copy(xe1, i1, xe1->xdf2.nrec - i1, 0,
			dest ? dest + size : NULL);
	return size;
}

/*
 * Sometimes, changes are not quite identical, but differ in only a few
 * lines. Try hard to show only these few lines as conflicting.
 */
static int xdl_refine_conflicts(xdfenv_t *xe1, xdfenv_t *xe2, xdmerge_t *m,
		xpparam_t const *xpp)
{
	for (; m; m = m->next) {
		mmfile_t t1, t2;
		xdfenv_t xe;
		xdchange_t *xscr, *x;
		int i1 = m->i1, i2 = m->i2;

		/* let's handle just the conflicts */
		if (m->mode)
			continue;

		/*
		 * This probably does not work outside git, since
		 * we have a very simple mmfile structure.
		 */
		t1.ptr = (char *)xe1->xdf2.recs[m->i1]->ptr;
		t1.size = xe1->xdf2.recs[m->i1 + m->chg1 - 1]->ptr
			+ xe1->xdf2.recs[m->i1 + m->chg1 - 1]->size - t1.ptr;
		t2.ptr = (char *)xe2->xdf2.recs[m->i2]->ptr;
		t2.size = xe2->xdf2.recs[m->i2 + m->chg2 - 1]->ptr
			+ xe2->xdf2.recs[m->i2 + m->chg2 - 1]->size - t2.ptr;
		if (xdl_do_diff(&t1, &t2, xpp, &xe) < 0)
			return -1;
		if (xdl_change_compact(&xe.xdf1, &xe.xdf2, xpp->flags) < 0 ||
		    xdl_change_compact(&xe.xdf2, &xe.xdf1, xpp->flags) < 0 ||
		    xdl_build_script(&xe, &xscr) < 0) {
			xdl_free_env(&xe);
			return -1;
		}
		if (!xscr) {
			/* If this happens, it's a bug. */
			xdl_free_env(&xe);
			return -2;
		}
		x = xscr;
		m->i1 = xscr->i1 + i1;
		m->chg1 = xscr->chg1;
		m->i2 = xscr->i2 + i2;
		m->chg2 = xscr->chg2;
		while (xscr->next) {
			xdmerge_t *m2 = xdl_malloc(sizeof(xdmerge_t));
			if (!m2) {
				xdl_free_env(&xe);
				xdl_free_script(x);
				return -1;
			}
			xscr = xscr->next;
			m2->next = m->next;
			m->next = m2;
			m = m2;
			m->mode = 0;
			m->i1 = xscr->i1 + i1;
			m->chg1 = xscr->chg1;
			m->i2 = xscr->i2 + i2;
			m->chg2 = xscr->chg2;
		}
		xdl_free_env(&xe);
		xdl_free_script(x);
	}
	return 0;
}

/*
 * level == 0: mark all overlapping changes as conflict
 * level == 1: mark overlapping changes as conflict only if not identical
 * level == 2: analyze non-identical changes for minimal conflict set
 *
 * returns < 0 on error, == 0 for no conflicts, else number of conflicts
 */
static int xdl_do_merge(xdfenv_t *xe1, xdchange_t *xscr1, const char *name1,
		xdfenv_t *xe2, xdchange_t *xscr2, const char *name2,
		int level, xpparam_t const *xpp, mmbuffer_t *result) {
	xdmerge_t *changes, *c;
	int i1, i2, chg1, chg2;

	c = changes = NULL;

	while (xscr1 && xscr2) {
		if (!changes)
			changes = c;
		if (xscr1->i1 + xscr1->chg1 < xscr2->i1) {
			i1 = xscr1->i2;
			i2 = xscr2->i2 - xscr2->i1 + xscr1->i1;
			chg1 = xscr1->chg2;
			chg2 = xscr1->chg1;
			if (xdl_append_merge(&c, 1, i1, chg1, i2, chg2)) {
				xdl_cleanup_merge(changes);
				return -1;
			}
			xscr1 = xscr1->next;
			continue;
		}
		if (xscr2->i1 + xscr2->chg1 < xscr1->i1) {
			i1 = xscr1->i2 - xscr1->i1 + xscr2->i1;
			i2 = xscr2->i2;
			chg1 = xscr2->chg1;
			chg2 = xscr2->chg2;
			if (xdl_append_merge(&c, 2, i1, chg1, i2, chg2)) {
				xdl_cleanup_merge(changes);
				return -1;
			}
			xscr2 = xscr2->next;
			continue;
		}
		if (level < 1 || xscr1->i1 != xscr2->i1 ||
				xscr1->chg1 != xscr2->chg1 ||
				xscr1->chg2 != xscr2->chg2 ||
				xdl_merge_cmp_lines(xe1, xscr1->i2,
					xe2, xscr2->i2,
					xscr1->chg2, xpp->flags)) {
			/* conflict */
			int off = xscr1->i1 - xscr2->i1;
			int ffo = off + xscr1->chg1 - xscr2->chg1;

			i1 = xscr1->i2;
			i2 = xscr2->i2;
			if (off > 0)
				i1 -= off;
			else
				i2 += off;
			chg1 = xscr1->i2 + xscr1->chg2 - i1;
			chg2 = xscr2->i2 + xscr2->chg2 - i2;
			if (ffo > 0)
				chg2 += ffo;
			else
				chg1 -= ffo;
			if (xdl_append_merge(&c, 0, i1, chg1, i2, chg2)) {
				xdl_cleanup_merge(changes);
				return -1;
			}
		}

		i1 = xscr1->i1 + xscr1->chg1;
		i2 = xscr2->i1 + xscr2->chg1;

		if (i1 > i2) {
			xscr1->chg1 -= i1 - i2;
			xscr1->i1 = i2;
			xscr1->i2 += xscr1->chg2;
			xscr1->chg2 = 0;
			xscr2 = xscr2->next;
		} else if (i2 > i1) {
			xscr2->chg1 -= i2 - i1;
			xscr2->i1 = i1;
			xscr2->i2 += xscr2->chg2;
			xscr2->chg2 = 0;
			xscr1 = xscr1->next;
		} else {
			xscr1 = xscr1->next;
			xscr2 = xscr2->next;
		}
	}
	while (xscr1) {
		if (!changes)
			changes = c;
		i1 = xscr1->i2;
		i2 = xscr1->i1 + xe2->xdf2.nrec - xe2->xdf1.nrec;
		chg1 = xscr1->chg2;
		chg2 = xscr1->chg1;
		if (xdl_append_merge(&c, 1, i1, chg1, i2, chg2)) {
			xdl_cleanup_merge(changes);
			return -1;
		}
		xscr1 = xscr1->next;
	}
	while (xscr2) {
		if (!changes)
			changes = c;
		i1 = xscr2->i1 + xe1->xdf2.nrec - xe1->xdf1.nrec;
		i2 = xscr2->i2;
		chg1 = xscr2->chg1;
		chg2 = xscr2->chg2;
		if (xdl_append_merge(&c, 2, i1, chg1, i2, chg2)) {
			xdl_cleanup_merge(changes);
			return -1;
		}
		xscr2 = xscr2->next;
	}
	if (!changes)
		changes = c;
	/* refine conflicts */
	if (level > 1 && xdl_refine_conflicts(xe1, xe2, changes, xpp) < 0) {
		xdl_cleanup_merge(changes);
		return -1;
	}
	/* output */
	if (result) {
		int size = xdl_fill_merge_buffer(xe1, name1, xe2, name2,
			changes, NULL);
		result->ptr = xdl_malloc(size);
		if (!result->ptr) {
			xdl_cleanup_merge(changes);
			return -1;
		}
		result->size = size;
		xdl_fill_merge_buffer(xe1, name1, xe2, name2, changes,
				result->ptr);
	}
	return xdl_cleanup_merge(changes);
}

int xdl_merge(mmfile_t *orig, mmfile_t *mf1, const char *name1,
		mmfile_t *mf2, const char *name2,
		xpparam_t const *xpp, int level, mmbuffer_t *result) {
	xdchange_t *xscr1, *xscr2;
	xdfenv_t xe1, xe2;
	int status;

	result->ptr = NULL;
	result->size = 0;

	if (xdl_do_diff(orig, mf1, xpp, &xe1) < 0 ||
			xdl_do_diff(orig, mf2, xpp, &xe2) < 0) {
		return -1;
	}
	if (xdl_change_compact(&xe1.xdf1, &xe1.xdf2, xpp->flags) < 0 ||
	    xdl_change_compact(&xe1.xdf2, &xe1.xdf1, xpp->flags) < 0 ||
	    xdl_build_script(&xe1, &xscr1) < 0) {
		xdl_free_env(&xe1);
		return -1;
	}
	if (xdl_change_compact(&xe2.xdf1, &xe2.xdf2, xpp->flags) < 0 ||
	    xdl_change_compact(&xe2.xdf2, &xe2.xdf1, xpp->flags) < 0 ||
	    xdl_build_script(&xe2, &xscr2) < 0) {
		xdl_free_env(&xe2);
		return -1;
	}
	status = 0;
	if (xscr1 || xscr2) {
		if (!xscr1) {
			result->ptr = xdl_malloc(mf2->size);
			memcpy(result->ptr, mf2->ptr, mf2->size);
			result->size = mf2->size;
		} else if (!xscr2) {
			result->ptr = xdl_malloc(mf1->size);
			memcpy(result->ptr, mf1->ptr, mf1->size);
			result->size = mf1->size;
		} else {
			status = xdl_do_merge(&xe1, xscr1, name1,
					      &xe2, xscr2, name2,
					      level, xpp, result);
		}
		xdl_free_script(xscr1);
		xdl_free_script(xscr2);
	}
	xdl_free_env(&xe1);
	xdl_free_env(&xe2);

	return status;
}
