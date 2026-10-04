/*
 * The x86-64 parts of sha1.c: detecting what the CPU has, and the SHA-1
 * compression and recompression on SHA-NI.
 */

#include "../git-compat-util.h"
#include "internal.h"

#ifdef SHA1DC_HAVE_SSE2

#include <cpuid.h>

/* Leaf 7 of CPUID, subleaf 0, if the CPU has it. */
static int cpuid_7(unsigned int *ebx)
{
	unsigned int eax, ecx, edx;

	if (__get_cpuid_max(0, NULL) < 7)
		return 0;
	__cpuid_count(7, 0, eax, *ebx, ecx, edx);
	return 1;
}

int sha1dc_avx2_available(void)
{
	unsigned int eax, ebx, ecx, edx, xcr0_lo, xcr0_hi;

	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return 0;
	/* OSXSAVE and AVX, then whether the OS saves the YMM registers. */
	if ((ecx & (1u << 27 | 1u << 28)) != (1u << 27 | 1u << 28))
		return 0;
	__asm__("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
	if ((xcr0_lo & 6) != 6)
		return 0;
	return cpuid_7(&ebx) && (ebx & (1u << 5));
}

#ifdef SHA1DC_HAVE_SHANI

int sha1dc_shani_available(void)
{
	unsigned int eax, ebx, ecx, edx;

	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return 0;
	/* SSSE3 and SSE4.1 */
	if ((ecx & (1u << 9 | 1u << 19)) != (1u << 9 | 1u << 19))
		return 0;
	return cpuid_7(&ebx) && (ebx & (1u << 29));
}

/*
 * `sha1rnds4` does four steps at a time, on `abcd` held with A in the top
 * lane; `sha1nexte` derives the fifth working word for the next group from
 * the previous `abcd`, which is why two registers alternate as `live` and
 * `held`. The group of schedule words for steps t..t+3 is held reversed too.
 * Words 16 to 31 come from `sha1msg1`/`sha1msg2`, the rest from plain SSE
 * by the recurrence W[t] = (W[t-6] ^ W[t-16] ^ W[t-28] ^ W[t-32]) <<< 2, in
 * which no word of a group depends on another.
 */

/* Turns a group round, between step order and the order SHA-NI holds. */
#define REVERSE 0x1B

#define LOADU(p) _mm_loadu_si128((const __m128i *)(const void *)(p))
#define STOREU(p, v) _mm_storeu_si128((__m128i *)(void *)(p), (v))

/* The group is already reversed; store it in the mirrored schedule. */
#define SPILL(t, v) STOREU(w + 76 - (t), (v))

/* Schedule words 4k..4k+3 for k from 8 on, from groups k-8, k-7, k-4, k-2, k-1. */
SHA1DC_TARGET_SHANI
static inline __m128i expand_rol2(__m128i v8, __m128i v7, __m128i v4,
				  __m128i v2, __m128i v1)
{
	__m128i x = _mm_xor_si128(_mm_xor_si128(v8, v7), v4);
	/* Words t-6 to t-3: the last two of group k-2, the first two of k-1. */
	x = _mm_xor_si128(x, _mm_alignr_epi8(v2, v1, 8));
	return _mm_or_si128(_mm_slli_epi32(x, 2), _mm_srli_epi32(x, 30));
}

/* Schedule words 4k..4k+3 for k from 4 to 7, with the SHA-NI instructions. */
#define EXPAND_NI(a, b, c, d) \
	_mm_sha1msg2_epu32(_mm_xor_si128(_mm_sha1msg1_epu32((a), (b)), (c)), (d))

/* One group of four steps on schedule group `t / 4`, spilling it first. */
#define ROUNDS(t, k, live, held) \
	do { \
		SPILL(t, v[(t) / 4]); \
		live = _mm_sha1nexte_epu32(live, v[(t) / 4]); \
		held = abcd; \
		abcd = _mm_sha1rnds4_epu32(abcd, live, k); \
	} while (0)

/* The same, also storing the state [A, B, C, D, E] before it in `at`. */
#define ROUNDS_AT(t, k, live, held, at) \
	do { \
		SPILL(t, v[(t) / 4]); \
		live = _mm_sha1nexte_epu32(live, v[(t) / 4]); \
		STOREU(at, _mm_shuffle_epi32(abcd, REVERSE)); \
		/* `live` holds E + W[t] in its top lane. */ \
		at[4] = (uint32_t)_mm_extract_epi32(_mm_sub_epi32(live, v[(t) / 4]), 3); \
		held = abcd; \
		abcd = _mm_sha1rnds4_epu32(abcd, live, k); \
	} while (0)

#define ROL2(k) v[k] = expand_rol2(v[(k) - 8], v[(k) - 7], v[(k) - 4], v[(k) - 2], v[(k) - 1])

SHA1DC_TARGET_SHANI
void sha1dc_compress_shani(uint32_t ihv[5], const unsigned char *block,
			   uint32_t w[80], uint32_t at_60[5], uint32_t at_64[5])
{
	/* Big-endian words, and the four of a group reversed. */
	const __m128i swap = _mm_set_epi64x(0x0001020304050607LL,
					    0x08090A0B0C0D0E0FLL);
	__m128i abcd = _mm_shuffle_epi32(LOADU(ihv), REVERSE);
	const __m128i abcd_in = abcd;
	const __m128i e_in = _mm_set_epi32((int)ihv[4], 0, 0, 0);
	__m128i e0, e1, v[20];

	v[0] = _mm_shuffle_epi8(LOADU(block), swap);
	v[1] = _mm_shuffle_epi8(LOADU(block + 16), swap);
	v[2] = _mm_shuffle_epi8(LOADU(block + 32), swap);
	v[3] = _mm_shuffle_epi8(LOADU(block + 48), swap);

	SPILL(0, v[0]);
	e0 = _mm_add_epi32(e_in, v[0]);
	e1 = abcd;
	abcd = _mm_sha1rnds4_epu32(abcd, e0, 0);
	v[4] = EXPAND_NI(v[0], v[1], v[2], v[3]);

	ROUNDS(4, 0, e1, e0);
	v[5] = EXPAND_NI(v[1], v[2], v[3], v[4]);
	ROUNDS(8, 0, e0, e1);
	v[6] = EXPAND_NI(v[2], v[3], v[4], v[5]);
	ROUNDS(12, 0, e1, e0);
	v[7] = EXPAND_NI(v[3], v[4], v[5], v[6]);
	ROUNDS(16, 0, e0, e1);
	ROL2(8);
	ROUNDS(20, 1, e1, e0);
	ROL2(9);
	ROUNDS(24, 1, e0, e1);
	ROL2(10);
	ROUNDS(28, 1, e1, e0);
	ROL2(11);
	ROUNDS(32, 1, e0, e1);
	ROL2(12);
	ROUNDS(36, 1, e1, e0);
	ROL2(13);
	ROUNDS(40, 2, e0, e1);
	ROL2(14);
	ROUNDS(44, 2, e1, e0);
	ROL2(15);
	ROUNDS(48, 2, e0, e1);
	ROL2(16);
	ROUNDS(52, 2, e1, e0);
	ROL2(17);
	ROUNDS(56, 2, e0, e1);
	ROL2(18);
	ROUNDS_AT(60, 3, e1, e0, at_60);
	ROL2(19);
	ROUNDS_AT(64, 3, e0, e1, at_64);
	ROUNDS(68, 3, e1, e0);
	ROUNDS(72, 3, e0, e1);
	ROUNDS(76, 3, e1, e0);

	/* Feed-forward. */
	e0 = _mm_sha1nexte_epu32(e0, e_in);
	abcd = _mm_add_epi32(abcd, abcd_in);
	STOREU(ihv, _mm_shuffle_epi32(abcd, REVERSE));
	ihv[4] = (uint32_t)_mm_extract_epi32(e0, 3);
}

/*
 * The partner block's schedule words for group `g`, in the order the
 * rounds take them.
 */
#define WORDS(g) \
	_mm_shuffle_epi32(_mm_xor_si128(LOADU(m1 + 4 * (g)), LOADU(dm + 4 * (g))), REVERSE)

/* Four steps. The first of a run adds the fifth word itself. */
#define GROUP_FIRST(e, g, k) \
	do { \
		__m128i live_ = _mm_add_epi32((e), WORDS(g)); \
		held = abcd; \
		abcd = _mm_sha1rnds4_epu32(abcd, live_, k); \
	} while (0)
#define GROUP(g, k) \
	do { \
		__m128i live_ = _mm_sha1nexte_epu32(held, WORDS(g)); \
		held = abcd; \
		abcd = _mm_sha1rnds4_epu32(abcd, live_, k); \
	} while (0)

/*
 * Whether the partner block, whose state at `from` is `state`, ends on
 * `ihv_out`. From its state at step 60 or 64, it runs out to step 80,
 * which gives the chaining value an attack would have had to start from,
 * and then in from that value, which must arrive back at the same state.
 */
SHA1DC_TARGET_SHANI
int sha1dc_recompress_shani(enum sha1dc_from from, const uint32_t m1[80],
			    const uint32_t dm[80], const uint32_t state[5],
			    const uint32_t ihv_out[5])
{
	uint32_t at[5], reached[5];
	__m128i abcd, held, e_at, e_80, e_in;

	sha1dc_partner_boundary(from, m1, dm, state, at);
	abcd = _mm_shuffle_epi32(LOADU(at), REVERSE);

	/* Out to step 80, from 60 or from 64. */
	e_at = _mm_set_epi32((int)at[4], 0, 0, 0);
	if (from == SHA1DC_FROM_58) {
		GROUP_FIRST(e_at, 15, 3);
		GROUP(16, 3);
	} else {
		GROUP_FIRST(e_at, 16, 3);
	}
	GROUP(17, 3);
	GROUP(18, 3);
	GROUP(19, 3);

	/*
	 * The feed-forward adds the input to the state at 80, so the only
	 * input that gives this block's output is the output less that state.
	 */
	e_80 = _mm_sha1nexte_epu32(held, _mm_setzero_si128());
	abcd = _mm_sub_epi32(_mm_shuffle_epi32(LOADU(ihv_out), REVERSE), abcd);
	e_in = _mm_sub_epi32(_mm_set_epi32((int)ihv_out[4], 0, 0, 0), e_80);

	/* In from there, as far as the state the way out started from. */
	GROUP_FIRST(e_in, 0, 0);
	GROUP(1, 0);
	GROUP(2, 0);
	GROUP(3, 0);
	GROUP(4, 0);
	GROUP(5, 1);
	GROUP(6, 1);
	GROUP(7, 1);
	GROUP(8, 1);
	GROUP(9, 1);
	GROUP(10, 2);
	GROUP(11, 2);
	GROUP(12, 2);
	GROUP(13, 2);
	GROUP(14, 2);
	if (from == SHA1DC_FROM_65)
		GROUP(15, 3);

	STOREU(reached, _mm_shuffle_epi32(abcd, REVERSE));
	reached[4] = (uint32_t)_mm_extract_epi32(
		_mm_sha1nexte_epu32(held, _mm_setzero_si128()), 3);
	return !memcmp(reached, at, sizeof(at));
}

#endif /* SHA1DC_HAVE_SHANI */

#endif /* SHA1DC_HAVE_SSE2 */
