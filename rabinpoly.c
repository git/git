/*
 *
 * Copyright (C) 1999 David Mazieres (dm@uun.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

  /* Faster generic_fls */
  /* (c) 2002, D.Phillips and Sistina Software */

#include "rabinpoly.h"
#define MSB64 0x8000000000000000ULL

static inline unsigned fls8(unsigned n)
{
       return n & 0xf0?
           n & 0xc0? (n >> 7) + 7: (n >> 5) + 5:
           n & 0x0c? (n >> 3) + 3: n - ((n + 1) >> 2);
}

static inline unsigned fls16(unsigned n)
{
       return n & 0xff00? fls8(n >> 8) + 8: fls8(n);
}

static inline unsigned fls32(unsigned n)
{
       return n & 0xffff0000? fls16(n >> 16) + 16: fls16(n);
}

static inline unsigned fls64(unsigned long long n) /* should be u64 */
{
       return n & 0xffffffff00000000ULL? fls32(n >> 32) + 32: fls32(n);
}


static u_int64_t polymod (u_int64_t nh, u_int64_t nl, u_int64_t d);
static void      polymult (u_int64_t *php, u_int64_t *plp,
                           u_int64_t x, u_int64_t y);
static u_int64_t polymmult (u_int64_t x, u_int64_t y, u_int64_t d);

static u_int64_t poly = 0xb15e234bd3792f63ull;	// Actual polynomial
static u_int64_t T[256];			// Lookup table for mod
static int shift;

u_int64_t append8 (u_int64_t p, u_char m)
{ return ((p << 8) | m) ^ T[p >> shift];
}

static u_int64_t
polymod (u_int64_t nh, u_int64_t nl, u_int64_t d)
{ int i, k;
  assert (d);
  k = fls64 (d) - 1;
  d <<= 63 - k;

  if (nh) {
    if (nh & MSB64)
      nh ^= d;
    for (i = 62; i >= 0; i--)
      if (nh & 1ULL << i) {
	nh ^= d >> (63 - i);
	nl ^= d << (i + 1);
      }
  }
  for (i = 63; i >= k; i--)
    if (nl & 1ULL << i)
      nl ^= d >> (63 - i);
  return nl;
}

static void
polymult (u_int64_t *php, u_int64_t *plp, u_int64_t x, u_int64_t y)
{ int i;
  u_int64_t ph = 0, pl = 0;
  if (x & 1)
    pl = y;
  for (i = 1; i < 64; i++)
    if (x & (1ULL << i)) {
      ph ^= y >> (64 - i);
      pl ^= y << i;
    }
  if (php)
    *php = ph;
  if (plp)
    *plp = pl;
}

static u_int64_t
polymmult (u_int64_t x, u_int64_t y, u_int64_t d)
{
  u_int64_t h, l;
  polymult (&h, &l, x, y);
  return polymod (h, l, d);
}

static int size = RABIN_WINDOW_SIZE;
static u_int64_t fingerprint = 0;
static int bufpos = -1;
static u_int64_t U[256];
static u_char buf[RABIN_WINDOW_SIZE];

void rabin_init ()
{ u_int64_t sizeshift = 1;
 u_int64_t T1;
  int xshift;
  int i, j;
  assert (poly >= 0x100);
  xshift = fls64 (poly) - 1;
  shift = xshift - 8;
  T1 = polymod (0, 1ULL << xshift, poly);
  for (j = 0; j < 256; j++)
    T[j] = polymmult (j, T1, poly) | ((u_int64_t) j << xshift);

  for (i = 1; i < size; i++)
    sizeshift = append8 (sizeshift, 0);
  for (i = 0; i < 256; i++)
    U[i] = polymmult (i, sizeshift, poly);
  bzero (buf, sizeof (buf));
}

void
rabin_reset ()
{ rabin_init();
  fingerprint = 0;
  bzero (buf, sizeof (buf));
}

u_int64_t
rabin_slide8 (u_char m)
{ u_char om;
  if (++bufpos >= size) bufpos = 0;

  om = buf[bufpos];
  buf[bufpos] = m;
  fingerprint = append8 (fingerprint ^ U[om], m);

  return fingerprint;
}

