/*
 * The x86-64 parts of sha1.c: detecting what the CPU has.
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

#endif /* SHA1DC_HAVE_SSE2 */
