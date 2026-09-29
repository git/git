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
#  include <immintrin.h>
#  define SHA1DC_TARGET_SSE2
#  define SHA1DC_TARGET_AVX2 __attribute__((target("avx2")))
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

#endif /* SHA1DC_ACCEL_INTERNAL_H */
