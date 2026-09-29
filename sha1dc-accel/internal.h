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

#if defined(__GNUC__)
# define SHA1DC_NOINLINE __attribute__((noinline))
#else
# define SHA1DC_NOINLINE
#endif

/* The step a DV's recompression starts from. */
enum sha1dc_from {
	SHA1DC_FROM_58 = 58,
	SHA1DC_FROM_65 = 65
};

#endif /* SHA1DC_ACCEL_INTERNAL_H */
