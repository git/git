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

#if !defined(XUTILS_H)
#define XUTILS_H



long xdl_bogosqrt(long n);
int xdl_emit_diffrec(char const *rec, long size, char const *pre, long psize,
		     xdemitcb_t *ecb);
int xdl_cha_init(chastore_t *cha, long isize, long icount);
void xdl_cha_free(chastore_t *cha);
void *xdl_cha_alloc(chastore_t *cha);
long xdl_guess_lines(mmfile_t *mf, long sample);
int xdl_blankline(const char *line, long size, long flags);
int xdl_recmatch(const char *l1, long s1, const char *l2, long s2, long flags);
unsigned long xdl_hash_record_verbatim(char const **data, char const *top);
unsigned long xdl_hash_record_with_whitespace(char const **data, char const *top, long flags);
static inline unsigned long xdl_hash_record(char const **data, char const *top, long flags)
{
	if (flags & XDF_WHITESPACE_FLAGS)
		return xdl_hash_record_with_whitespace(data, top, flags);
	else
		return xdl_hash_record_verbatim(data, top);
}
unsigned int xdl_hashbits(unsigned int size);
int xdl_num_out(char *out, long val);
int xdl_emit_hunk_hdr(long s1, long c1, long s2, long c2,
		      const char *func, long funclen, xdemitcb_t *ecb);
int xdl_fall_back_diff(xdfenv_t *diff_env, xpparam_t const *xpp,
		       int line1, int count1, int line2, int count2);

/* Do not call this function, use XDL_ALLOC_GROW instead */
void* xdl_alloc_grow_helper(void* p, long nr, long* alloc, size_t size);

#endif /* #if !defined(XUTILS_H) */
