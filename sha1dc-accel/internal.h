#ifndef SHA1DC_ACCEL_INTERNAL_H
#define SHA1DC_ACCEL_INTERNAL_H

/*
 * Shared between the files of sha1dc-accel/. See sha1.c for an overview.
 *
 * The schedule `w` is always the 80 expanded message words in step order,
 * w[t] at index t, which is also what sha1dc/ keeps in SHA1_CTX.m1.
 *
 * A "state" is the five working words [a, b, c, d, e] before a step.
 */

/*
 * Which forms this build can have. The vector and hardware forms need
 * GCC-compatible target attributes and intrinsics; anything else gets the
 * portable form, which every build has.
 */
#if defined(__GNUC__) && !defined(SHA1DC_ACCEL_PORTABLE_ONLY)
# if defined(__x86_64__) && (defined(__clang__) || __GNUC__ >= 5)
#  define SHA1DC_HAVE_SSE2 1
#  define SHA1DC_HAVE_AVX2 1
#  define SHA1DC_HAVE_SHANI 1
#  include <immintrin.h>
#  define SHA1DC_TARGET_SSE2
#  define SHA1DC_TARGET_AVX2 __attribute__((target("avx2")))
#  define SHA1DC_TARGET_SHANI __attribute__((target("sha,sse4.1,ssse3")))
# elif defined(__aarch64__) && defined(__ARM_NEON)
#  define SHA1DC_HAVE_NEON 1
#  include <arm_neon.h>
# endif
#endif

#if defined(__GNUC__)
# define SHA1DC_NOINLINE __attribute__((noinline))
# define sha1dc_ctz(x) ((unsigned)__builtin_ctz(x))
#else
# define SHA1DC_NOINLINE
static inline unsigned sha1dc_ctz(uint32_t x)
{
	unsigned n = 0;
	while (!(x & 1)) {
		x >>= 1;
		n++;
	}
	return n;
}
#endif

/* The step a DV's recompression starts from. */
enum sha1dc_from {
	SHA1DC_FROM_58 = 58,
	SHA1DC_FROM_65 = 65
};

/*
 * The UBC check, one form per instruction set. Each returns the same mask
 * as ubc_check() in sha1dc/: a set bit names a DV that is still possible.
 * See ubc_check.c.
 */
uint32_t sha1dc_ubc_check_scalar(const uint32_t w[80]);
#ifdef SHA1DC_HAVE_SSE2
uint32_t sha1dc_ubc_check_sse2(const uint32_t w[80]);
#endif
#ifdef SHA1DC_HAVE_AVX2
uint32_t sha1dc_ubc_check_avx2(const uint32_t w[80]);
#endif
#ifdef SHA1DC_HAVE_NEON
uint32_t sha1dc_ubc_check_neon(const uint32_t w[80]);
#endif

/* Whether the CPU (and OS) can run the AVX2 form. In x86.c. */
#ifdef SHA1DC_HAVE_AVX2
int sha1dc_avx2_available(void);
#endif

/*
 * The state the partner block of a DV reaches at the group boundary next to
 * where its recompression starts: step 60 from step 58, step 64 from step
 * 65. `state` is this block's state at `from`. In sha1.c.
 */
void sha1dc_partner_boundary(enum sha1dc_from from, const uint32_t m1[80],
			     const uint32_t dm[80], const uint32_t state[5],
			     uint32_t out[5]);

/*
 * The hardware compression. It compresses one 64-byte block into `ihv`,
 * writes the expanded schedule to `w`, and this block's own states at
 * steps 60 and 64 to `at_60` and `at_64`.
 *
 * Its recompression answers whether a DV candidate is really an attack,
 * running the partner block forwards from `state` (this block's state at
 * `from`) with the SHA-1 instructions.
 */
#ifdef SHA1DC_HAVE_SHANI
int sha1dc_shani_available(void);
void sha1dc_compress_shani(uint32_t ihv[5], const unsigned char *block,
			   uint32_t w[80], uint32_t at_60[5], uint32_t at_64[5]);
int sha1dc_recompress_shani(enum sha1dc_from from, const uint32_t m1[80],
			    const uint32_t dm[80], const uint32_t state[5],
			    const uint32_t ihv_out[5]);
#endif
#endif /* SHA1DC_ACCEL_INTERNAL_H */
