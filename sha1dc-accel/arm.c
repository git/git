/*
 * The SHA-1 compression and recompression on the ARMv8 SHA-1 instructions,
 * for sha1.c. `vsha1{c,p,m}q_u32` do four steps at a time on `abcd`, with
 * the fifth working word carried apart and derived by `vsha1h_u32`.
 */

#include "../git-compat-util.h"
#include "internal.h"

#ifdef SHA1DC_HAVE_ARMV8

#if !defined(__ARM_FEATURE_SHA2) && !defined(__ARM_FEATURE_CRYPTO) && \
	defined(__linux__)
#include <sys/auxv.h>
#ifndef HWCAP_SHA1
#define HWCAP_SHA1 (1 << 5)
#endif
#endif

int sha1dc_armv8_available(void)
{
#if defined(__ARM_FEATURE_SHA2) || defined(__ARM_FEATURE_CRYPTO) || \
	defined(__APPLE__)
	return 1;
#elif defined(__linux__)
	return !!(getauxval(AT_HWCAP) & HWCAP_SHA1);
#else
	return 0;
#endif
}

#define K0 0x5A827999
#define K1 0x6ED9EBA1
#define K2 0x8F1BBCDC
#define K3 0xCA62C1D6

/* Big-endian message words t..t+3. */
#define MSG(t) vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + 4 * (t))))

/*
 * Four steps of a compression that keeps the schedule four groups ahead:
 * `f` the round instruction, `e` the fifth word in, `next` where the one
 * for the next group goes, `wk` the schedule words plus the constant.
 */
#define QUAD(f, e, next, wk) \
	do { \
		next = vsha1h_u32(vgetq_lane_u32(abcd, 0)); \
		abcd = f(abcd, e, wk); \
	} while (0)

SHA1DC_TARGET_ARMV8
void sha1dc_compress_armv8(uint32_t ihv[5], const unsigned char *block,
			   uint32_t w[80], uint32_t at_60[5], uint32_t at_64[5])
{
	uint32x4_t abcd = vld1q_u32(ihv);
	const uint32x4_t abcd_in = abcd;
	uint32_t e0 = ihv[4], e1;
	const uint32_t e_in = e0;
	const uint32x4_t k0 = vdupq_n_u32(K0), k1 = vdupq_n_u32(K1);
	const uint32x4_t k2 = vdupq_n_u32(K2), k3 = vdupq_n_u32(K3);
	uint32x4_t msg0 = MSG(0), msg1 = MSG(4), msg2 = MSG(8), msg3 = MSG(12);
	uint32x4_t tmp0, tmp1;

	vst1q_u32(w + 0, msg0);
	vst1q_u32(w + 4, msg1);
	vst1q_u32(w + 8, msg2);
	vst1q_u32(w + 12, msg3);

	tmp0 = vaddq_u32(msg0, k0);
	tmp1 = vaddq_u32(msg1, k0);

	/* Steps 0-3 */
	QUAD(vsha1cq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg2, k0);
	msg0 = vsha1su0q_u32(msg0, msg1, msg2);

	/* Steps 4-7 */
	QUAD(vsha1cq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg3, k0);
	msg0 = vsha1su1q_u32(msg0, msg3);
	vst1q_u32(w + 16, msg0);
	msg1 = vsha1su0q_u32(msg1, msg2, msg3);

	/* Steps 8-11 */
	QUAD(vsha1cq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg0, k0);
	msg1 = vsha1su1q_u32(msg1, msg0);
	vst1q_u32(w + 20, msg1);
	msg2 = vsha1su0q_u32(msg2, msg3, msg0);

	/* Steps 12-15 */
	QUAD(vsha1cq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg1, k1);
	msg2 = vsha1su1q_u32(msg2, msg1);
	vst1q_u32(w + 24, msg2);
	msg3 = vsha1su0q_u32(msg3, msg0, msg1);

	/* Steps 16-19 */
	QUAD(vsha1cq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg2, k1);
	msg3 = vsha1su1q_u32(msg3, msg2);
	vst1q_u32(w + 28, msg3);
	msg0 = vsha1su0q_u32(msg0, msg1, msg2);

	/* Steps 20-23 */
	QUAD(vsha1pq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg3, k1);
	msg0 = vsha1su1q_u32(msg0, msg3);
	vst1q_u32(w + 32, msg0);
	msg1 = vsha1su0q_u32(msg1, msg2, msg3);

	/* Steps 24-27 */
	QUAD(vsha1pq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg0, k1);
	msg1 = vsha1su1q_u32(msg1, msg0);
	vst1q_u32(w + 36, msg1);
	msg2 = vsha1su0q_u32(msg2, msg3, msg0);

	/* Steps 28-31 */
	QUAD(vsha1pq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg1, k1);
	msg2 = vsha1su1q_u32(msg2, msg1);
	vst1q_u32(w + 40, msg2);
	msg3 = vsha1su0q_u32(msg3, msg0, msg1);

	/* Steps 32-35 */
	QUAD(vsha1pq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg2, k2);
	msg3 = vsha1su1q_u32(msg3, msg2);
	vst1q_u32(w + 44, msg3);
	msg0 = vsha1su0q_u32(msg0, msg1, msg2);

	/* Steps 36-39 */
	QUAD(vsha1pq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg3, k2);
	msg0 = vsha1su1q_u32(msg0, msg3);
	vst1q_u32(w + 48, msg0);
	msg1 = vsha1su0q_u32(msg1, msg2, msg3);

	/* Steps 40-43 */
	QUAD(vsha1mq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg0, k2);
	msg1 = vsha1su1q_u32(msg1, msg0);
	vst1q_u32(w + 52, msg1);
	msg2 = vsha1su0q_u32(msg2, msg3, msg0);

	/* Steps 44-47 */
	QUAD(vsha1mq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg1, k2);
	msg2 = vsha1su1q_u32(msg2, msg1);
	vst1q_u32(w + 56, msg2);
	msg3 = vsha1su0q_u32(msg3, msg0, msg1);

	/* Steps 48-51 */
	QUAD(vsha1mq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg2, k2);
	msg3 = vsha1su1q_u32(msg3, msg2);
	vst1q_u32(w + 60, msg3);
	msg0 = vsha1su0q_u32(msg0, msg1, msg2);

	/* Steps 52-55 */
	QUAD(vsha1mq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg3, k3);
	msg0 = vsha1su1q_u32(msg0, msg3);
	vst1q_u32(w + 64, msg0);
	msg1 = vsha1su0q_u32(msg1, msg2, msg3);

	/* Steps 56-59 */
	QUAD(vsha1mq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg0, k3);
	msg1 = vsha1su1q_u32(msg1, msg0);
	vst1q_u32(w + 68, msg1);
	msg2 = vsha1su0q_u32(msg2, msg3, msg0);

	/* The state at step 60, for recompression to start from. */
	vst1q_u32(at_60, abcd);
	at_60[4] = e1;

	/* Steps 60-63 */
	QUAD(vsha1pq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg1, k3);
	msg2 = vsha1su1q_u32(msg2, msg1);
	vst1q_u32(w + 72, msg2);
	msg3 = vsha1su0q_u32(msg3, msg0, msg1);

	/* And at step 64. */
	vst1q_u32(at_64, abcd);
	at_64[4] = e0;

	/* Steps 64-67 */
	QUAD(vsha1pq_u32, e0, e1, tmp0);
	tmp0 = vaddq_u32(msg2, k3);
	msg3 = vsha1su1q_u32(msg3, msg2);
	vst1q_u32(w + 76, msg3);

	/* Steps 68-71 */
	QUAD(vsha1pq_u32, e1, e0, tmp1);
	tmp1 = vaddq_u32(msg3, k3);

	/* Steps 72-75 */
	QUAD(vsha1pq_u32, e0, e1, tmp0);

	/* Steps 76-79 */
	QUAD(vsha1pq_u32, e1, e0, tmp1);

	vst1q_u32(ihv, vaddq_u32(abcd_in, abcd));
	ihv[4] = e0 + e_in;
}

/* Four steps of the partner block, on schedule group `g`. */
#define GROUP(f, k, g) \
	do { \
		uint32x4_t wk_ = vaddq_u32(veorq_u32(vld1q_u32(m1 + 4 * (g)), \
						     vld1q_u32(dm + 4 * (g))), \
					   vdupq_n_u32(k)); \
		uint32_t next_ = vsha1h_u32(vgetq_lane_u32(abcd, 0)); \
		abcd = f(abcd, e, wk_); \
		e = next_; \
	} while (0)

/*
 * Whether the partner block, whose state at `from` is `state`, ends on
 * `ihv_out`: out from its state at step 60 or 64 to step 80, then in from
 * the chaining value that implies, which must arrive back at that state.
 */
SHA1DC_TARGET_ARMV8
int sha1dc_recompress_armv8(enum sha1dc_from from, const uint32_t m1[80],
			    const uint32_t dm[80], const uint32_t state[5],
			    const uint32_t ihv_out[5])
{
	uint32_t at[5], reached[5], e;
	uint32x4_t abcd;

	sha1dc_partner_boundary(from, m1, dm, state, at);
	abcd = vld1q_u32(at);
	e = at[4];

	/* Out to step 80, from 60 or from 64. */
	if (from == SHA1DC_FROM_58)
		GROUP(vsha1pq_u32, K3, 15);
	GROUP(vsha1pq_u32, K3, 16);
	GROUP(vsha1pq_u32, K3, 17);
	GROUP(vsha1pq_u32, K3, 18);
	GROUP(vsha1pq_u32, K3, 19);

	/* The only input that gives this output is the output less the state. */
	abcd = vsubq_u32(vld1q_u32(ihv_out), abcd);
	e = ihv_out[4] - e;

	/* In from there, as far as the state the way out started from. */
	GROUP(vsha1cq_u32, K0, 0);
	GROUP(vsha1cq_u32, K0, 1);
	GROUP(vsha1cq_u32, K0, 2);
	GROUP(vsha1cq_u32, K0, 3);
	GROUP(vsha1cq_u32, K0, 4);
	GROUP(vsha1pq_u32, K1, 5);
	GROUP(vsha1pq_u32, K1, 6);
	GROUP(vsha1pq_u32, K1, 7);
	GROUP(vsha1pq_u32, K1, 8);
	GROUP(vsha1pq_u32, K1, 9);
	GROUP(vsha1mq_u32, K2, 10);
	GROUP(vsha1mq_u32, K2, 11);
	GROUP(vsha1mq_u32, K2, 12);
	GROUP(vsha1mq_u32, K2, 13);
	GROUP(vsha1mq_u32, K2, 14);
	if (from == SHA1DC_FROM_65)
		GROUP(vsha1pq_u32, K3, 15);

	vst1q_u32(reached, abcd);
	reached[4] = e;
	return !memcmp(reached, at, sizeof(at));
}

#endif /* SHA1DC_HAVE_ARMV8 */
