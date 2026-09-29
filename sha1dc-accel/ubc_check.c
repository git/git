/*
 * The unavoidable-bitconditions (UBC) check of SHA-1 collision detection,
 * in one form per instruction set.
 *
 * Every form returns the same mask as ubc_check() in sha1dc/ubc_check.c:
 * one bit per disturbance vector (DV) that the expanded message w[] has not
 * ruled out. A DV is ruled out as soon as one of its bitconditions fails;
 * each condition says that bit a of w[i] XOR bit b of w[j] equals c.
 *
 * Each form runs in two parts. The prefix tests a fixed set of conditions,
 * chosen and packed into vector lanes for that instruction set, on every
 * block; it rules out every DV for almost all blocks. The few blocks that
 * survive it run the tail, which tests the remaining conditions of each DV
 * still alive.
 *
 * The tables in this file were derived from the output of the solver in
 * the "sha1dc" Rust crate by Sam Reis (https://github.com/srijs/sha1dc,
 * commit 426b4afd), which picks the conditions and their packing. They are
 * used under the MIT license:
 *
 *   Copyright (c) 2017 Marc Stevens (Cryptology Group, Centrum Wiskunde &
 *   Informatica)
 *   Copyright (c) 2017 Dan Shumow (Microsoft Research)
 *   Copyright (c) 2026 Sam Reis
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "../git-compat-util.h"
#include "internal.h"

#define DV_I_43_0_BIT ((uint32_t)1 << 0)
#define DV_I_44_0_BIT ((uint32_t)1 << 1)
#define DV_I_45_0_BIT ((uint32_t)1 << 2)
#define DV_I_46_0_BIT ((uint32_t)1 << 3)
#define DV_I_46_2_BIT ((uint32_t)1 << 4)
#define DV_I_47_0_BIT ((uint32_t)1 << 5)
#define DV_I_47_2_BIT ((uint32_t)1 << 6)
#define DV_I_48_0_BIT ((uint32_t)1 << 7)
#define DV_I_48_2_BIT ((uint32_t)1 << 8)
#define DV_I_49_0_BIT ((uint32_t)1 << 9)
#define DV_I_49_2_BIT ((uint32_t)1 << 10)
#define DV_I_50_0_BIT ((uint32_t)1 << 11)
#define DV_I_50_2_BIT ((uint32_t)1 << 12)
#define DV_I_51_0_BIT ((uint32_t)1 << 13)
#define DV_I_51_2_BIT ((uint32_t)1 << 14)
#define DV_I_52_0_BIT ((uint32_t)1 << 15)
#define DV_II_45_0_BIT ((uint32_t)1 << 16)
#define DV_II_46_0_BIT ((uint32_t)1 << 17)
#define DV_II_46_2_BIT ((uint32_t)1 << 18)
#define DV_II_47_0_BIT ((uint32_t)1 << 19)
#define DV_II_48_0_BIT ((uint32_t)1 << 20)
#define DV_II_49_0_BIT ((uint32_t)1 << 21)
#define DV_II_49_2_BIT ((uint32_t)1 << 22)
#define DV_II_50_0_BIT ((uint32_t)1 << 23)
#define DV_II_50_2_BIT ((uint32_t)1 << 24)
#define DV_II_51_0_BIT ((uint32_t)1 << 25)
#define DV_II_51_2_BIT ((uint32_t)1 << 26)
#define DV_II_52_0_BIT ((uint32_t)1 << 27)
#define DV_II_53_0_BIT ((uint32_t)1 << 28)
#define DV_II_54_0_BIT ((uint32_t)1 << 29)
#define DV_II_55_0_BIT ((uint32_t)1 << 30)
#define DV_II_56_0_BIT ((uint32_t)1 << 31)

/*
 * The prefixes loop over constant tables. Unrolling those loops fully lets
 * the compiler fold each entry into the code as immediates, which is what
 * makes them fast; without it they are two to three times slower.
 */
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
#define UNROLL_TABLE _Pragma("GCC unroll 128")
#else
#define UNROLL_TABLE
#endif

/*
 * The tail loops run over a few conditions at a time, too few to gain from
 * vectorizing; clang would otherwise vectorize them when targeting AVX2.
 */
#ifdef __clang__
#define NO_VECTORIZE _Pragma("clang loop vectorize(disable)")
#else
#define NO_VECTORIZE
#endif

/* A bitcondition: bit a of w[i] XOR bit b of w[j] must equal c. */
struct ubc_cond {
	uint8_t i, a, j, b, c;
};

static inline uint32_t cond_fails(const uint32_t *w, const struct ubc_cond *c)
{
	return (((w[c->i] >> c->a) ^ (w[c->j] >> c->b) ^ c->c) & 1);
}

/* A condition of the scalar prefix, and the DVs it rules out if it fails. */
struct ubc_prefix_cond {
	struct ubc_cond cond;
	uint32_t dvs;
};

/*
 * A group of conditions for a vector prefix, one per lane. In lane k, the
 * bit selected by test[k] of (w[lo + k] >> lo_shift) ^ (w[hi + k] >> hi_shift)
 * must equal want, or the DVs in dvs[k] are ruled out. The lanes share
 * everything but test[] and dvs[]; an unused lane has both 0.
 *
 * No group reads past w[66].
 */
struct ubc_group4 {
	uint8_t lo, lo_shift, hi, hi_shift, want;
	uint32_t test[4];
	uint32_t dvs[4];
};

struct ubc_group8 {
	uint8_t lo, lo_shift, hi, hi_shift, want;
	uint32_t test[8];
	uint32_t dvs[8];
};

/* The tail conditions of DV d are checks[spans[d].start] onwards. */
struct tail_span {
	uint16_t start;
	uint8_t len;
};

/*
 * Runs the tail conditions of each DV still alive in `mask`, and clears
 * the DVs with a failing one. Each condition fails about half the time, so
 * it tests all of a DV's conditions rather than branch on each one.
 */
static inline uint32_t run_tail(const uint32_t *w, uint32_t mask,
				const struct ubc_cond *checks,
				const struct tail_span *spans)
{
	uint32_t out = mask;
	while (mask) {
		unsigned d = sha1dc_ctz(mask);
		const struct ubc_cond *c = checks + spans[d].start;
		const struct ubc_cond *end = c + spans[d].len;
		uint32_t fail = 0;

		NO_VECTORIZE
		for (; c < end; c++)
			fail |= cond_fails(w, c);
		out &= ~(fail << d);
		mask &= mask - 1;
	}
	return out;
}

/* scalar form */

static const struct ubc_prefix_cond scalar_prefix_conds[] = {
	{ { 44, 29, 45, 29, 0 },
	  DV_I_48_0_BIT | DV_I_51_0_BIT | DV_I_52_0_BIT | DV_II_45_0_BIT |
	  DV_II_46_0_BIT | DV_II_50_0_BIT | DV_II_51_0_BIT },
	{ { 46, 29, 47, 29, 0 },
	  DV_I_43_0_BIT | DV_I_50_0_BIT | DV_II_47_0_BIT | DV_II_48_0_BIT |
	  DV_II_52_0_BIT | DV_II_53_0_BIT },
	{ { 45, 4, 48, 29, 0 },
	  DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT |
	  DV_II_49_0_BIT | DV_II_54_0_BIT },
	{ { 49, 29, 50, 29, 0 },
	  DV_I_46_0_BIT | DV_II_45_0_BIT | DV_II_50_0_BIT | DV_II_51_0_BIT |
	  DV_II_55_0_BIT | DV_II_56_0_BIT },
	{ { 40, 29, 41, 29, 0 },
	  DV_I_44_0_BIT | DV_I_47_0_BIT | DV_I_48_0_BIT | DV_II_46_0_BIT |
	  DV_II_47_0_BIT | DV_II_56_0_BIT },
	{ { 36, 1, 37, 6, 1 },
	  DV_I_47_2_BIT | DV_I_50_2_BIT | DV_II_46_2_BIT },
	{ { 47, 29, 48, 29, 0 },
	  DV_I_44_0_BIT | DV_I_51_0_BIT | DV_II_48_0_BIT | DV_II_49_0_BIT |
	  DV_II_53_0_BIT | DV_II_54_0_BIT },
	{ { 39, 1, 40, 6, 1 },
	  DV_I_46_2_BIT | DV_I_50_2_BIT | DV_II_49_2_BIT },
	{ { 40, 1, 41, 6, 1 },
	  DV_I_47_2_BIT | DV_I_51_2_BIT | DV_II_50_2_BIT },
	{ { 41, 1, 42, 6, 1 },
	  DV_I_48_2_BIT | DV_II_46_2_BIT | DV_II_51_2_BIT },
	{ { 43, 4, 46, 29, 0 },
	  DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT |
	  DV_II_47_0_BIT | DV_II_52_0_BIT },
	{ { 46, 4, 49, 29, 0 },
	  DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT |
	  DV_II_50_0_BIT | DV_II_55_0_BIT },
	{ { 45, 6, 47, 6, 0 },
	  DV_I_47_2_BIT | DV_I_49_2_BIT | DV_I_51_2_BIT },
	{ { 45, 29, 46, 29, 0 },
	  DV_I_49_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT | DV_II_47_0_BIT |
	  DV_II_51_0_BIT | DV_II_52_0_BIT },
	{ { 44, 4, 47, 29, 0 },
	  DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT |
	  DV_II_48_0_BIT | DV_II_53_0_BIT },
	{ { 48, 29, 49, 29, 0 },
	  DV_I_45_0_BIT | DV_I_52_0_BIT | DV_II_49_0_BIT | DV_II_50_0_BIT |
	  DV_II_54_0_BIT | DV_II_55_0_BIT },
	{ { 44, 6, 46, 6, 0 },
	  DV_I_46_2_BIT | DV_I_48_2_BIT | DV_I_50_2_BIT },
	{ { 47, 4, 50, 29, 0 },
	  DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	  DV_II_51_0_BIT | DV_II_56_0_BIT },
	{ { 35, 1, 36, 6, 1 },
	  DV_I_46_2_BIT | DV_I_49_2_BIT },
	{ { 44, 1, 45, 6, 1 },
	  DV_I_51_2_BIT | DV_II_49_2_BIT },
	{ { 42, 6, 43, 1, 0 },
	  DV_II_46_2_BIT | DV_II_51_2_BIT },
	{ { 37, 4, 40, 29, 0 },
	  DV_I_43_0_BIT | DV_I_47_0_BIT | DV_II_46_0_BIT | DV_II_53_0_BIT |
	  DV_II_55_0_BIT },
	{ { 41, 6, 42, 1, 0 },
	  DV_I_51_2_BIT | DV_II_50_2_BIT },
	{ { 40, 4, 43, 29, 0 },
	  DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_50_0_BIT | DV_II_49_0_BIT |
	  DV_II_56_0_BIT },
	{ { 41, 4, 44, 29, 0 },
	  DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_51_0_BIT |
	  DV_II_45_0_BIT | DV_II_50_0_BIT },
	{ { 52, 29, 53, 29, 0 },
	  DV_I_49_0_BIT | DV_II_45_0_BIT | DV_II_48_0_BIT | DV_II_53_0_BIT |
	  DV_II_54_0_BIT },
	{ { 40, 6, 41, 1, 0 },
	  DV_I_50_2_BIT | DV_II_49_2_BIT },
	{ { 46, 6, 47, 1, 0 },
	  DV_I_46_2_BIT | DV_II_50_2_BIT },
	{ { 47, 6, 48, 1, 0 },
	  DV_I_47_2_BIT | DV_II_51_2_BIT },
	{ { 42, 4, 45, 29, 0 },
	  DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_52_0_BIT |
	  DV_II_46_0_BIT | DV_II_51_0_BIT },
	{ { 37, 1, 38, 6, 1 },
	  DV_I_48_2_BIT | DV_I_51_2_BIT },
	{ { 43, 6, 45, 6, 0 },
	  DV_I_47_2_BIT | DV_I_49_2_BIT },
	{ { 53, 29, 54, 29, 0 },
	  DV_I_50_0_BIT | DV_II_46_0_BIT | DV_II_49_0_BIT | DV_II_54_0_BIT |
	  DV_II_55_0_BIT },
	{ { 41, 29, 42, 29, 0 },
	  DV_I_45_0_BIT | DV_I_48_0_BIT | DV_I_49_0_BIT | DV_II_47_0_BIT |
	  DV_II_48_0_BIT },
	{ { 50, 29, 51, 29, 0 },
	  DV_I_47_0_BIT | DV_II_46_0_BIT | DV_II_51_0_BIT | DV_II_52_0_BIT |
	  DV_II_56_0_BIT },
	{ { 61, 2, 62, 7, 1 },
	  DV_I_46_2_BIT | DV_II_46_2_BIT },
	{ { 46, 6, 48, 6, 0 },
	  DV_I_48_2_BIT | DV_I_50_2_BIT },
	{ { 39, 4, 42, 29, 0 },
	  DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_49_0_BIT | DV_II_48_0_BIT |
	  DV_II_55_0_BIT },
	{ { 43, 29, 44, 29, 0 },
	  DV_I_47_0_BIT | DV_I_50_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	  DV_II_49_0_BIT | DV_II_50_0_BIT },
	{ { 47, 6, 49, 6, 0 },
	  DV_I_49_2_BIT | DV_I_51_2_BIT },
	{ { 51, 29, 52, 29, 0 },
	  DV_I_48_0_BIT | DV_II_47_0_BIT | DV_II_52_0_BIT | DV_II_53_0_BIT },
	{ { 36, 0, 37, 5, 1 },
	  DV_II_49_2_BIT },
	{ { 37, 0, 38, 5, 1 },
	  DV_II_50_2_BIT },
	{ { 38, 0, 39, 5, 1 },
	  DV_II_51_2_BIT },
	{ { 38, 4, 41, 29, 0 },
	  DV_I_44_0_BIT | DV_I_48_0_BIT | DV_II_47_0_BIT | DV_II_54_0_BIT |
	  DV_II_56_0_BIT },
	{ { 50, 6, 51, 1, 0 },
	  DV_I_50_2_BIT | DV_II_46_2_BIT },
	{ { 42, 6, 44, 6, 0 },
	  DV_I_46_2_BIT | DV_I_48_2_BIT },
	{ { 48, 4, 51, 29, 0 },
	  DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	  DV_II_52_0_BIT },
	{ { 42, 29, 43, 29, 0 },
	  DV_I_46_0_BIT | DV_I_49_0_BIT | DV_I_50_0_BIT | DV_II_48_0_BIT |
	  DV_II_49_0_BIT },
	{ { 54, 29, 55, 29, 0 },
	  DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_50_0_BIT | DV_II_55_0_BIT |
	  DV_II_56_0_BIT },
	{ { 38, 1, 39, 6, 1 },
	  DV_I_49_2_BIT },
	{ { 45, 1, 46, 6, 1 },
	  DV_II_50_2_BIT },
	{ { 46, 1, 47, 6, 1 },
	  DV_II_51_2_BIT },
	{ { 50, 1, 51, 6, 1 },
	  DV_II_49_2_BIT },
	{ { 39, 4, 40, 29, 1 },
	  DV_I_43_0_BIT | DV_II_53_0_BIT | DV_II_55_0_BIT },
	{ { 55, 29, 56, 29, 0 },
	  DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_51_0_BIT | DV_II_56_0_BIT },
	{ { 48, 6, 50, 6, 0 },
	  DV_I_50_2_BIT | DV_II_46_2_BIT },
	{ { 49, 4, 52, 29, 0 },
	  DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT | DV_II_47_0_BIT |
	  DV_II_53_0_BIT },
	{ { 40, 4, 41, 29, 1 },
	  DV_I_44_0_BIT | DV_II_54_0_BIT | DV_II_56_0_BIT },
	{ { 41, 4, 42, 29, 1 },
	  DV_I_43_0_BIT | DV_I_45_0_BIT | DV_II_55_0_BIT },
	{ { 42, 1, 43, 6, 1 },
	  DV_I_49_2_BIT },
	{ { 51, 1, 52, 6, 1 },
	  DV_II_50_2_BIT },
	{ { 52, 1, 53, 6, 1 },
	  DV_II_51_2_BIT },
	{ { 62, 2, 63, 7, 1 },
	  DV_I_47_2_BIT },
	{ { 63, 2, 64, 7, 1 },
	  DV_I_48_2_BIT },
	{ { 45, 6, 46, 1, 0 },
	  DV_II_49_2_BIT },
	{ { 36, 4, 40, 29, 0 },
	  DV_I_46_0_BIT | DV_I_49_0_BIT | DV_II_45_0_BIT | DV_II_48_0_BIT },
	{ { 50, 4, 53, 29, 0 },
	  DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT | DV_II_48_0_BIT |
	  DV_II_54_0_BIT },
	{ { 56, 29, 57, 29, 0 },
	  DV_II_49_0_BIT | DV_II_52_0_BIT },
	{ { 43, 4, 44, 29, 1 },
	  DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT }
};

static const struct ubc_cond scalar_tail_checks[] = {
	/* DV_I_43_0 */
	{ 58, 0, 59, 5, 1 },
	{ 58, 0, 63, 30, 1 },
	{ 61, 1, 62, 6, 1 },
	{ 43, 4, 47, 29, 0 },
	/* DV_I_44_0 */
	{ 40, 4, 42, 4, 1 },
	{ 42, 4, 44, 4, 1 },
	{ 59, 0, 60, 5, 1 },
	{ 59, 0, 64, 30, 1 },
	{ 62, 1, 63, 6, 1 },
	{ 44, 4, 48, 29, 0 },
	/* DV_I_45_0 */
	{ 43, 4, 45, 4, 1 },
	{ 60, 0, 61, 5, 1 },
	{ 63, 1, 64, 6, 1 },
	{ 35, 4, 39, 29, 0 },
	/* DV_I_46_0 */
	{ 40, 4, 42, 4, 1 },
	{ 42, 4, 44, 4, 1 },
	{ 44, 4, 46, 4, 1 },
	{ 61, 0, 62, 5, 1 },
	/* DV_I_46_2 */
	{ 39, 1, 42, 6, 1 },
	/* DV_I_47_0 */
	{ 43, 4, 45, 4, 1 },
	{ 45, 4, 47, 4, 1 },
	{ 62, 0, 63, 5, 1 },
	{ 37, 4, 41, 29, 0 },
	/* DV_I_47_2 */
	{ 40, 1, 43, 6, 1 },
	/* DV_I_48_0 */
	{ 42, 4, 44, 4, 1 },
	{ 44, 4, 46, 4, 1 },
	{ 46, 4, 48, 4, 1 },
	{ 63, 0, 64, 5, 1 },
	{ 35, 4, 39, 29, 0 },
	{ 38, 4, 42, 29, 0 },
	/* DV_I_48_2 */
	{ 41, 1, 49, 1, 1 },
	/* DV_I_49_0 */
	{ 43, 4, 45, 4, 1 },
	{ 45, 4, 47, 4, 1 },
	{ 47, 4, 49, 4, 1 },
	{ 39, 4, 43, 29, 0 },
	/* DV_I_49_2 */
	{ 38, 1, 40, 1, 1 },
	{ 42, 1, 50, 1, 1 },
	/* DV_I_50_0 */
	{ 36, 4, 37, 4, 1 },
	{ 44, 4, 46, 4, 1 },
	{ 46, 4, 48, 4, 1 },
	{ 48, 4, 50, 4, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 40, 4, 44, 29, 0 },
	/* DV_I_50_2 */
	{ 43, 1, 44, 6, 1 },
	/* DV_I_51_0 */
	{ 37, 4, 38, 4, 1 },
	{ 45, 4, 47, 4, 1 },
	{ 47, 4, 49, 4, 1 },
	{ 52, 29, 55, 29, 1 },
	{ 35, 3, 39, 28, 0 },
	{ 38, 4, 42, 29, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 51, 4, 54, 29, 0 },
	/* DV_I_51_2 */
	{ 44, 1, 51, 6, 1 },
	{ 44, 1, 52, 1, 1 },
	{ 35, 5, 39, 30, 0 },
	{ 37, 1, 37, 6, 0 },
	/* DV_I_52_0 */
	{ 38, 4, 39, 4, 1 },
	{ 46, 4, 48, 4, 1 },
	{ 48, 4, 50, 4, 1 },
	{ 53, 29, 56, 29, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	{ 52, 4, 55, 29, 0 },
	/* DV_II_45_0 */
	{ 47, 4, 49, 4, 1 },
	{ 60, 0, 61, 5, 1 },
	{ 63, 1, 64, 6, 1 },
	{ 41, 4, 45, 29, 0 },
	/* DV_II_46_0 */
	{ 48, 4, 50, 4, 1 },
	{ 61, 0, 62, 5, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	/* DV_II_46_2 */
	{ 47, 1, 48, 6, 1 },
	/* DV_II_47_0 */
	{ 52, 29, 55, 29, 1 },
	{ 62, 0, 63, 5, 1 },
	{ 35, 3, 39, 28, 0 },
	{ 35, 4, 39, 29, 0 },
	{ 38, 4, 42, 29, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 51, 4, 54, 29, 0 },
	/* DV_II_48_0 */
	{ 35, 30, 36, 3, 1 },
	{ 35, 30, 40, 28, 1 },
	{ 52, 29, 55, 29, 1 },
	{ 53, 29, 56, 29, 1 },
	{ 63, 0, 64, 5, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 52, 4, 55, 29, 0 },
	/* DV_II_49_0 */
	{ 36, 30, 37, 3, 1 },
	{ 36, 30, 41, 28, 1 },
	{ 53, 29, 56, 29, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 40, 4, 44, 29, 0 },
	{ 51, 4, 54, 29, 0 },
	{ 53, 4, 56, 29, 0 },
	/* DV_II_49_2 */
	{ 36, 0, 41, 30, 1 },
	{ 50, 1, 53, 6, 1 },
	{ 50, 1, 54, 1, 1 },
	/* DV_II_50_0 */
	{ 37, 30, 38, 3, 1 },
	{ 37, 30, 42, 28, 1 },
	{ 55, 29, 58, 29, 1 },
	{ 38, 4, 42, 29, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 52, 4, 55, 29, 0 },
	{ 54, 4, 57, 29, 0 },
	{ 57, 29, 58, 29, 0 },
	/* DV_II_50_2 */
	{ 37, 0, 42, 30, 1 },
	{ 51, 1, 54, 6, 1 },
	{ 51, 1, 55, 1, 1 },
	/* DV_II_51_0 */
	{ 38, 30, 39, 3, 1 },
	{ 38, 30, 43, 28, 1 },
	{ 55, 29, 58, 29, 1 },
	{ 56, 29, 59, 29, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	{ 53, 4, 56, 29, 0 },
	{ 55, 4, 58, 29, 0 },
	/* DV_II_51_2 */
	{ 38, 0, 43, 30, 1 },
	{ 52, 1, 55, 6, 1 },
	{ 52, 1, 56, 1, 1 },
	/* DV_II_52_0 */
	{ 36, 4, 38, 4, 1 },
	{ 39, 30, 40, 3, 1 },
	{ 39, 30, 44, 28, 1 },
	{ 54, 4, 60, 29, 1 },
	{ 56, 29, 59, 29, 1 },
	{ 40, 4, 44, 29, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 54, 4, 57, 29, 0 },
	{ 56, 4, 59, 29, 0 },
	/* DV_II_53_0 */
	{ 55, 4, 57, 4, 1 },
	{ 55, 4, 61, 29, 1 },
	{ 41, 3, 45, 28, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 55, 4, 58, 29, 0 },
	{ 57, 29, 58, 29, 0 },
	/* DV_II_54_0 */
	{ 36, 4, 38, 4, 1 },
	{ 42, 3, 46, 28, 0 },
	{ 42, 4, 46, 29, 0 },
	{ 56, 4, 59, 29, 0 },
	{ 56, 4, 58, 29, 0 },
	{ 58, 4, 62, 29, 0 },
	/* DV_II_55_0 */
	{ 43, 3, 47, 28, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 51, 4, 54, 29, 0 },
	{ 57, 4, 59, 29, 0 },
	{ 59, 4, 63, 29, 0 },
	/* DV_II_56_0 */
	{ 40, 4, 42, 4, 1 },
	{ 44, 3, 48, 28, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 52, 4, 55, 29, 0 },
	{ 60, 4, 64, 29, 0 }
};

static const struct tail_span scalar_tail_spans[32] = {
	{ 0, 4 },	/* DV_I_43_0 */
	{ 4, 6 },	/* DV_I_44_0 */
	{ 10, 4 },	/* DV_I_45_0 */
	{ 14, 4 },	/* DV_I_46_0 */
	{ 18, 1 },	/* DV_I_46_2 */
	{ 19, 4 },	/* DV_I_47_0 */
	{ 23, 1 },	/* DV_I_47_2 */
	{ 24, 6 },	/* DV_I_48_0 */
	{ 30, 1 },	/* DV_I_48_2 */
	{ 31, 4 },	/* DV_I_49_0 */
	{ 35, 2 },	/* DV_I_49_2 */
	{ 37, 6 },	/* DV_I_50_0 */
	{ 43, 1 },	/* DV_I_50_2 */
	{ 44, 8 },	/* DV_I_51_0 */
	{ 52, 4 },	/* DV_I_51_2 */
	{ 56, 7 },	/* DV_I_52_0 */
	{ 63, 4 },	/* DV_II_45_0 */
	{ 67, 4 },	/* DV_II_46_0 */
	{ 71, 1 },	/* DV_II_46_2 */
	{ 72, 7 },	/* DV_II_47_0 */
	{ 79, 8 },	/* DV_II_48_0 */
	{ 87, 7 },	/* DV_II_49_0 */
	{ 94, 3 },	/* DV_II_49_2 */
	{ 97, 8 },	/* DV_II_50_0 */
	{ 105, 3 },	/* DV_II_50_2 */
	{ 108, 8 },	/* DV_II_51_0 */
	{ 116, 3 },	/* DV_II_51_2 */
	{ 119, 9 },	/* DV_II_52_0 */
	{ 128, 7 },	/* DV_II_53_0 */
	{ 135, 6 },	/* DV_II_54_0 */
	{ 141, 5 },	/* DV_II_55_0 */
	{ 146, 5 }	/* DV_II_56_0 */
};

static uint32_t scalar_prefix(const uint32_t *w)
{
	uint32_t mask = 0xFFFFFFFF;
	size_t i;

	UNROLL_TABLE
	for (i = 0; i < ARRAY_SIZE(scalar_prefix_conds); i++) {
		const struct ubc_prefix_cond *p = &scalar_prefix_conds[i];
		mask &= ~(p->dvs & (0 - cond_fails(w, &p->cond)));
	}
	return mask;
}

uint32_t sha1dc_ubc_check_scalar(const uint32_t w[80])
{
	uint32_t mask = scalar_prefix(w);
	/* Every check only clears bits, so an empty mask settles it. */
	if (!mask)
		return 0;
	return run_tail(w, mask, scalar_tail_checks, scalar_tail_spans);
}

/* neon form */

#ifdef SHA1DC_HAVE_NEON

static const struct ubc_group4 neon_groups[] = {
	{ 35, 0, 36, 5, 1,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 0 },
	  { DV_I_46_2_BIT | DV_I_49_2_BIT,
	    DV_I_47_2_BIT | DV_I_50_2_BIT | DV_II_46_2_BIT,
	    DV_I_48_2_BIT | DV_I_51_2_BIT,
	    DV_II_51_2_BIT } },
	{ 36, 0, 37, 5, 1,
	  { 1u << 0, 1u << 0, 1u << 1, 1u << 1 },
	  { DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_I_49_2_BIT,
	    DV_I_46_2_BIT | DV_I_50_2_BIT | DV_II_49_2_BIT } },
	{ 36, 0, 38, 0, 1,
	  { 1u << 4, 1u << 4, 1u << 1, 1u << 4 },
	  { DV_II_52_0_BIT | DV_II_54_0_BIT,
	    DV_I_43_0_BIT | DV_II_53_0_BIT | DV_II_55_0_BIT,
	    DV_I_49_2_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_II_55_0_BIT } },
	{ 36, 0, 40, 25, 0,
	  { 1u << 4, 1u << 5, 1u << 5, 1u << 5 },
	  { DV_I_46_0_BIT | DV_I_49_0_BIT | DV_II_45_0_BIT |
	    DV_II_48_0_BIT,
	    DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT } },
	{ 38, 0, 40, 0, 1,
	  { 1u << 4, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_44_0_BIT | DV_II_54_0_BIT | DV_II_56_0_BIT,
	    DV_I_50_2_BIT | DV_II_49_2_BIT,
	    DV_I_51_2_BIT | DV_II_50_2_BIT,
	    DV_II_46_2_BIT | DV_II_51_2_BIT } },
	{ 37, 0, 40, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_43_0_BIT | DV_I_47_0_BIT | DV_II_46_0_BIT |
	    DV_II_53_0_BIT | DV_II_55_0_BIT,
	    DV_I_44_0_BIT | DV_I_48_0_BIT | DV_II_47_0_BIT |
	    DV_II_54_0_BIT | DV_II_56_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_49_0_BIT | DV_II_48_0_BIT |
	    DV_II_55_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_50_0_BIT | DV_II_49_0_BIT |
	    DV_II_56_0_BIT } },
	{ 40, 0, 41, 5, 1,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_47_2_BIT | DV_I_51_2_BIT | DV_II_50_2_BIT,
	    DV_I_48_2_BIT | DV_II_46_2_BIT | DV_II_51_2_BIT,
	    DV_I_49_2_BIT,
	    DV_I_50_2_BIT } },
	{ 40, 0, 41, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_44_0_BIT | DV_I_47_0_BIT | DV_I_48_0_BIT | DV_II_46_0_BIT |
	    DV_II_47_0_BIT | DV_II_56_0_BIT,
	    DV_I_45_0_BIT | DV_I_48_0_BIT | DV_I_49_0_BIT | DV_II_47_0_BIT |
	    DV_II_48_0_BIT,
	    DV_I_46_0_BIT | DV_I_49_0_BIT | DV_I_50_0_BIT | DV_II_48_0_BIT |
	    DV_II_49_0_BIT,
	    DV_I_47_0_BIT | DV_I_50_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	    DV_II_49_0_BIT | DV_II_50_0_BIT } },
	{ 41, 0, 43, 0, 0,
	  { 1u << 6, 1u << 6, 1u << 6, 1u << 6 },
	  { DV_I_47_2_BIT,
	    DV_I_46_2_BIT | DV_I_48_2_BIT,
	    DV_I_47_2_BIT | DV_I_49_2_BIT,
	    DV_I_46_2_BIT | DV_I_48_2_BIT | DV_I_50_2_BIT } },
	{ 41, 0, 44, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_51_0_BIT |
	    DV_II_45_0_BIT | DV_II_50_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_52_0_BIT |
	    DV_II_46_0_BIT | DV_II_51_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT |
	    DV_II_47_0_BIT | DV_II_52_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT |
	    DV_II_48_0_BIT | DV_II_53_0_BIT } },
	{ 44, 0, 45, 5, 1,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_51_2_BIT | DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT,
	    DV_II_46_2_BIT } },
	{ 44, 0, 45, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_48_0_BIT | DV_I_51_0_BIT | DV_I_52_0_BIT | DV_II_45_0_BIT |
	    DV_II_46_0_BIT | DV_II_50_0_BIT | DV_II_51_0_BIT,
	    DV_I_49_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	    DV_II_47_0_BIT | DV_II_51_0_BIT | DV_II_52_0_BIT,
	    DV_I_43_0_BIT | DV_I_50_0_BIT | DV_II_47_0_BIT |
	    DV_II_48_0_BIT | DV_II_52_0_BIT | DV_II_53_0_BIT,
	    DV_I_44_0_BIT | DV_I_51_0_BIT | DV_II_48_0_BIT |
	    DV_II_49_0_BIT | DV_II_53_0_BIT | DV_II_54_0_BIT } },
	{ 45, 5, 46, 0, 0,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_II_49_2_BIT,
	    DV_I_46_2_BIT | DV_II_50_2_BIT,
	    DV_I_47_2_BIT | DV_II_51_2_BIT,
	    DV_I_48_2_BIT } },
	{ 45, 0, 47, 0, 0,
	  { 1u << 6, 1u << 6, 1u << 6, 1u << 6 },
	  { DV_I_47_2_BIT | DV_I_49_2_BIT | DV_I_51_2_BIT,
	    DV_I_48_2_BIT | DV_I_50_2_BIT,
	    DV_I_49_2_BIT | DV_I_51_2_BIT,
	    DV_I_50_2_BIT | DV_II_46_2_BIT } },
	{ 45, 0, 47, 0, 1,
	  { 1u << 29, 1u << 29, 1u << 4, 1u << 4 },
	  { DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT,
	    DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT,
	    DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT,
	    DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT } },
	{ 45, 0, 48, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT |
	    DV_II_49_0_BIT | DV_II_54_0_BIT,
	    DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT |
	    DV_II_50_0_BIT | DV_II_55_0_BIT,
	    DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	    DV_II_51_0_BIT | DV_II_56_0_BIT,
	    DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	    DV_II_52_0_BIT } },
	{ 48, 0, 49, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_45_0_BIT | DV_I_52_0_BIT | DV_II_49_0_BIT |
	    DV_II_50_0_BIT | DV_II_54_0_BIT | DV_II_55_0_BIT,
	    DV_I_46_0_BIT | DV_II_45_0_BIT | DV_II_50_0_BIT |
	    DV_II_51_0_BIT | DV_II_55_0_BIT | DV_II_56_0_BIT,
	    DV_I_47_0_BIT | DV_II_46_0_BIT | DV_II_51_0_BIT |
	    DV_II_52_0_BIT | DV_II_56_0_BIT,
	    DV_I_48_0_BIT | DV_II_47_0_BIT | DV_II_52_0_BIT |
	    DV_II_53_0_BIT } },
	{ 52, 0, 53, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_49_0_BIT | DV_II_45_0_BIT | DV_II_48_0_BIT |
	    DV_II_53_0_BIT | DV_II_54_0_BIT,
	    DV_I_50_0_BIT | DV_II_46_0_BIT | DV_II_49_0_BIT |
	    DV_II_54_0_BIT | DV_II_55_0_BIT,
	    DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_50_0_BIT |
	    DV_II_55_0_BIT | DV_II_56_0_BIT,
	    DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_51_0_BIT |
	    DV_II_56_0_BIT } },
	{ 52, 0, 55, 0, 1,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_48_0_BIT,
	    DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_49_0_BIT,
	    DV_II_49_0_BIT | DV_II_50_0_BIT,
	    DV_II_50_0_BIT | DV_II_51_0_BIT } },
	{ 60, 0, 61, 5, 1,
	  { 1u << 0, 1u << 2, 1u << 2, 1u << 2 },
	  { DV_I_45_0_BIT | DV_II_45_0_BIT,
	    DV_I_46_2_BIT | DV_II_46_2_BIT,
	    DV_I_47_2_BIT,
	    DV_I_48_2_BIT } }
};

static const struct ubc_cond neon_tail_checks[] = {
	/* DV_I_43_0 */
	{ 41, 4, 43, 4, 1 },
	{ 58, 0, 59, 5, 1 },
	{ 58, 0, 63, 30, 1 },
	{ 61, 1, 62, 6, 1 },
	{ 43, 4, 47, 29, 0 },
	/* DV_I_44_0 */
	{ 40, 4, 42, 4, 1 },
	{ 59, 0, 60, 5, 1 },
	{ 59, 0, 64, 30, 1 },
	{ 62, 1, 63, 6, 1 },
	{ 44, 4, 48, 29, 0 },
	/* DV_I_45_0 */
	{ 41, 4, 43, 4, 1 },
	{ 63, 1, 64, 6, 1 },
	{ 35, 4, 39, 29, 0 },
	/* DV_I_46_0 */
	{ 40, 4, 42, 4, 1 },
	{ 44, 4, 46, 4, 1 },
	{ 61, 0, 62, 5, 1 },
	/* DV_I_46_2 */
	{ 39, 1, 42, 6, 1 },
	/* DV_I_47_0 */
	{ 41, 4, 43, 4, 1 },
	{ 45, 4, 47, 4, 1 },
	{ 62, 0, 63, 5, 1 },
	{ 37, 4, 41, 29, 0 },
	/* DV_I_48_0 */
	{ 44, 4, 46, 4, 1 },
	{ 46, 4, 48, 4, 1 },
	{ 63, 0, 64, 5, 1 },
	{ 35, 4, 39, 29, 0 },
	{ 38, 4, 42, 29, 0 },
	/* DV_I_49_0 */
	{ 45, 4, 47, 4, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 49, 4, 52, 29, 0 },
	/* DV_I_49_2 */
	{ 42, 1, 50, 1, 1 },
	/* DV_I_50_0 */
	{ 36, 4, 37, 4, 1 },
	{ 44, 4, 46, 4, 1 },
	{ 46, 4, 48, 4, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 40, 4, 44, 29, 0 },
	{ 50, 4, 53, 29, 0 },
	/* DV_I_50_2 */
	{ 48, 6, 51, 1, 0 },
	/* DV_I_51_0 */
	{ 37, 4, 38, 4, 1 },
	{ 45, 4, 47, 4, 1 },
	{ 35, 3, 39, 28, 0 },
	{ 38, 4, 42, 29, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 49, 4, 52, 29, 0 },
	{ 51, 4, 54, 29, 0 },
	/* DV_I_51_2 */
	{ 44, 1, 51, 6, 1 },
	{ 44, 1, 52, 1, 1 },
	{ 35, 5, 39, 30, 0 },
	{ 37, 1, 37, 6, 0 },
	/* DV_I_52_0 */
	{ 38, 4, 39, 4, 1 },
	{ 46, 4, 48, 4, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	{ 50, 4, 53, 29, 0 },
	{ 52, 4, 55, 29, 0 },
	/* DV_II_45_0 */
	{ 63, 1, 64, 6, 1 },
	{ 41, 4, 45, 29, 0 },
	{ 49, 4, 52, 29, 0 },
	/* DV_II_46_0 */
	{ 61, 0, 62, 5, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	{ 50, 4, 53, 29, 0 },
	/* DV_II_46_2 */
	{ 48, 6, 51, 1, 0 },
	/* DV_II_47_0 */
	{ 62, 0, 63, 5, 1 },
	{ 35, 3, 39, 28, 0 },
	{ 35, 4, 39, 29, 0 },
	{ 38, 4, 42, 29, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 49, 4, 52, 29, 0 },
	{ 51, 4, 54, 29, 0 },
	/* DV_II_48_0 */
	{ 35, 30, 36, 3, 1 },
	{ 35, 30, 40, 28, 1 },
	{ 63, 0, 64, 5, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 50, 4, 53, 29, 0 },
	{ 52, 4, 55, 29, 0 },
	/* DV_II_49_0 */
	{ 36, 30, 37, 3, 1 },
	{ 36, 30, 41, 28, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 40, 4, 44, 29, 0 },
	{ 51, 4, 54, 29, 0 },
	{ 53, 4, 56, 29, 0 },
	/* DV_II_49_2 */
	{ 50, 1, 51, 6, 1 },
	{ 50, 1, 53, 6, 1 },
	{ 50, 1, 54, 1, 1 },
	/* DV_II_50_0 */
	{ 37, 30, 38, 3, 1 },
	{ 37, 30, 42, 28, 1 },
	{ 38, 4, 42, 29, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 52, 4, 55, 29, 0 },
	{ 54, 4, 57, 29, 0 },
	/* DV_II_50_2 */
	{ 51, 1, 52, 6, 1 },
	{ 51, 1, 54, 6, 1 },
	{ 51, 1, 55, 1, 1 },
	/* DV_II_51_0 */
	{ 38, 30, 39, 3, 1 },
	{ 38, 30, 43, 28, 1 },
	{ 56, 29, 59, 29, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	{ 53, 4, 56, 29, 0 },
	{ 55, 4, 58, 29, 0 },
	/* DV_II_51_2 */
	{ 52, 1, 53, 6, 1 },
	{ 52, 1, 55, 6, 1 },
	{ 52, 1, 56, 1, 1 },
	/* DV_II_52_0 */
	{ 39, 30, 40, 3, 1 },
	{ 39, 30, 44, 28, 1 },
	{ 54, 4, 56, 4, 1 },
	{ 54, 4, 60, 29, 1 },
	{ 56, 29, 59, 29, 1 },
	{ 40, 4, 44, 29, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 54, 4, 57, 29, 0 },
	{ 56, 4, 59, 29, 0 },
	/* DV_II_53_0 */
	{ 55, 4, 57, 4, 1 },
	{ 55, 4, 61, 29, 1 },
	{ 41, 3, 45, 28, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 49, 4, 52, 29, 0 },
	{ 55, 4, 58, 29, 0 },
	{ 55, 4, 57, 29, 0 },
	/* DV_II_54_0 */
	{ 42, 3, 46, 28, 0 },
	{ 42, 4, 46, 29, 0 },
	{ 50, 4, 53, 29, 0 },
	{ 56, 4, 59, 29, 0 },
	{ 56, 4, 58, 29, 0 },
	{ 58, 4, 62, 29, 0 },
	/* DV_II_55_0 */
	{ 43, 3, 47, 28, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 51, 4, 54, 29, 0 },
	{ 57, 4, 59, 29, 0 },
	{ 59, 4, 63, 29, 0 },
	/* DV_II_56_0 */
	{ 40, 4, 42, 4, 1 },
	{ 44, 3, 48, 28, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 52, 4, 55, 29, 0 },
	{ 60, 4, 64, 29, 0 }
};

static const struct tail_span neon_tail_spans[32] = {
	{ 0, 5 },	/* DV_I_43_0 */
	{ 5, 5 },	/* DV_I_44_0 */
	{ 10, 3 },	/* DV_I_45_0 */
	{ 13, 3 },	/* DV_I_46_0 */
	{ 16, 1 },	/* DV_I_46_2 */
	{ 17, 4 },	/* DV_I_47_0 */
	{ 21, 0 },	/* DV_I_47_2 */
	{ 21, 5 },	/* DV_I_48_0 */
	{ 26, 0 },	/* DV_I_48_2 */
	{ 26, 3 },	/* DV_I_49_0 */
	{ 29, 1 },	/* DV_I_49_2 */
	{ 30, 6 },	/* DV_I_50_0 */
	{ 36, 1 },	/* DV_I_50_2 */
	{ 37, 7 },	/* DV_I_51_0 */
	{ 44, 4 },	/* DV_I_51_2 */
	{ 48, 6 },	/* DV_I_52_0 */
	{ 54, 3 },	/* DV_II_45_0 */
	{ 57, 4 },	/* DV_II_46_0 */
	{ 61, 1 },	/* DV_II_46_2 */
	{ 62, 7 },	/* DV_II_47_0 */
	{ 69, 7 },	/* DV_II_48_0 */
	{ 76, 6 },	/* DV_II_49_0 */
	{ 82, 3 },	/* DV_II_49_2 */
	{ 85, 6 },	/* DV_II_50_0 */
	{ 91, 3 },	/* DV_II_50_2 */
	{ 94, 7 },	/* DV_II_51_0 */
	{ 101, 3 },	/* DV_II_51_2 */
	{ 104, 9 },	/* DV_II_52_0 */
	{ 113, 8 },	/* DV_II_53_0 */
	{ 121, 6 },	/* DV_II_54_0 */
	{ 127, 5 },	/* DV_II_55_0 */
	{ 132, 5 }	/* DV_II_56_0 */
};

static uint32_t neon_prefix(const uint32_t *w)
{
	uint32x4_t acc = vdupq_n_u32(0);
	uint32x2_t folded;
	size_t i;

	UNROLL_TABLE
	for (i = 0; i < ARRAY_SIZE(neon_groups); i++) {
		const struct ubc_group4 *g = &neon_groups[i];
		uint32x4_t lo = vld1q_u32(w + g->lo);
		uint32x4_t hi = vld1q_u32(w + g->hi);
		uint32x4_t dvs = vld1q_u32(g->dvs);
		uint32x4_t set, fail;

		lo = vshlq_u32(lo, vdupq_n_s32(-(int32_t)g->lo_shift));
		hi = vshlq_u32(hi, vdupq_n_s32(-(int32_t)g->hi_shift));
		set = vtstq_u32(veorq_u32(lo, hi), vld1q_u32(g->test));
		/*
		 * The DVs of the lanes where the bit is not g->want. Each lane
		 * of set is all ones or zero, so a saturating subtraction keeps
		 * dvs where the bit is clear, and min keeps it where it is set.
		 */
		fail = g->want ? vqsubq_u32(dvs, set) : vminq_u32(set, dvs);
		acc = vorrq_u32(acc, fail);
	}

	folded = vorr_u32(vget_low_u32(acc), vget_high_u32(acc));
	return ~vget_lane_u32(vorr_u32(folded, vdup_lane_u32(folded, 1)), 0);
}

uint32_t sha1dc_ubc_check_neon(const uint32_t w[80])
{
	uint32_t mask = neon_prefix(w);
	/* Every check only clears bits, so an empty mask settles it. */
	if (!mask)
		return 0;
	return run_tail(w, mask, neon_tail_checks, neon_tail_spans);
}

#endif /* SHA1DC_HAVE_NEON */

/* sse2 form */

#ifdef SHA1DC_HAVE_SSE2

static const struct ubc_group4 sse2_groups[] = {
	{ 35, 0, 36, 5, 1,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 0 },
	  { DV_I_46_2_BIT | DV_I_49_2_BIT,
	    DV_I_47_2_BIT | DV_I_50_2_BIT | DV_II_46_2_BIT,
	    DV_I_48_2_BIT | DV_I_51_2_BIT,
	    DV_II_51_2_BIT } },
	{ 36, 0, 37, 5, 1,
	  { 1u << 0, 1u << 0, 1u << 1, 1u << 1 },
	  { DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_I_49_2_BIT,
	    DV_I_46_2_BIT | DV_I_50_2_BIT | DV_II_49_2_BIT } },
	{ 36, 0, 38, 0, 1,
	  { 1u << 4, 1u << 4, 1u << 1, 1u << 4 },
	  { DV_II_52_0_BIT | DV_II_54_0_BIT,
	    DV_I_43_0_BIT | DV_II_53_0_BIT | DV_II_55_0_BIT,
	    DV_I_49_2_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_II_55_0_BIT } },
	{ 37, 0, 40, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_43_0_BIT | DV_I_47_0_BIT | DV_II_46_0_BIT |
	    DV_II_53_0_BIT | DV_II_55_0_BIT,
	    DV_I_44_0_BIT | DV_I_48_0_BIT | DV_II_47_0_BIT |
	    DV_II_54_0_BIT | DV_II_56_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_49_0_BIT | DV_II_48_0_BIT |
	    DV_II_55_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_50_0_BIT | DV_II_49_0_BIT |
	    DV_II_56_0_BIT } },
	{ 36, 0, 40, 25, 0,
	  { 1u << 4, 1u << 5, 1u << 5, 1u << 5 },
	  { DV_I_46_0_BIT | DV_I_49_0_BIT | DV_II_45_0_BIT |
	    DV_II_48_0_BIT,
	    DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT } },
	{ 38, 0, 40, 0, 1,
	  { 1u << 4, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_44_0_BIT | DV_II_54_0_BIT | DV_II_56_0_BIT,
	    DV_I_50_2_BIT | DV_II_49_2_BIT,
	    DV_I_51_2_BIT | DV_II_50_2_BIT,
	    DV_II_46_2_BIT | DV_II_51_2_BIT } },
	{ 40, 0, 41, 5, 1,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_47_2_BIT | DV_I_51_2_BIT | DV_II_50_2_BIT,
	    DV_I_48_2_BIT | DV_II_46_2_BIT | DV_II_51_2_BIT,
	    DV_I_49_2_BIT,
	    DV_I_50_2_BIT } },
	{ 40, 0, 41, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_44_0_BIT | DV_I_47_0_BIT | DV_I_48_0_BIT | DV_II_46_0_BIT |
	    DV_II_47_0_BIT | DV_II_56_0_BIT,
	    DV_I_45_0_BIT | DV_I_48_0_BIT | DV_I_49_0_BIT | DV_II_47_0_BIT |
	    DV_II_48_0_BIT,
	    DV_I_46_0_BIT | DV_I_49_0_BIT | DV_I_50_0_BIT | DV_II_48_0_BIT |
	    DV_II_49_0_BIT,
	    DV_I_47_0_BIT | DV_I_50_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	    DV_II_49_0_BIT | DV_II_50_0_BIT } },
	{ 41, 0, 44, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_51_0_BIT |
	    DV_II_45_0_BIT | DV_II_50_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_52_0_BIT |
	    DV_II_46_0_BIT | DV_II_51_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT |
	    DV_II_47_0_BIT | DV_II_52_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT |
	    DV_II_48_0_BIT | DV_II_53_0_BIT } },
	{ 42, 0, 44, 0, 0,
	  { 1u << 6, 1u << 6, 1u << 6, 1u << 6 },
	  { DV_I_46_2_BIT | DV_I_48_2_BIT,
	    DV_I_47_2_BIT | DV_I_49_2_BIT,
	    DV_I_46_2_BIT | DV_I_48_2_BIT | DV_I_50_2_BIT,
	    DV_I_47_2_BIT | DV_I_49_2_BIT | DV_I_51_2_BIT } },
	{ 40, 0, 44, 0, 0,
	  { 1u << 6, 1u << 6, 1u << 29, 1u << 29 },
	  { DV_I_46_2_BIT,
	    DV_I_47_2_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT } },
	{ 44, 0, 45, 5, 1,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_51_2_BIT | DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT,
	    DV_II_46_2_BIT } },
	{ 44, 0, 45, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_48_0_BIT | DV_I_51_0_BIT | DV_I_52_0_BIT | DV_II_45_0_BIT |
	    DV_II_46_0_BIT | DV_II_50_0_BIT | DV_II_51_0_BIT,
	    DV_I_49_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	    DV_II_47_0_BIT | DV_II_51_0_BIT | DV_II_52_0_BIT,
	    DV_I_43_0_BIT | DV_I_50_0_BIT | DV_II_47_0_BIT |
	    DV_II_48_0_BIT | DV_II_52_0_BIT | DV_II_53_0_BIT,
	    DV_I_44_0_BIT | DV_I_51_0_BIT | DV_II_48_0_BIT |
	    DV_II_49_0_BIT | DV_II_53_0_BIT | DV_II_54_0_BIT } },
	{ 45, 5, 46, 0, 0,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_II_49_2_BIT,
	    DV_I_46_2_BIT | DV_II_50_2_BIT,
	    DV_I_47_2_BIT | DV_II_51_2_BIT,
	    DV_I_48_2_BIT } },
	{ 45, 0, 47, 0, 1,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 4 },
	  { DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT,
	    DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT,
	    DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT,
	    DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT } },
	{ 46, 0, 48, 0, 0,
	  { 1u << 6, 1u << 6, 1u << 6, 1u << 6 },
	  { DV_I_48_2_BIT | DV_I_50_2_BIT,
	    DV_I_49_2_BIT | DV_I_51_2_BIT,
	    DV_I_50_2_BIT | DV_II_46_2_BIT,
	    DV_I_51_2_BIT } },
	{ 45, 0, 48, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT |
	    DV_II_49_0_BIT | DV_II_54_0_BIT,
	    DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT |
	    DV_II_50_0_BIT | DV_II_55_0_BIT,
	    DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	    DV_II_51_0_BIT | DV_II_56_0_BIT,
	    DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	    DV_II_52_0_BIT } },
	{ 48, 0, 49, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_45_0_BIT | DV_I_52_0_BIT | DV_II_49_0_BIT |
	    DV_II_50_0_BIT | DV_II_54_0_BIT | DV_II_55_0_BIT,
	    DV_I_46_0_BIT | DV_II_45_0_BIT | DV_II_50_0_BIT |
	    DV_II_51_0_BIT | DV_II_55_0_BIT | DV_II_56_0_BIT,
	    DV_I_47_0_BIT | DV_II_46_0_BIT | DV_II_51_0_BIT |
	    DV_II_52_0_BIT | DV_II_56_0_BIT,
	    DV_I_48_0_BIT | DV_II_47_0_BIT | DV_II_52_0_BIT |
	    DV_II_53_0_BIT } },
	{ 49, 5, 50, 0, 0,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_49_2_BIT,
	    DV_I_50_2_BIT | DV_II_46_2_BIT,
	    DV_I_51_2_BIT,
	    0 } },
	{ 49, 0, 52, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	    DV_II_47_0_BIT | DV_II_53_0_BIT,
	    DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	    DV_II_48_0_BIT | DV_II_54_0_BIT,
	    DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_49_0_BIT |
	    DV_II_55_0_BIT,
	    DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_50_0_BIT |
	    DV_II_56_0_BIT } },
	{ 49, 0, 53, 0, 1,
	  { 1u << 29, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_II_45_0_BIT,
	    DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT } },
	{ 52, 0, 53, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_49_0_BIT | DV_II_45_0_BIT | DV_II_48_0_BIT |
	    DV_II_53_0_BIT | DV_II_54_0_BIT,
	    DV_I_50_0_BIT | DV_II_46_0_BIT | DV_II_49_0_BIT |
	    DV_II_54_0_BIT | DV_II_55_0_BIT,
	    DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_50_0_BIT |
	    DV_II_55_0_BIT | DV_II_56_0_BIT,
	    DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_51_0_BIT |
	    DV_II_56_0_BIT } },
	{ 53, 5, 54, 0, 0,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT,
	    0 } },
	{ 52, 0, 55, 0, 1,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_48_0_BIT,
	    DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_49_0_BIT,
	    DV_II_49_0_BIT | DV_II_50_0_BIT,
	    DV_II_50_0_BIT | DV_II_51_0_BIT } },
	{ 53, 0, 56, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_II_49_0_BIT | DV_II_51_0_BIT,
	    DV_II_50_0_BIT | DV_II_52_0_BIT,
	    DV_II_51_0_BIT | DV_II_53_0_BIT,
	    DV_II_52_0_BIT | DV_II_54_0_BIT } },
	{ 60, 0, 61, 5, 1,
	  { 1u << 0, 1u << 2, 1u << 2, 1u << 2 },
	  { DV_I_45_0_BIT | DV_II_45_0_BIT,
	    DV_I_46_2_BIT | DV_II_46_2_BIT,
	    DV_I_47_2_BIT,
	    DV_I_48_2_BIT } }
};

static const struct ubc_cond sse2_tail_checks[] = {
	/* DV_I_43_0 */
	{ 41, 4, 43, 4, 1 },
	{ 58, 0, 59, 5, 1 },
	{ 58, 0, 63, 30, 1 },
	{ 61, 1, 62, 6, 1 },
	{ 43, 4, 47, 29, 0 },
	/* DV_I_44_0 */
	{ 59, 0, 60, 5, 1 },
	{ 59, 0, 64, 30, 1 },
	{ 62, 1, 63, 6, 1 },
	{ 38, 4, 42, 4, 0 },
	{ 44, 4, 48, 29, 0 },
	/* DV_I_45_0 */
	{ 41, 4, 43, 4, 1 },
	{ 63, 1, 64, 6, 1 },
	{ 35, 4, 39, 29, 0 },
	/* DV_I_46_0 */
	{ 61, 0, 62, 5, 1 },
	/* DV_I_47_0 */
	{ 41, 4, 43, 4, 1 },
	{ 45, 4, 47, 4, 1 },
	{ 62, 0, 63, 5, 1 },
	{ 37, 4, 41, 29, 0 },
	/* DV_I_48_0 */
	{ 46, 4, 48, 4, 1 },
	{ 63, 0, 64, 5, 1 },
	{ 35, 4, 39, 29, 0 },
	{ 38, 4, 42, 29, 0 },
	/* DV_I_49_0 */
	{ 45, 4, 47, 4, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 45, 4, 49, 4, 0 },
	/* DV_I_50_0 */
	{ 36, 4, 37, 4, 1 },
	{ 46, 4, 48, 4, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 40, 4, 44, 29, 0 },
	/* DV_I_51_0 */
	{ 37, 4, 38, 4, 1 },
	{ 45, 4, 47, 4, 1 },
	{ 35, 3, 39, 28, 0 },
	{ 38, 4, 42, 29, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 45, 4, 49, 4, 0 },
	/* DV_I_51_2 */
	{ 35, 5, 39, 30, 0 },
	{ 37, 1, 37, 6, 0 },
	/* DV_I_52_0 */
	{ 38, 4, 39, 4, 1 },
	{ 46, 4, 48, 4, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	/* DV_II_45_0 */
	{ 63, 1, 64, 6, 1 },
	{ 41, 4, 45, 29, 0 },
	/* DV_II_46_0 */
	{ 61, 0, 62, 5, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	/* DV_II_47_0 */
	{ 62, 0, 63, 5, 1 },
	{ 35, 3, 39, 28, 0 },
	{ 35, 4, 39, 29, 0 },
	{ 38, 4, 42, 29, 0 },
	{ 43, 4, 47, 29, 0 },
	/* DV_II_48_0 */
	{ 35, 30, 36, 3, 1 },
	{ 35, 30, 40, 28, 1 },
	{ 63, 0, 64, 5, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 44, 4, 48, 29, 0 },
	/* DV_II_49_0 */
	{ 36, 30, 37, 3, 1 },
	{ 36, 30, 41, 28, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 40, 4, 44, 29, 0 },
	/* DV_II_49_2 */
	{ 50, 1, 51, 6, 1 },
	/* DV_II_50_0 */
	{ 37, 30, 38, 3, 1 },
	{ 37, 30, 42, 28, 1 },
	{ 38, 4, 42, 29, 0 },
	{ 41, 4, 45, 29, 0 },
	/* DV_II_50_2 */
	{ 51, 1, 52, 6, 1 },
	/* DV_II_51_0 */
	{ 38, 30, 39, 3, 1 },
	{ 38, 30, 43, 28, 1 },
	{ 56, 29, 59, 29, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 42, 4, 46, 29, 0 },
	/* DV_II_51_2 */
	{ 52, 1, 53, 6, 1 },
	/* DV_II_52_0 */
	{ 39, 30, 40, 3, 1 },
	{ 39, 30, 44, 28, 1 },
	{ 54, 4, 56, 4, 1 },
	{ 54, 4, 60, 29, 1 },
	{ 56, 29, 59, 29, 1 },
	{ 40, 4, 44, 29, 0 },
	{ 43, 4, 47, 29, 0 },
	/* DV_II_53_0 */
	{ 55, 4, 57, 4, 1 },
	{ 55, 4, 61, 29, 1 },
	{ 41, 3, 45, 28, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 55, 4, 57, 29, 0 },
	/* DV_II_54_0 */
	{ 42, 3, 46, 28, 0 },
	{ 42, 4, 46, 29, 0 },
	{ 56, 4, 58, 29, 0 },
	{ 58, 4, 62, 29, 0 },
	/* DV_II_55_0 */
	{ 43, 3, 47, 28, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 57, 4, 59, 29, 0 },
	{ 59, 4, 63, 29, 0 },
	/* DV_II_56_0 */
	{ 38, 4, 42, 4, 0 },
	{ 44, 3, 48, 28, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 60, 4, 64, 29, 0 }
};

static const struct tail_span sse2_tail_spans[32] = {
	{ 0, 5 },	/* DV_I_43_0 */
	{ 5, 5 },	/* DV_I_44_0 */
	{ 10, 3 },	/* DV_I_45_0 */
	{ 13, 1 },	/* DV_I_46_0 */
	{ 14, 0 },	/* DV_I_46_2 */
	{ 14, 4 },	/* DV_I_47_0 */
	{ 18, 0 },	/* DV_I_47_2 */
	{ 18, 4 },	/* DV_I_48_0 */
	{ 22, 0 },	/* DV_I_48_2 */
	{ 22, 3 },	/* DV_I_49_0 */
	{ 25, 0 },	/* DV_I_49_2 */
	{ 25, 4 },	/* DV_I_50_0 */
	{ 29, 0 },	/* DV_I_50_2 */
	{ 29, 6 },	/* DV_I_51_0 */
	{ 35, 2 },	/* DV_I_51_2 */
	{ 37, 4 },	/* DV_I_52_0 */
	{ 41, 2 },	/* DV_II_45_0 */
	{ 43, 3 },	/* DV_II_46_0 */
	{ 46, 0 },	/* DV_II_46_2 */
	{ 46, 5 },	/* DV_II_47_0 */
	{ 51, 5 },	/* DV_II_48_0 */
	{ 56, 4 },	/* DV_II_49_0 */
	{ 60, 1 },	/* DV_II_49_2 */
	{ 61, 4 },	/* DV_II_50_0 */
	{ 65, 1 },	/* DV_II_50_2 */
	{ 66, 5 },	/* DV_II_51_0 */
	{ 71, 1 },	/* DV_II_51_2 */
	{ 72, 7 },	/* DV_II_52_0 */
	{ 79, 6 },	/* DV_II_53_0 */
	{ 85, 4 },	/* DV_II_54_0 */
	{ 89, 4 },	/* DV_II_55_0 */
	{ 93, 4 }	/* DV_II_56_0 */
};

SHA1DC_TARGET_SSE2
static uint32_t sse2_prefix(const uint32_t *w)
{
	const __m128i zero = _mm_setzero_si128();
	__m128i acc = zero;
	size_t i;

	UNROLL_TABLE
	for (i = 0; i < ARRAY_SIZE(sse2_groups); i++) {
		const struct ubc_group4 *g = &sse2_groups[i];
		__m128i lo = _mm_loadu_si128((const __m128i *)(w + g->lo));
		__m128i hi = _mm_loadu_si128((const __m128i *)(w + g->hi));
		__m128i test = _mm_loadu_si128((const __m128i *)g->test);
		__m128i dvs = _mm_loadu_si128((const __m128i *)g->dvs);
		__m128i clear, fail;

		lo = _mm_srl_epi32(lo, _mm_cvtsi32_si128(g->lo_shift));
		hi = _mm_srl_epi32(hi, _mm_cvtsi32_si128(g->hi_shift));
		clear = _mm_and_si128(_mm_xor_si128(lo, hi), test);
		clear = _mm_cmpeq_epi32(clear, zero);
		/* The DVs of the lanes where the bit is not g->want. */
		fail = g->want ? _mm_and_si128(clear, dvs) :
				 _mm_andnot_si128(clear, dvs);
		acc = _mm_or_si128(acc, fail);
	}

	acc = _mm_or_si128(acc, _mm_shuffle_epi32(acc, 0x4E));
	acc = _mm_or_si128(acc, _mm_shuffle_epi32(acc, 0xB1));
	return ~(uint32_t)_mm_cvtsi128_si32(acc);
}

SHA1DC_TARGET_SSE2
uint32_t sha1dc_ubc_check_sse2(const uint32_t w[80])
{
	uint32_t mask = sse2_prefix(w);
	/* Every check only clears bits, so an empty mask settles it. */
	if (!mask)
		return 0;
	return run_tail(w, mask, sse2_tail_checks, sse2_tail_spans);
}

#endif /* SHA1DC_HAVE_SSE2 */

/* avx2 form */

#ifdef SHA1DC_HAVE_AVX2

static const struct ubc_group8 avx2_groups[] = {
	{ 35, 0, 36, 5, 1,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 0,
	    1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_46_2_BIT | DV_I_49_2_BIT,
	    DV_I_47_2_BIT | DV_I_50_2_BIT | DV_II_46_2_BIT,
	    DV_I_48_2_BIT | DV_I_51_2_BIT,
	    DV_II_51_2_BIT,
	    DV_I_46_2_BIT | DV_I_50_2_BIT | DV_II_49_2_BIT,
	    DV_I_47_2_BIT | DV_I_51_2_BIT | DV_II_50_2_BIT,
	    DV_I_48_2_BIT | DV_II_46_2_BIT | DV_II_51_2_BIT,
	    DV_I_49_2_BIT } },
	{ 36, 0, 38, 0, 1,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4,
	    1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_II_52_0_BIT | DV_II_54_0_BIT,
	    DV_I_43_0_BIT | DV_II_53_0_BIT | DV_II_55_0_BIT,
	    DV_I_44_0_BIT | DV_II_54_0_BIT | DV_II_56_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_II_55_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_II_56_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT,
	    DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT } },
	{ 37, 0, 38, 5, 1,
	  { 1u << 0, 1u << 1, 0, 0,
	    0, 0, 1u << 1, 1u << 1 },
	  { DV_II_50_2_BIT,
	    DV_I_49_2_BIT,
	    0,
	    0,
	    0,
	    0,
	    DV_I_50_2_BIT,
	    DV_I_51_2_BIT | DV_II_49_2_BIT } },
	{ 35, 0, 39, 25, 0,
	  { 1u << 5, 1u << 4, 1u << 5, 1u << 5,
	    1u << 5, 1u << 3, 1u << 3, 1u << 4 },
	  { DV_I_51_2_BIT,
	    DV_I_46_0_BIT | DV_I_49_0_BIT | DV_II_45_0_BIT |
	    DV_II_48_0_BIT,
	    DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT,
	    DV_II_52_0_BIT,
	    DV_II_53_0_BIT,
	    DV_I_52_0_BIT | DV_II_46_0_BIT | DV_II_51_0_BIT |
	    DV_II_54_0_BIT } },
	{ 37, 0, 40, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4,
	    1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_43_0_BIT | DV_I_47_0_BIT | DV_II_46_0_BIT |
	    DV_II_53_0_BIT | DV_II_55_0_BIT,
	    DV_I_44_0_BIT | DV_I_48_0_BIT | DV_II_47_0_BIT |
	    DV_II_54_0_BIT | DV_II_56_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_49_0_BIT | DV_II_48_0_BIT |
	    DV_II_55_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_50_0_BIT | DV_II_49_0_BIT |
	    DV_II_56_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_51_0_BIT |
	    DV_II_45_0_BIT | DV_II_50_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_52_0_BIT |
	    DV_II_46_0_BIT | DV_II_51_0_BIT,
	    DV_I_43_0_BIT | DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT |
	    DV_II_47_0_BIT | DV_II_52_0_BIT,
	    DV_I_44_0_BIT | DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT |
	    DV_II_48_0_BIT | DV_II_53_0_BIT } },
	{ 39, 5, 40, 0, 0,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1,
	    1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_49_2_BIT,
	    DV_I_50_2_BIT | DV_II_49_2_BIT,
	    DV_I_51_2_BIT | DV_II_50_2_BIT,
	    DV_II_46_2_BIT | DV_II_51_2_BIT,
	    0,
	    0,
	    DV_II_49_2_BIT,
	    DV_I_46_2_BIT | DV_II_50_2_BIT } },
	{ 40, 0, 41, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29,
	    1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_44_0_BIT | DV_I_47_0_BIT | DV_I_48_0_BIT | DV_II_46_0_BIT |
	    DV_II_47_0_BIT | DV_II_56_0_BIT,
	    DV_I_45_0_BIT | DV_I_48_0_BIT | DV_I_49_0_BIT | DV_II_47_0_BIT |
	    DV_II_48_0_BIT,
	    DV_I_46_0_BIT | DV_I_49_0_BIT | DV_I_50_0_BIT | DV_II_48_0_BIT |
	    DV_II_49_0_BIT,
	    DV_I_47_0_BIT | DV_I_50_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	    DV_II_49_0_BIT | DV_II_50_0_BIT,
	    DV_I_48_0_BIT | DV_I_51_0_BIT | DV_I_52_0_BIT | DV_II_45_0_BIT |
	    DV_II_46_0_BIT | DV_II_50_0_BIT | DV_II_51_0_BIT,
	    DV_I_49_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	    DV_II_47_0_BIT | DV_II_51_0_BIT | DV_II_52_0_BIT,
	    DV_I_43_0_BIT | DV_I_50_0_BIT | DV_II_47_0_BIT |
	    DV_II_48_0_BIT | DV_II_52_0_BIT | DV_II_53_0_BIT,
	    DV_I_44_0_BIT | DV_I_51_0_BIT | DV_II_48_0_BIT |
	    DV_II_49_0_BIT | DV_II_53_0_BIT | DV_II_54_0_BIT } },
	{ 40, 0, 42, 0, 0,
	  { 1u << 6, 1u << 6, 1u << 6, 1u << 6,
	    1u << 6, 1u << 6, 1u << 6, 1u << 6 },
	  { DV_I_46_2_BIT,
	    DV_I_47_2_BIT,
	    DV_I_46_2_BIT | DV_I_48_2_BIT,
	    DV_I_47_2_BIT | DV_I_49_2_BIT,
	    DV_I_46_2_BIT | DV_I_48_2_BIT | DV_I_50_2_BIT,
	    DV_I_47_2_BIT | DV_I_49_2_BIT | DV_I_51_2_BIT,
	    DV_I_48_2_BIT | DV_I_50_2_BIT,
	    DV_I_49_2_BIT | DV_I_51_2_BIT } },
	{ 45, 0, 46, 5, 1,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1,
	    1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_II_50_2_BIT,
	    DV_II_51_2_BIT,
	    DV_II_46_2_BIT,
	    0,
	    0,
	    DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT } },
	{ 45, 0, 48, 25, 0,
	  { 1u << 4, 1u << 4, 1u << 4, 1u << 4,
	    1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_45_0_BIT | DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT |
	    DV_II_49_0_BIT | DV_II_54_0_BIT,
	    DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT |
	    DV_II_50_0_BIT | DV_II_55_0_BIT,
	    DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	    DV_II_51_0_BIT | DV_II_56_0_BIT,
	    DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	    DV_II_52_0_BIT,
	    DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT |
	    DV_II_47_0_BIT | DV_II_53_0_BIT,
	    DV_I_50_0_BIT | DV_I_52_0_BIT | DV_II_46_0_BIT |
	    DV_II_48_0_BIT | DV_II_54_0_BIT,
	    DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_49_0_BIT |
	    DV_II_55_0_BIT,
	    DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_50_0_BIT |
	    DV_II_56_0_BIT } },
	{ 47, 5, 48, 0, 0,
	  { 1u << 1, 1u << 1, 1u << 1, 1u << 1,
	    1u << 1, 1u << 1, 1u << 1, 1u << 1 },
	  { DV_I_47_2_BIT | DV_II_51_2_BIT,
	    DV_I_48_2_BIT,
	    DV_I_49_2_BIT,
	    DV_I_50_2_BIT | DV_II_46_2_BIT,
	    DV_I_51_2_BIT,
	    0,
	    DV_II_49_2_BIT,
	    DV_II_50_2_BIT } },
	{ 47, 0, 49, 0, 1,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29,
	    1u << 4, 1u << 4, 1u << 4, 1u << 4 },
	  { DV_I_46_0_BIT | DV_I_48_0_BIT | DV_I_50_0_BIT,
	    DV_I_47_0_BIT | DV_I_49_0_BIT | DV_I_51_0_BIT,
	    DV_I_48_0_BIT | DV_I_50_0_BIT | DV_I_52_0_BIT,
	    DV_I_49_0_BIT | DV_I_51_0_BIT | DV_II_45_0_BIT,
	    DV_II_49_0_BIT,
	    DV_II_50_0_BIT,
	    DV_II_51_0_BIT,
	    DV_II_52_0_BIT } },
	{ 48, 0, 49, 0, 0,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29,
	    1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_45_0_BIT | DV_I_52_0_BIT | DV_II_49_0_BIT |
	    DV_II_50_0_BIT | DV_II_54_0_BIT | DV_II_55_0_BIT,
	    DV_I_46_0_BIT | DV_II_45_0_BIT | DV_II_50_0_BIT |
	    DV_II_51_0_BIT | DV_II_55_0_BIT | DV_II_56_0_BIT,
	    DV_I_47_0_BIT | DV_II_46_0_BIT | DV_II_51_0_BIT |
	    DV_II_52_0_BIT | DV_II_56_0_BIT,
	    DV_I_48_0_BIT | DV_II_47_0_BIT | DV_II_52_0_BIT |
	    DV_II_53_0_BIT,
	    DV_I_49_0_BIT | DV_II_45_0_BIT | DV_II_48_0_BIT |
	    DV_II_53_0_BIT | DV_II_54_0_BIT,
	    DV_I_50_0_BIT | DV_II_46_0_BIT | DV_II_49_0_BIT |
	    DV_II_54_0_BIT | DV_II_55_0_BIT,
	    DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_50_0_BIT |
	    DV_II_55_0_BIT | DV_II_56_0_BIT,
	    DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_51_0_BIT |
	    DV_II_56_0_BIT } },
	{ 48, 0, 50, 0, 0,
	  { 1u << 6, 1u << 6, 1u << 6, 1u << 6,
	    1u << 6, 1u << 6, 1u << 6, 1u << 6 },
	  { DV_I_50_2_BIT | DV_II_46_2_BIT,
	    DV_I_51_2_BIT,
	    0,
	    DV_II_49_2_BIT,
	    DV_II_50_2_BIT,
	    DV_II_51_2_BIT,
	    0,
	    0 } },
	{ 51, 0, 54, 0, 1,
	  { 1u << 29, 1u << 29, 1u << 29, 1u << 29,
	    1u << 29, 1u << 29, 1u << 29, 1u << 29 },
	  { DV_I_50_0_BIT | DV_II_46_0_BIT | DV_II_47_0_BIT,
	    DV_I_51_0_BIT | DV_II_47_0_BIT | DV_II_48_0_BIT,
	    DV_I_52_0_BIT | DV_II_48_0_BIT | DV_II_49_0_BIT,
	    DV_II_49_0_BIT | DV_II_50_0_BIT,
	    DV_II_50_0_BIT | DV_II_51_0_BIT,
	    DV_II_51_0_BIT | DV_II_52_0_BIT,
	    DV_II_52_0_BIT,
	    DV_II_53_0_BIT } },
	{ 58, 0, 59, 5, 1,
	  { 1u << 0, 1u << 0, 1u << 0, 1u << 2,
	    1u << 2, 1u << 2, 0, 0 },
	  { DV_I_43_0_BIT,
	    DV_I_44_0_BIT,
	    DV_I_45_0_BIT | DV_II_45_0_BIT,
	    DV_I_46_2_BIT | DV_II_46_2_BIT,
	    DV_I_47_2_BIT,
	    DV_I_48_2_BIT,
	    0,
	    0 } }
};

static const struct ubc_cond avx2_tail_checks[] = {
	/* DV_I_43_0 */
	{ 58, 0, 63, 30, 1 },
	{ 61, 1, 62, 6, 1 },
	{ 43, 4, 47, 29, 0 },
	/* DV_I_44_0 */
	{ 59, 0, 64, 30, 1 },
	{ 62, 1, 63, 6, 1 },
	{ 44, 4, 48, 29, 0 },
	/* DV_I_45_0 */
	{ 63, 1, 64, 6, 1 },
	{ 35, 4, 39, 29, 0 },
	/* DV_I_46_0 */
	{ 61, 0, 62, 5, 1 },
	/* DV_I_47_0 */
	{ 62, 0, 63, 5, 1 },
	{ 37, 4, 41, 29, 0 },
	/* DV_I_48_0 */
	{ 63, 0, 64, 5, 1 },
	{ 35, 4, 39, 29, 0 },
	{ 38, 4, 42, 29, 0 },
	/* DV_I_49_0 */
	{ 39, 4, 43, 29, 0 },
	/* DV_I_50_0 */
	{ 36, 4, 37, 4, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 40, 4, 44, 29, 0 },
	{ 46, 4, 50, 4, 0 },
	/* DV_I_51_0 */
	{ 37, 4, 38, 4, 1 },
	{ 35, 3, 39, 28, 0 },
	{ 38, 4, 42, 29, 0 },
	{ 41, 4, 45, 29, 0 },
	/* DV_I_51_2 */
	{ 37, 1, 37, 6, 0 },
	/* DV_I_52_0 */
	{ 38, 4, 39, 4, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 46, 4, 50, 4, 0 },
	/* DV_II_45_0 */
	{ 63, 1, 64, 6, 1 },
	{ 41, 4, 45, 29, 0 },
	/* DV_II_46_0 */
	{ 61, 0, 62, 5, 1 },
	{ 37, 4, 41, 29, 0 },
	/* DV_II_47_0 */
	{ 62, 0, 63, 5, 1 },
	{ 35, 3, 39, 28, 0 },
	{ 35, 4, 39, 29, 0 },
	{ 38, 4, 42, 29, 0 },
	{ 43, 4, 47, 29, 0 },
	/* DV_II_48_0 */
	{ 35, 30, 36, 3, 1 },
	{ 35, 30, 40, 28, 1 },
	{ 63, 0, 64, 5, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 44, 4, 48, 29, 0 },
	/* DV_II_49_0 */
	{ 36, 30, 37, 3, 1 },
	{ 36, 30, 41, 28, 1 },
	{ 37, 4, 41, 29, 0 },
	{ 40, 4, 44, 29, 0 },
	/* DV_II_49_2 */
	{ 36, 0, 37, 5, 1 },
	/* DV_II_50_0 */
	{ 37, 30, 38, 3, 1 },
	{ 37, 30, 42, 28, 1 },
	{ 38, 4, 42, 29, 0 },
	{ 41, 4, 45, 29, 0 },
	{ 54, 4, 57, 29, 0 },
	/* DV_II_51_0 */
	{ 38, 30, 39, 3, 1 },
	{ 38, 30, 43, 28, 1 },
	{ 39, 4, 43, 29, 0 },
	{ 55, 4, 58, 29, 0 },
	/* DV_II_51_2 */
	{ 52, 1, 56, 1, 1 },
	/* DV_II_52_0 */
	{ 39, 30, 40, 3, 1 },
	{ 40, 4, 44, 29, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 54, 4, 57, 29, 0 },
	{ 56, 4, 59, 29, 0 },
	/* DV_II_53_0 */
	{ 55, 4, 57, 4, 1 },
	{ 41, 4, 45, 29, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 55, 4, 58, 29, 0 },
	{ 55, 4, 57, 29, 0 },
	/* DV_II_54_0 */
	{ 42, 3, 46, 28, 0 },
	{ 56, 4, 59, 29, 0 },
	{ 56, 4, 58, 29, 0 },
	{ 58, 4, 62, 29, 0 },
	/* DV_II_55_0 */
	{ 43, 3, 47, 28, 0 },
	{ 43, 4, 47, 29, 0 },
	{ 57, 4, 59, 29, 0 },
	{ 59, 4, 63, 29, 0 },
	/* DV_II_56_0 */
	{ 44, 3, 48, 28, 0 },
	{ 44, 4, 48, 29, 0 },
	{ 60, 4, 64, 29, 0 }
};

static const struct tail_span avx2_tail_spans[32] = {
	{ 0, 3 },	/* DV_I_43_0 */
	{ 3, 3 },	/* DV_I_44_0 */
	{ 6, 2 },	/* DV_I_45_0 */
	{ 8, 1 },	/* DV_I_46_0 */
	{ 9, 0 },	/* DV_I_46_2 */
	{ 9, 2 },	/* DV_I_47_0 */
	{ 11, 0 },	/* DV_I_47_2 */
	{ 11, 3 },	/* DV_I_48_0 */
	{ 14, 0 },	/* DV_I_48_2 */
	{ 14, 1 },	/* DV_I_49_0 */
	{ 15, 0 },	/* DV_I_49_2 */
	{ 15, 4 },	/* DV_I_50_0 */
	{ 19, 0 },	/* DV_I_50_2 */
	{ 19, 4 },	/* DV_I_51_0 */
	{ 23, 1 },	/* DV_I_51_2 */
	{ 24, 3 },	/* DV_I_52_0 */
	{ 27, 2 },	/* DV_II_45_0 */
	{ 29, 2 },	/* DV_II_46_0 */
	{ 31, 0 },	/* DV_II_46_2 */
	{ 31, 5 },	/* DV_II_47_0 */
	{ 36, 5 },	/* DV_II_48_0 */
	{ 41, 4 },	/* DV_II_49_0 */
	{ 45, 1 },	/* DV_II_49_2 */
	{ 46, 5 },	/* DV_II_50_0 */
	{ 51, 0 },	/* DV_II_50_2 */
	{ 51, 4 },	/* DV_II_51_0 */
	{ 55, 1 },	/* DV_II_51_2 */
	{ 56, 5 },	/* DV_II_52_0 */
	{ 61, 5 },	/* DV_II_53_0 */
	{ 66, 4 },	/* DV_II_54_0 */
	{ 70, 4 },	/* DV_II_55_0 */
	{ 74, 3 }	/* DV_II_56_0 */
};

SHA1DC_TARGET_AVX2
static uint32_t avx2_prefix(const uint32_t *w)
{
	const __m256i zero = _mm256_setzero_si256();
	__m256i acc = zero;
	__m128i half;
	size_t i;

	UNROLL_TABLE
	for (i = 0; i < ARRAY_SIZE(avx2_groups); i++) {
		const struct ubc_group8 *g = &avx2_groups[i];
		__m256i lo = _mm256_loadu_si256((const __m256i *)(w + g->lo));
		__m256i hi = _mm256_loadu_si256((const __m256i *)(w + g->hi));
		__m256i test = _mm256_loadu_si256((const __m256i *)g->test);
		__m256i dvs = _mm256_loadu_si256((const __m256i *)g->dvs);
		__m256i clear, fail;

		lo = _mm256_srl_epi32(lo, _mm_cvtsi32_si128(g->lo_shift));
		hi = _mm256_srl_epi32(hi, _mm_cvtsi32_si128(g->hi_shift));
		clear = _mm256_and_si256(_mm256_xor_si256(lo, hi), test);
		clear = _mm256_cmpeq_epi32(clear, zero);
		/* The DVs of the lanes where the bit is not g->want. */
		fail = g->want ? _mm256_and_si256(clear, dvs) :
				 _mm256_andnot_si256(clear, dvs);
		acc = _mm256_or_si256(acc, fail);
	}

	half = _mm_or_si128(_mm256_castsi256_si128(acc),
			    _mm256_extracti128_si256(acc, 1));
	half = _mm_or_si128(half, _mm_shuffle_epi32(half, 0x4E));
	half = _mm_or_si128(half, _mm_shuffle_epi32(half, 0xB1));
	return ~(uint32_t)_mm_cvtsi128_si32(half);
}

SHA1DC_TARGET_AVX2
uint32_t sha1dc_ubc_check_avx2(const uint32_t w[80])
{
	uint32_t mask = avx2_prefix(w);
	/* Every check only clears bits, so an empty mask settles it. */
	if (!mask)
		return 0;
	return run_tail(w, mask, avx2_tail_checks, avx2_tail_spans);
}

#endif /* SHA1DC_HAVE_AVX2 */
