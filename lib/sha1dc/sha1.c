/***
* Copyright 2017 Marc Stevens <marc@marc-stevens.nl>, Dan Shumow (danshu@microsoft.com)
* Distributed under the MIT Software License.
* See accompanying file LICENSE.txt or copy at
* https://opensource.org/licenses/MIT
***/

#ifndef SHA1DC_NO_STANDARD_INCLUDES
#include <string.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __unix__
#include <sys/types.h> /* make sure macros like _BIG_ENDIAN visible */
#endif
#endif

#ifdef SHA1DC_CUSTOM_INCLUDE_SHA1_C
#include SHA1DC_CUSTOM_INCLUDE_SHA1_C
#endif

#ifndef SHA1DC_INIT_SAFE_HASH_DEFAULT
#define SHA1DC_INIT_SAFE_HASH_DEFAULT 1
#endif

#include "sha1.h"
#include "ubc_check.h"

#if (defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || \
     defined(i386) || defined(__i386) || defined(__i386__) || defined(__i486__)  || \
     defined(__i586__) || defined(__i686__) || defined(_M_IX86) || defined(__X86__) || \
     defined(_X86_) || defined(__THW_INTEL__) || defined(__I86__) || defined(__INTEL__) || \
     defined(__386) || defined(_M_X64) || defined(_M_AMD64))
#define SHA1DC_ON_INTEL_LIKE_PROCESSOR
#endif

/*
   Because Little-Endian architectures are most common,
   we only set SHA1DC_BIGENDIAN if one of these conditions is met.
   Note that all MSFT platforms are little endian,
   so none of these will be defined under the MSC compiler.
   If you are compiling on a big endian platform and your compiler does not define one of these,
   you will have to add whatever macros your tool chain defines to indicate Big-Endianness.
 */

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
/*
 * Should detect Big Endian under GCC since at least 4.6.0 (gcc svn
 * rev #165881). See
 * https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
 *
 * This also works under clang since 3.2, it copied the GCC-ism. See
 * clang.git's 3b198a97d2 ("Preprocessor: add __BYTE_ORDER__
 * predefined macro", 2012-07-27)
 */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SHA1DC_BIGENDIAN
#endif

/* Not under GCC-alike */
#elif defined(__BYTE_ORDER) && defined(__BIG_ENDIAN)
/*
 * Should detect Big Endian under glibc.git since 14245eb70e ("entered
 * into RCS", 1992-11-25). Defined in <endian.h> which will have been
 * brought in by standard headers. See glibc.git and
 * https://sourceforge.net/p/predef/wiki/Endianness/
 */
#if __BYTE_ORDER == __BIG_ENDIAN
#define SHA1DC_BIGENDIAN
#endif

/* Not under GCC-alike or glibc */
#elif defined(_BYTE_ORDER) && defined(_BIG_ENDIAN) && defined(_LITTLE_ENDIAN)
/*
 * *BSD and newlib (embedded linux, cygwin, etc).
 * the defined(_BIG_ENDIAN) && defined(_LITTLE_ENDIAN) part prevents
 * this condition from matching with Solaris/sparc.
 * (Solaris defines only one endian macro)
 */
#if _BYTE_ORDER == _BIG_ENDIAN
#define SHA1DC_BIGENDIAN
#endif

/* Not under GCC-alike or glibc or *BSD or newlib */
#elif (defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || \
       defined(__MIPSEB__) || defined(__MIPSEB) || defined(_MIPSEB) || \
       defined(__sparc))
/*
 * Should define Big Endian for a whitelist of known processors. See
 * https://sourceforge.net/p/predef/wiki/Endianness/ and
 * https://web.archive.org/web/20140421151132/http://www.perforce.com/perforce/doc.current/manuals/p4sag/chapter.superuser.html
 */
#define SHA1DC_BIGENDIAN

/* Not under GCC-alike or glibc or *BSD or newlib or <processor whitelist> */
#elif (defined(_AIX) || defined(__hpux))

/*
 * Defines Big Endian on a whitelist of OSs that are known to be Big
 * Endian-only. See
 * https://lore.kernel.org/git/93056823-2740-d072-1ebd-46b440b33d7e@felt.demon.nl/
 */
#define SHA1DC_BIGENDIAN

/* Not under GCC-alike or glibc or *BSD or newlib or <processor whitelist> or <os whitelist> */
#elif defined(SHA1DC_ON_INTEL_LIKE_PROCESSOR)
/*
 * As a last resort before we do anything else we're not 100% sure
 * about below, we blacklist specific processors here. We could add
 * more, see e.g. https://wiki.debian.org/ArchitectureSpecificsMemo
 */
#else /* Not under GCC-alike or glibc or *BSD or newlib or <processor whitelist> or <os whitelist> or <processor blacklist> */

/* We do nothing more here for now */
/*#error "Uncomment this to see if you fall through all the detection"*/

#endif /* Big Endian detection */

#if (defined(SHA1DC_FORCE_LITTLEENDIAN) && defined(SHA1DC_BIGENDIAN))
#undef SHA1DC_BIGENDIAN
#endif
#if (defined(SHA1DC_FORCE_BIGENDIAN) && !defined(SHA1DC_BIGENDIAN))
#define SHA1DC_BIGENDIAN
#endif
/*ENDIANNESS SELECTION*/

#ifndef SHA1DC_FORCE_ALIGNED_ACCESS
#if defined(SHA1DC_FORCE_UNALIGNED_ACCESS) || defined(SHA1DC_ON_INTEL_LIKE_PROCESSOR)
#define SHA1DC_ALLOW_UNALIGNED_ACCESS
#endif /*UNALIGNED ACCESS DETECTION*/
#endif /*FORCE ALIGNED ACCESS*/

#define rotate_right(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define rotate_left(x,n)  (((x)<<(n))|((x)>>(32-(n))))

#define sha1_bswap32(x) \
	{x = ((x << 8) & 0xFF00FF00) | ((x >> 8) & 0xFF00FF); x = (x << 16) | (x >> 16);}

#define sha1_mix(W, t)  (rotate_left(W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16], 1))

#ifdef SHA1DC_BIGENDIAN
	#define sha1_load(m, t, temp)  { temp = m[t]; }
#else
	#define sha1_load(m, t, temp)  { temp = m[t]; sha1_bswap32(temp); }
#endif

#define sha1_store(W, t, x)	*(volatile uint32_t *)&W[t] = x

#define sha1_f1(b,c,d) ((d)^((b)&((c)^(d))))
#define sha1_f2(b,c,d) ((b)^(c)^(d))
#define sha1_f3(b,c,d) (((b)&(c))+((d)&((b)^(c))))
#define sha1_f4(b,c,d) ((b)^(c)^(d))

#define HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, m, t) \
	{ e += rotate_left(a, 5) + sha1_f1(b,c,d) + 0x5A827999 + m[t]; b = rotate_left(b, 30); }
#define HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, m, t) \
	{ e += rotate_left(a, 5) + sha1_f2(b,c,d) + 0x6ED9EBA1 + m[t]; b = rotate_left(b, 30); }
#define HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, m, t) \
	{ e += rotate_left(a, 5) + sha1_f3(b,c,d) + 0x8F1BBCDC + m[t]; b = rotate_left(b, 30); }
#define HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, m, t) \
	{ e += rotate_left(a, 5) + sha1_f4(b,c,d) + 0xCA62C1D6 + m[t]; b = rotate_left(b, 30); }

#define HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(a, b, c, d, e, m, t) \
	{ b = rotate_right(b, 30); e -= rotate_left(a, 5) + sha1_f1(b,c,d) + 0x5A827999 + m[t]; }
#define HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(a, b, c, d, e, m, t) \
	{ b = rotate_right(b, 30); e -= rotate_left(a, 5) + sha1_f2(b,c,d) + 0x6ED9EBA1 + m[t]; }
#define HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(a, b, c, d, e, m, t) \
	{ b = rotate_right(b, 30); e -= rotate_left(a, 5) + sha1_f3(b,c,d) + 0x8F1BBCDC + m[t]; }
#define HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(a, b, c, d, e, m, t) \
	{ b = rotate_right(b, 30); e -= rotate_left(a, 5) + sha1_f4(b,c,d) + 0xCA62C1D6 + m[t]; }

#define SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(a, b, c, d, e, m, W, t, temp) \
	{sha1_load(m, t, temp); sha1_store(W, t, temp); e += temp + rotate_left(a, 5) + sha1_f1(b,c,d) + 0x5A827999; b = rotate_left(b, 30);}

#define SHA1COMPRESS_FULL_ROUND1_STEP_EXPAND(a, b, c, d, e, W, t, temp) \
	{temp = sha1_mix(W, t); sha1_store(W, t, temp); e += temp + rotate_left(a, 5) + sha1_f1(b,c,d) + 0x5A827999; b = rotate_left(b, 30); }

#define SHA1COMPRESS_FULL_ROUND2_STEP(a, b, c, d, e, W, t, temp) \
	{temp = sha1_mix(W, t); sha1_store(W, t, temp); e += temp + rotate_left(a, 5) + sha1_f2(b,c,d) + 0x6ED9EBA1; b = rotate_left(b, 30); }

#define SHA1COMPRESS_FULL_ROUND3_STEP(a, b, c, d, e, W, t, temp) \
	{temp = sha1_mix(W, t); sha1_store(W, t, temp); e += temp + rotate_left(a, 5) + sha1_f3(b,c,d) + 0x8F1BBCDC; b = rotate_left(b, 30); }

#define SHA1COMPRESS_FULL_ROUND4_STEP(a, b, c, d, e, W, t, temp) \
	{temp = sha1_mix(W, t); sha1_store(W, t, temp); e += temp + rotate_left(a, 5) + sha1_f4(b,c,d) + 0xCA62C1D6; b = rotate_left(b, 30); }


#define SHA1_STORE_STATE(i) states[i][0] = a; states[i][1] = b; states[i][2] = c; states[i][3] = d; states[i][4] = e;

#ifdef BUILDNOCOLLDETECTSHA1COMPRESSION
void sha1_compression(uint32_t ihv[5], const uint32_t m[16])
{
	uint32_t W[80];
	uint32_t a,b,c,d,e;
	unsigned i;

	memcpy(W, m, 16 * 4);
	for (i = 16; i < 80; ++i)
		W[i] = sha1_mix(W, i);

	a = ihv[0]; b = ihv[1]; c = ihv[2]; d = ihv[3]; e = ihv[4];

	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, W, 0);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, W, 1);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, W, 2);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, W, 3);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, W, 4);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, W, 5);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, W, 6);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, W, 7);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, W, 8);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, W, 9);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, W, 10);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, W, 11);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, W, 12);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, W, 13);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, W, 14);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, W, 15);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, W, 16);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, W, 17);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, W, 18);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, W, 19);

	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, W, 20);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, W, 21);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, W, 22);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, W, 23);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, W, 24);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, W, 25);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, W, 26);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, W, 27);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, W, 28);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, W, 29);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, W, 30);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, W, 31);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, W, 32);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, W, 33);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, W, 34);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, W, 35);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, W, 36);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, W, 37);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, W, 38);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, W, 39);

	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, W, 40);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, W, 41);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, W, 42);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, W, 43);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, W, 44);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, W, 45);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, W, 46);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, W, 47);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, W, 48);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, W, 49);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, W, 50);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, W, 51);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, W, 52);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, W, 53);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, W, 54);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, W, 55);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, W, 56);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, W, 57);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, W, 58);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, W, 59);

	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, W, 60);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, W, 61);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, W, 62);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, W, 63);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, W, 64);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, W, 65);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, W, 66);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, W, 67);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, W, 68);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, W, 69);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, W, 70);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, W, 71);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, W, 72);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, W, 73);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, W, 74);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, W, 75);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, W, 76);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, W, 77);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, W, 78);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, W, 79);

	ihv[0] += a; ihv[1] += b; ihv[2] += c; ihv[3] += d; ihv[4] += e;
}
#endif /*BUILDNOCOLLDETECTSHA1COMPRESSION*/


static void sha1_compression_W(uint32_t ihv[5], const uint32_t W[80])
{
	uint32_t a = ihv[0], b = ihv[1], c = ihv[2], d = ihv[3], e = ihv[4];

	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, W, 0);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, W, 1);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, W, 2);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, W, 3);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, W, 4);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, W, 5);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, W, 6);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, W, 7);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, W, 8);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, W, 9);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, W, 10);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, W, 11);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, W, 12);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, W, 13);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, W, 14);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, W, 15);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, W, 16);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, W, 17);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, W, 18);
	HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, W, 19);

	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, W, 20);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, W, 21);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, W, 22);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, W, 23);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, W, 24);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, W, 25);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, W, 26);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, W, 27);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, W, 28);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, W, 29);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, W, 30);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, W, 31);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, W, 32);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, W, 33);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, W, 34);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, W, 35);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, W, 36);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, W, 37);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, W, 38);
	HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, W, 39);

	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, W, 40);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, W, 41);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, W, 42);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, W, 43);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, W, 44);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, W, 45);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, W, 46);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, W, 47);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, W, 48);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, W, 49);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, W, 50);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, W, 51);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, W, 52);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, W, 53);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, W, 54);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, W, 55);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, W, 56);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, W, 57);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, W, 58);
	HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, W, 59);

	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, W, 60);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, W, 61);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, W, 62);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, W, 63);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, W, 64);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, W, 65);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, W, 66);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, W, 67);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, W, 68);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, W, 69);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, W, 70);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, W, 71);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, W, 72);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, W, 73);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, W, 74);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, W, 75);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, W, 76);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, W, 77);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, W, 78);
	HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, W, 79);

	ihv[0] += a; ihv[1] += b; ihv[2] += c; ihv[3] += d; ihv[4] += e;
}



void sha1_compression_states(uint32_t ihv[5], const uint32_t m[16], uint32_t W[80], uint32_t states[80][5])
{
	uint32_t a = ihv[0], b = ihv[1], c = ihv[2], d = ihv[3], e = ihv[4];
	uint32_t temp;

#ifdef DOSTORESTATE00
	SHA1_STORE_STATE(0)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(a, b, c, d, e, m, W, 0, temp);

#ifdef DOSTORESTATE01
	SHA1_STORE_STATE(1)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(e, a, b, c, d, m, W, 1, temp);

#ifdef DOSTORESTATE02
	SHA1_STORE_STATE(2)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(d, e, a, b, c, m, W, 2, temp);

#ifdef DOSTORESTATE03
	SHA1_STORE_STATE(3)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(c, d, e, a, b, m, W, 3, temp);

#ifdef DOSTORESTATE04
	SHA1_STORE_STATE(4)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(b, c, d, e, a, m, W, 4, temp);

#ifdef DOSTORESTATE05
	SHA1_STORE_STATE(5)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(a, b, c, d, e, m, W, 5, temp);

#ifdef DOSTORESTATE06
	SHA1_STORE_STATE(6)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(e, a, b, c, d, m, W, 6, temp);

#ifdef DOSTORESTATE07
	SHA1_STORE_STATE(7)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(d, e, a, b, c, m, W, 7, temp);

#ifdef DOSTORESTATE08
	SHA1_STORE_STATE(8)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(c, d, e, a, b, m, W, 8, temp);

#ifdef DOSTORESTATE09
	SHA1_STORE_STATE(9)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(b, c, d, e, a, m, W, 9, temp);

#ifdef DOSTORESTATE10
	SHA1_STORE_STATE(10)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(a, b, c, d, e, m, W, 10, temp);

#ifdef DOSTORESTATE11
	SHA1_STORE_STATE(11)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(e, a, b, c, d, m, W, 11, temp);

#ifdef DOSTORESTATE12
	SHA1_STORE_STATE(12)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(d, e, a, b, c, m, W, 12, temp);

#ifdef DOSTORESTATE13
	SHA1_STORE_STATE(13)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(c, d, e, a, b, m, W, 13, temp);

#ifdef DOSTORESTATE14
	SHA1_STORE_STATE(14)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(b, c, d, e, a, m, W, 14, temp);

#ifdef DOSTORESTATE15
	SHA1_STORE_STATE(15)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_LOAD(a, b, c, d, e, m, W, 15, temp);

#ifdef DOSTORESTATE16
	SHA1_STORE_STATE(16)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_EXPAND(e, a, b, c, d, W, 16, temp);

#ifdef DOSTORESTATE17
	SHA1_STORE_STATE(17)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_EXPAND(d, e, a, b, c, W, 17, temp);

#ifdef DOSTORESTATE18
	SHA1_STORE_STATE(18)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_EXPAND(c, d, e, a, b, W, 18, temp);

#ifdef DOSTORESTATE19
	SHA1_STORE_STATE(19)
#endif
	SHA1COMPRESS_FULL_ROUND1_STEP_EXPAND(b, c, d, e, a, W, 19, temp);



#ifdef DOSTORESTATE20
	SHA1_STORE_STATE(20)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(a, b, c, d, e, W, 20, temp);

#ifdef DOSTORESTATE21
	SHA1_STORE_STATE(21)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(e, a, b, c, d, W, 21, temp);

#ifdef DOSTORESTATE22
	SHA1_STORE_STATE(22)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(d, e, a, b, c, W, 22, temp);

#ifdef DOSTORESTATE23
	SHA1_STORE_STATE(23)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(c, d, e, a, b, W, 23, temp);

#ifdef DOSTORESTATE24
	SHA1_STORE_STATE(24)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(b, c, d, e, a, W, 24, temp);

#ifdef DOSTORESTATE25
	SHA1_STORE_STATE(25)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(a, b, c, d, e, W, 25, temp);

#ifdef DOSTORESTATE26
	SHA1_STORE_STATE(26)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(e, a, b, c, d, W, 26, temp);

#ifdef DOSTORESTATE27
	SHA1_STORE_STATE(27)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(d, e, a, b, c, W, 27, temp);

#ifdef DOSTORESTATE28
	SHA1_STORE_STATE(28)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(c, d, e, a, b, W, 28, temp);

#ifdef DOSTORESTATE29
	SHA1_STORE_STATE(29)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(b, c, d, e, a, W, 29, temp);

#ifdef DOSTORESTATE30
	SHA1_STORE_STATE(30)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(a, b, c, d, e, W, 30, temp);

#ifdef DOSTORESTATE31
	SHA1_STORE_STATE(31)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(e, a, b, c, d, W, 31, temp);

#ifdef DOSTORESTATE32
	SHA1_STORE_STATE(32)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(d, e, a, b, c, W, 32, temp);

#ifdef DOSTORESTATE33
	SHA1_STORE_STATE(33)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(c, d, e, a, b, W, 33, temp);

#ifdef DOSTORESTATE34
	SHA1_STORE_STATE(34)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(b, c, d, e, a, W, 34, temp);

#ifdef DOSTORESTATE35
	SHA1_STORE_STATE(35)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(a, b, c, d, e, W, 35, temp);

#ifdef DOSTORESTATE36
	SHA1_STORE_STATE(36)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(e, a, b, c, d, W, 36, temp);

#ifdef DOSTORESTATE37
	SHA1_STORE_STATE(37)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(d, e, a, b, c, W, 37, temp);

#ifdef DOSTORESTATE38
	SHA1_STORE_STATE(38)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(c, d, e, a, b, W, 38, temp);

#ifdef DOSTORESTATE39
	SHA1_STORE_STATE(39)
#endif
	SHA1COMPRESS_FULL_ROUND2_STEP(b, c, d, e, a, W, 39, temp);



#ifdef DOSTORESTATE40
	SHA1_STORE_STATE(40)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(a, b, c, d, e, W, 40, temp);

#ifdef DOSTORESTATE41
	SHA1_STORE_STATE(41)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(e, a, b, c, d, W, 41, temp);

#ifdef DOSTORESTATE42
	SHA1_STORE_STATE(42)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(d, e, a, b, c, W, 42, temp);

#ifdef DOSTORESTATE43
	SHA1_STORE_STATE(43)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(c, d, e, a, b, W, 43, temp);

#ifdef DOSTORESTATE44
	SHA1_STORE_STATE(44)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(b, c, d, e, a, W, 44, temp);

#ifdef DOSTORESTATE45
	SHA1_STORE_STATE(45)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(a, b, c, d, e, W, 45, temp);

#ifdef DOSTORESTATE46
	SHA1_STORE_STATE(46)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(e, a, b, c, d, W, 46, temp);

#ifdef DOSTORESTATE47
	SHA1_STORE_STATE(47)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(d, e, a, b, c, W, 47, temp);

#ifdef DOSTORESTATE48
	SHA1_STORE_STATE(48)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(c, d, e, a, b, W, 48, temp);

#ifdef DOSTORESTATE49
	SHA1_STORE_STATE(49)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(b, c, d, e, a, W, 49, temp);

#ifdef DOSTORESTATE50
	SHA1_STORE_STATE(50)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(a, b, c, d, e, W, 50, temp);

#ifdef DOSTORESTATE51
	SHA1_STORE_STATE(51)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(e, a, b, c, d, W, 51, temp);

#ifdef DOSTORESTATE52
	SHA1_STORE_STATE(52)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(d, e, a, b, c, W, 52, temp);

#ifdef DOSTORESTATE53
	SHA1_STORE_STATE(53)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(c, d, e, a, b, W, 53, temp);

#ifdef DOSTORESTATE54
	SHA1_STORE_STATE(54)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(b, c, d, e, a, W, 54, temp);

#ifdef DOSTORESTATE55
	SHA1_STORE_STATE(55)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(a, b, c, d, e, W, 55, temp);

#ifdef DOSTORESTATE56
	SHA1_STORE_STATE(56)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(e, a, b, c, d, W, 56, temp);

#ifdef DOSTORESTATE57
	SHA1_STORE_STATE(57)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(d, e, a, b, c, W, 57, temp);

#ifdef DOSTORESTATE58
	SHA1_STORE_STATE(58)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(c, d, e, a, b, W, 58, temp);

#ifdef DOSTORESTATE59
	SHA1_STORE_STATE(59)
#endif
	SHA1COMPRESS_FULL_ROUND3_STEP(b, c, d, e, a, W, 59, temp);




#ifdef DOSTORESTATE60
	SHA1_STORE_STATE(60)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(a, b, c, d, e, W, 60, temp);

#ifdef DOSTORESTATE61
	SHA1_STORE_STATE(61)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(e, a, b, c, d, W, 61, temp);

#ifdef DOSTORESTATE62
	SHA1_STORE_STATE(62)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(d, e, a, b, c, W, 62, temp);

#ifdef DOSTORESTATE63
	SHA1_STORE_STATE(63)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(c, d, e, a, b, W, 63, temp);

#ifdef DOSTORESTATE64
	SHA1_STORE_STATE(64)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(b, c, d, e, a, W, 64, temp);

#ifdef DOSTORESTATE65
	SHA1_STORE_STATE(65)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(a, b, c, d, e, W, 65, temp);

#ifdef DOSTORESTATE66
	SHA1_STORE_STATE(66)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(e, a, b, c, d, W, 66, temp);

#ifdef DOSTORESTATE67
	SHA1_STORE_STATE(67)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(d, e, a, b, c, W, 67, temp);

#ifdef DOSTORESTATE68
	SHA1_STORE_STATE(68)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(c, d, e, a, b, W, 68, temp);

#ifdef DOSTORESTATE69
	SHA1_STORE_STATE(69)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(b, c, d, e, a, W, 69, temp);

#ifdef DOSTORESTATE70
	SHA1_STORE_STATE(70)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(a, b, c, d, e, W, 70, temp);

#ifdef DOSTORESTATE71
	SHA1_STORE_STATE(71)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(e, a, b, c, d, W, 71, temp);

#ifdef DOSTORESTATE72
	SHA1_STORE_STATE(72)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(d, e, a, b, c, W, 72, temp);

#ifdef DOSTORESTATE73
	SHA1_STORE_STATE(73)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(c, d, e, a, b, W, 73, temp);

#ifdef DOSTORESTATE74
	SHA1_STORE_STATE(74)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(b, c, d, e, a, W, 74, temp);

#ifdef DOSTORESTATE75
	SHA1_STORE_STATE(75)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(a, b, c, d, e, W, 75, temp);

#ifdef DOSTORESTATE76
	SHA1_STORE_STATE(76)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(e, a, b, c, d, W, 76, temp);

#ifdef DOSTORESTATE77
	SHA1_STORE_STATE(77)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(d, e, a, b, c, W, 77, temp);

#ifdef DOSTORESTATE78
	SHA1_STORE_STATE(78)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(c, d, e, a, b, W, 78, temp);

#ifdef DOSTORESTATE79
	SHA1_STORE_STATE(79)
#endif
	SHA1COMPRESS_FULL_ROUND4_STEP(b, c, d, e, a, W, 79, temp);



	ihv[0] += a; ihv[1] += b; ihv[2] += c; ihv[3] += d; ihv[4] += e;
}




#define SHA1_RECOMPRESS(t) \
static void sha1recompress_fast_ ## t (uint32_t ihvin[5], uint32_t ihvout[5], const uint32_t me2[80], const uint32_t state[5]) \
{ \
	uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4]; \
	if (t > 79) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(b, c, d, e, a, me2, 79); \
	if (t > 78) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(c, d, e, a, b, me2, 78); \
	if (t > 77) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(d, e, a, b, c, me2, 77); \
	if (t > 76) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(e, a, b, c, d, me2, 76); \
	if (t > 75) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(a, b, c, d, e, me2, 75); \
	if (t > 74) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(b, c, d, e, a, me2, 74); \
	if (t > 73) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(c, d, e, a, b, me2, 73); \
	if (t > 72) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(d, e, a, b, c, me2, 72); \
	if (t > 71) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(e, a, b, c, d, me2, 71); \
	if (t > 70) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(a, b, c, d, e, me2, 70); \
	if (t > 69) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(b, c, d, e, a, me2, 69); \
	if (t > 68) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(c, d, e, a, b, me2, 68); \
	if (t > 67) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(d, e, a, b, c, me2, 67); \
	if (t > 66) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(e, a, b, c, d, me2, 66); \
	if (t > 65) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(a, b, c, d, e, me2, 65); \
	if (t > 64) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(b, c, d, e, a, me2, 64); \
	if (t > 63) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(c, d, e, a, b, me2, 63); \
	if (t > 62) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(d, e, a, b, c, me2, 62); \
	if (t > 61) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(e, a, b, c, d, me2, 61); \
	if (t > 60) HASHCLASH_SHA1COMPRESS_ROUND4_STEP_BW(a, b, c, d, e, me2, 60); \
	if (t > 59) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(b, c, d, e, a, me2, 59); \
	if (t > 58) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(c, d, e, a, b, me2, 58); \
	if (t > 57) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(d, e, a, b, c, me2, 57); \
	if (t > 56) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(e, a, b, c, d, me2, 56); \
	if (t > 55) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(a, b, c, d, e, me2, 55); \
	if (t > 54) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(b, c, d, e, a, me2, 54); \
	if (t > 53) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(c, d, e, a, b, me2, 53); \
	if (t > 52) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(d, e, a, b, c, me2, 52); \
	if (t > 51) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(e, a, b, c, d, me2, 51); \
	if (t > 50) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(a, b, c, d, e, me2, 50); \
	if (t > 49) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(b, c, d, e, a, me2, 49); \
	if (t > 48) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(c, d, e, a, b, me2, 48); \
	if (t > 47) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(d, e, a, b, c, me2, 47); \
	if (t > 46) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(e, a, b, c, d, me2, 46); \
	if (t > 45) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(a, b, c, d, e, me2, 45); \
	if (t > 44) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(b, c, d, e, a, me2, 44); \
	if (t > 43) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(c, d, e, a, b, me2, 43); \
	if (t > 42) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(d, e, a, b, c, me2, 42); \
	if (t > 41) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(e, a, b, c, d, me2, 41); \
	if (t > 40) HASHCLASH_SHA1COMPRESS_ROUND3_STEP_BW(a, b, c, d, e, me2, 40); \
	if (t > 39) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(b, c, d, e, a, me2, 39); \
	if (t > 38) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(c, d, e, a, b, me2, 38); \
	if (t > 37) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(d, e, a, b, c, me2, 37); \
	if (t > 36) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(e, a, b, c, d, me2, 36); \
	if (t > 35) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(a, b, c, d, e, me2, 35); \
	if (t > 34) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(b, c, d, e, a, me2, 34); \
	if (t > 33) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(c, d, e, a, b, me2, 33); \
	if (t > 32) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(d, e, a, b, c, me2, 32); \
	if (t > 31) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(e, a, b, c, d, me2, 31); \
	if (t > 30) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(a, b, c, d, e, me2, 30); \
	if (t > 29) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(b, c, d, e, a, me2, 29); \
	if (t > 28) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(c, d, e, a, b, me2, 28); \
	if (t > 27) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(d, e, a, b, c, me2, 27); \
	if (t > 26) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(e, a, b, c, d, me2, 26); \
	if (t > 25) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(a, b, c, d, e, me2, 25); \
	if (t > 24) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(b, c, d, e, a, me2, 24); \
	if (t > 23) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(c, d, e, a, b, me2, 23); \
	if (t > 22) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(d, e, a, b, c, me2, 22); \
	if (t > 21) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(e, a, b, c, d, me2, 21); \
	if (t > 20) HASHCLASH_SHA1COMPRESS_ROUND2_STEP_BW(a, b, c, d, e, me2, 20); \
	if (t > 19) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(b, c, d, e, a, me2, 19); \
	if (t > 18) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(c, d, e, a, b, me2, 18); \
	if (t > 17) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(d, e, a, b, c, me2, 17); \
	if (t > 16) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(e, a, b, c, d, me2, 16); \
	if (t > 15) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(a, b, c, d, e, me2, 15); \
	if (t > 14) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(b, c, d, e, a, me2, 14); \
	if (t > 13) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(c, d, e, a, b, me2, 13); \
	if (t > 12) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(d, e, a, b, c, me2, 12); \
	if (t > 11) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(e, a, b, c, d, me2, 11); \
	if (t > 10) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(a, b, c, d, e, me2, 10); \
	if (t > 9) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(b, c, d, e, a, me2, 9); \
	if (t > 8) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(c, d, e, a, b, me2, 8); \
	if (t > 7) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(d, e, a, b, c, me2, 7); \
	if (t > 6) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(e, a, b, c, d, me2, 6); \
	if (t > 5) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(a, b, c, d, e, me2, 5); \
	if (t > 4) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(b, c, d, e, a, me2, 4); \
	if (t > 3) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(c, d, e, a, b, me2, 3); \
	if (t > 2) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(d, e, a, b, c, me2, 2); \
	if (t > 1) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(e, a, b, c, d, me2, 1); \
	if (t > 0) HASHCLASH_SHA1COMPRESS_ROUND1_STEP_BW(a, b, c, d, e, me2, 0); \
	ihvin[0] = a; ihvin[1] = b; ihvin[2] = c; ihvin[3] = d; ihvin[4] = e; \
	a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4]; \
	if (t <= 0) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, me2, 0); \
	if (t <= 1) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, me2, 1); \
	if (t <= 2) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, me2, 2); \
	if (t <= 3) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, me2, 3); \
	if (t <= 4) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, me2, 4); \
	if (t <= 5) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, me2, 5); \
	if (t <= 6) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, me2, 6); \
	if (t <= 7) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, me2, 7); \
	if (t <= 8) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, me2, 8); \
	if (t <= 9) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, me2, 9); \
	if (t <= 10) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, me2, 10); \
	if (t <= 11) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, me2, 11); \
	if (t <= 12) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, me2, 12); \
	if (t <= 13) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, me2, 13); \
	if (t <= 14) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, me2, 14); \
	if (t <= 15) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(a, b, c, d, e, me2, 15); \
	if (t <= 16) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(e, a, b, c, d, me2, 16); \
	if (t <= 17) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(d, e, a, b, c, me2, 17); \
	if (t <= 18) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(c, d, e, a, b, me2, 18); \
	if (t <= 19) HASHCLASH_SHA1COMPRESS_ROUND1_STEP(b, c, d, e, a, me2, 19); \
	if (t <= 20) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, me2, 20); \
	if (t <= 21) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, me2, 21); \
	if (t <= 22) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, me2, 22); \
	if (t <= 23) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, me2, 23); \
	if (t <= 24) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, me2, 24); \
	if (t <= 25) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, me2, 25); \
	if (t <= 26) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, me2, 26); \
	if (t <= 27) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, me2, 27); \
	if (t <= 28) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, me2, 28); \
	if (t <= 29) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, me2, 29); \
	if (t <= 30) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, me2, 30); \
	if (t <= 31) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, me2, 31); \
	if (t <= 32) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, me2, 32); \
	if (t <= 33) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, me2, 33); \
	if (t <= 34) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, me2, 34); \
	if (t <= 35) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(a, b, c, d, e, me2, 35); \
	if (t <= 36) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(e, a, b, c, d, me2, 36); \
	if (t <= 37) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(d, e, a, b, c, me2, 37); \
	if (t <= 38) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(c, d, e, a, b, me2, 38); \
	if (t <= 39) HASHCLASH_SHA1COMPRESS_ROUND2_STEP(b, c, d, e, a, me2, 39); \
	if (t <= 40) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, me2, 40); \
	if (t <= 41) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, me2, 41); \
	if (t <= 42) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, me2, 42); \
	if (t <= 43) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, me2, 43); \
	if (t <= 44) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, me2, 44); \
	if (t <= 45) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, me2, 45); \
	if (t <= 46) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, me2, 46); \
	if (t <= 47) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, me2, 47); \
	if (t <= 48) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, me2, 48); \
	if (t <= 49) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, me2, 49); \
	if (t <= 50) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, me2, 50); \
	if (t <= 51) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, me2, 51); \
	if (t <= 52) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, me2, 52); \
	if (t <= 53) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, me2, 53); \
	if (t <= 54) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, me2, 54); \
	if (t <= 55) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(a, b, c, d, e, me2, 55); \
	if (t <= 56) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(e, a, b, c, d, me2, 56); \
	if (t <= 57) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(d, e, a, b, c, me2, 57); \
	if (t <= 58) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(c, d, e, a, b, me2, 58); \
	if (t <= 59) HASHCLASH_SHA1COMPRESS_ROUND3_STEP(b, c, d, e, a, me2, 59); \
	if (t <= 60) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, me2, 60); \
	if (t <= 61) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, me2, 61); \
	if (t <= 62) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, me2, 62); \
	if (t <= 63) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, me2, 63); \
	if (t <= 64) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, me2, 64); \
	if (t <= 65) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, me2, 65); \
	if (t <= 66) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, me2, 66); \
	if (t <= 67) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, me2, 67); \
	if (t <= 68) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, me2, 68); \
	if (t <= 69) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, me2, 69); \
	if (t <= 70) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, me2, 70); \
	if (t <= 71) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, me2, 71); \
	if (t <= 72) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, me2, 72); \
	if (t <= 73) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, me2, 73); \
	if (t <= 74) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, me2, 74); \
	if (t <= 75) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(a, b, c, d, e, me2, 75); \
	if (t <= 76) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(e, a, b, c, d, me2, 76); \
	if (t <= 77) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(d, e, a, b, c, me2, 77); \
	if (t <= 78) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(c, d, e, a, b, me2, 78); \
	if (t <= 79) HASHCLASH_SHA1COMPRESS_ROUND4_STEP(b, c, d, e, a, me2, 79); \
	ihvout[0] = ihvin[0] + a; ihvout[1] = ihvin[1] + b; ihvout[2] = ihvin[2] + c; ihvout[3] = ihvin[3] + d; ihvout[4] = ihvin[4] + e; \
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4127)  /* Compiler complains about the checks in the above macro being constant. */
#endif

#ifdef DOSTORESTATE0
SHA1_RECOMPRESS(0)
#endif

#ifdef DOSTORESTATE1
SHA1_RECOMPRESS(1)
#endif

#ifdef DOSTORESTATE2
SHA1_RECOMPRESS(2)
#endif

#ifdef DOSTORESTATE3
SHA1_RECOMPRESS(3)
#endif

#ifdef DOSTORESTATE4
SHA1_RECOMPRESS(4)
#endif

#ifdef DOSTORESTATE5
SHA1_RECOMPRESS(5)
#endif

#ifdef DOSTORESTATE6
SHA1_RECOMPRESS(6)
#endif

#ifdef DOSTORESTATE7
SHA1_RECOMPRESS(7)
#endif

#ifdef DOSTORESTATE8
SHA1_RECOMPRESS(8)
#endif

#ifdef DOSTORESTATE9
SHA1_RECOMPRESS(9)
#endif

#ifdef DOSTORESTATE10
SHA1_RECOMPRESS(10)
#endif

#ifdef DOSTORESTATE11
SHA1_RECOMPRESS(11)
#endif

#ifdef DOSTORESTATE12
SHA1_RECOMPRESS(12)
#endif

#ifdef DOSTORESTATE13
SHA1_RECOMPRESS(13)
#endif

#ifdef DOSTORESTATE14
SHA1_RECOMPRESS(14)
#endif

#ifdef DOSTORESTATE15
SHA1_RECOMPRESS(15)
#endif

#ifdef DOSTORESTATE16
SHA1_RECOMPRESS(16)
#endif

#ifdef DOSTORESTATE17
SHA1_RECOMPRESS(17)
#endif

#ifdef DOSTORESTATE18
SHA1_RECOMPRESS(18)
#endif

#ifdef DOSTORESTATE19
SHA1_RECOMPRESS(19)
#endif

#ifdef DOSTORESTATE20
SHA1_RECOMPRESS(20)
#endif

#ifdef DOSTORESTATE21
SHA1_RECOMPRESS(21)
#endif

#ifdef DOSTORESTATE22
SHA1_RECOMPRESS(22)
#endif

#ifdef DOSTORESTATE23
SHA1_RECOMPRESS(23)
#endif

#ifdef DOSTORESTATE24
SHA1_RECOMPRESS(24)
#endif

#ifdef DOSTORESTATE25
SHA1_RECOMPRESS(25)
#endif

#ifdef DOSTORESTATE26
SHA1_RECOMPRESS(26)
#endif

#ifdef DOSTORESTATE27
SHA1_RECOMPRESS(27)
#endif

#ifdef DOSTORESTATE28
SHA1_RECOMPRESS(28)
#endif

#ifdef DOSTORESTATE29
SHA1_RECOMPRESS(29)
#endif

#ifdef DOSTORESTATE30
SHA1_RECOMPRESS(30)
#endif

#ifdef DOSTORESTATE31
SHA1_RECOMPRESS(31)
#endif

#ifdef DOSTORESTATE32
SHA1_RECOMPRESS(32)
#endif

#ifdef DOSTORESTATE33
SHA1_RECOMPRESS(33)
#endif

#ifdef DOSTORESTATE34
SHA1_RECOMPRESS(34)
#endif

#ifdef DOSTORESTATE35
SHA1_RECOMPRESS(35)
#endif

#ifdef DOSTORESTATE36
SHA1_RECOMPRESS(36)
#endif

#ifdef DOSTORESTATE37
SHA1_RECOMPRESS(37)
#endif

#ifdef DOSTORESTATE38
SHA1_RECOMPRESS(38)
#endif

#ifdef DOSTORESTATE39
SHA1_RECOMPRESS(39)
#endif

#ifdef DOSTORESTATE40
SHA1_RECOMPRESS(40)
#endif

#ifdef DOSTORESTATE41
SHA1_RECOMPRESS(41)
#endif

#ifdef DOSTORESTATE42
SHA1_RECOMPRESS(42)
#endif

#ifdef DOSTORESTATE43
SHA1_RECOMPRESS(43)
#endif

#ifdef DOSTORESTATE44
SHA1_RECOMPRESS(44)
#endif

#ifdef DOSTORESTATE45
SHA1_RECOMPRESS(45)
#endif

#ifdef DOSTORESTATE46
SHA1_RECOMPRESS(46)
#endif

#ifdef DOSTORESTATE47
SHA1_RECOMPRESS(47)
#endif

#ifdef DOSTORESTATE48
SHA1_RECOMPRESS(48)
#endif

#ifdef DOSTORESTATE49
SHA1_RECOMPRESS(49)
#endif

#ifdef DOSTORESTATE50
SHA1_RECOMPRESS(50)
#endif

#ifdef DOSTORESTATE51
SHA1_RECOMPRESS(51)
#endif

#ifdef DOSTORESTATE52
SHA1_RECOMPRESS(52)
#endif

#ifdef DOSTORESTATE53
SHA1_RECOMPRESS(53)
#endif

#ifdef DOSTORESTATE54
SHA1_RECOMPRESS(54)
#endif

#ifdef DOSTORESTATE55
SHA1_RECOMPRESS(55)
#endif

#ifdef DOSTORESTATE56
SHA1_RECOMPRESS(56)
#endif

#ifdef DOSTORESTATE57
SHA1_RECOMPRESS(57)
#endif

#ifdef DOSTORESTATE58
SHA1_RECOMPRESS(58)
#endif

#ifdef DOSTORESTATE59
SHA1_RECOMPRESS(59)
#endif

#ifdef DOSTORESTATE60
SHA1_RECOMPRESS(60)
#endif

#ifdef DOSTORESTATE61
SHA1_RECOMPRESS(61)
#endif

#ifdef DOSTORESTATE62
SHA1_RECOMPRESS(62)
#endif

#ifdef DOSTORESTATE63
SHA1_RECOMPRESS(63)
#endif

#ifdef DOSTORESTATE64
SHA1_RECOMPRESS(64)
#endif

#ifdef DOSTORESTATE65
SHA1_RECOMPRESS(65)
#endif

#ifdef DOSTORESTATE66
SHA1_RECOMPRESS(66)
#endif

#ifdef DOSTORESTATE67
SHA1_RECOMPRESS(67)
#endif

#ifdef DOSTORESTATE68
SHA1_RECOMPRESS(68)
#endif

#ifdef DOSTORESTATE69
SHA1_RECOMPRESS(69)
#endif

#ifdef DOSTORESTATE70
SHA1_RECOMPRESS(70)
#endif

#ifdef DOSTORESTATE71
SHA1_RECOMPRESS(71)
#endif

#ifdef DOSTORESTATE72
SHA1_RECOMPRESS(72)
#endif

#ifdef DOSTORESTATE73
SHA1_RECOMPRESS(73)
#endif

#ifdef DOSTORESTATE74
SHA1_RECOMPRESS(74)
#endif

#ifdef DOSTORESTATE75
SHA1_RECOMPRESS(75)
#endif

#ifdef DOSTORESTATE76
SHA1_RECOMPRESS(76)
#endif

#ifdef DOSTORESTATE77
SHA1_RECOMPRESS(77)
#endif

#ifdef DOSTORESTATE78
SHA1_RECOMPRESS(78)
#endif

#ifdef DOSTORESTATE79
SHA1_RECOMPRESS(79)
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

static void sha1_recompression_step(uint32_t step, uint32_t ihvin[5], uint32_t ihvout[5], const uint32_t me2[80], const uint32_t state[5])
{
	switch (step)
	{
#ifdef DOSTORESTATE0
	case 0:
		sha1recompress_fast_0(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE1
	case 1:
		sha1recompress_fast_1(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE2
	case 2:
		sha1recompress_fast_2(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE3
	case 3:
		sha1recompress_fast_3(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE4
	case 4:
		sha1recompress_fast_4(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE5
	case 5:
		sha1recompress_fast_5(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE6
	case 6:
		sha1recompress_fast_6(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE7
	case 7:
		sha1recompress_fast_7(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE8
	case 8:
		sha1recompress_fast_8(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE9
	case 9:
		sha1recompress_fast_9(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE10
	case 10:
		sha1recompress_fast_10(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE11
	case 11:
		sha1recompress_fast_11(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE12
	case 12:
		sha1recompress_fast_12(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE13
	case 13:
		sha1recompress_fast_13(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE14
	case 14:
		sha1recompress_fast_14(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE15
	case 15:
		sha1recompress_fast_15(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE16
	case 16:
		sha1recompress_fast_16(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE17
	case 17:
		sha1recompress_fast_17(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE18
	case 18:
		sha1recompress_fast_18(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE19
	case 19:
		sha1recompress_fast_19(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE20
	case 20:
		sha1recompress_fast_20(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE21
	case 21:
		sha1recompress_fast_21(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE22
	case 22:
		sha1recompress_fast_22(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE23
	case 23:
		sha1recompress_fast_23(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE24
	case 24:
		sha1recompress_fast_24(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE25
	case 25:
		sha1recompress_fast_25(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE26
	case 26:
		sha1recompress_fast_26(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE27
	case 27:
		sha1recompress_fast_27(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE28
	case 28:
		sha1recompress_fast_28(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE29
	case 29:
		sha1recompress_fast_29(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE30
	case 30:
		sha1recompress_fast_30(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE31
	case 31:
		sha1recompress_fast_31(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE32
	case 32:
		sha1recompress_fast_32(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE33
	case 33:
		sha1recompress_fast_33(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE34
	case 34:
		sha1recompress_fast_34(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE35
	case 35:
		sha1recompress_fast_35(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE36
	case 36:
		sha1recompress_fast_36(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE37
	case 37:
		sha1recompress_fast_37(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE38
	case 38:
		sha1recompress_fast_38(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE39
	case 39:
		sha1recompress_fast_39(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE40
	case 40:
		sha1recompress_fast_40(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE41
	case 41:
		sha1recompress_fast_41(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE42
	case 42:
		sha1recompress_fast_42(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE43
	case 43:
		sha1recompress_fast_43(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE44
	case 44:
		sha1recompress_fast_44(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE45
	case 45:
		sha1recompress_fast_45(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE46
	case 46:
		sha1recompress_fast_46(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE47
	case 47:
		sha1recompress_fast_47(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE48
	case 48:
		sha1recompress_fast_48(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE49
	case 49:
		sha1recompress_fast_49(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE50
	case 50:
		sha1recompress_fast_50(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE51
	case 51:
		sha1recompress_fast_51(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE52
	case 52:
		sha1recompress_fast_52(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE53
	case 53:
		sha1recompress_fast_53(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE54
	case 54:
		sha1recompress_fast_54(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE55
	case 55:
		sha1recompress_fast_55(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE56
	case 56:
		sha1recompress_fast_56(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE57
	case 57:
		sha1recompress_fast_57(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE58
	case 58:
		sha1recompress_fast_58(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE59
	case 59:
		sha1recompress_fast_59(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE60
	case 60:
		sha1recompress_fast_60(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE61
	case 61:
		sha1recompress_fast_61(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE62
	case 62:
		sha1recompress_fast_62(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE63
	case 63:
		sha1recompress_fast_63(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE64
	case 64:
		sha1recompress_fast_64(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE65
	case 65:
		sha1recompress_fast_65(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE66
	case 66:
		sha1recompress_fast_66(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE67
	case 67:
		sha1recompress_fast_67(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE68
	case 68:
		sha1recompress_fast_68(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE69
	case 69:
		sha1recompress_fast_69(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE70
	case 70:
		sha1recompress_fast_70(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE71
	case 71:
		sha1recompress_fast_71(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE72
	case 72:
		sha1recompress_fast_72(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE73
	case 73:
		sha1recompress_fast_73(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE74
	case 74:
		sha1recompress_fast_74(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE75
	case 75:
		sha1recompress_fast_75(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE76
	case 76:
		sha1recompress_fast_76(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE77
	case 77:
		sha1recompress_fast_77(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE78
	case 78:
		sha1recompress_fast_78(ihvin, ihvout, me2, state);
		break;
#endif
#ifdef DOSTORESTATE79
	case 79:
		sha1recompress_fast_79(ihvin, ihvout, me2, state);
		break;
#endif
	default:
		abort();
	}

}



static void sha1_process(SHA1_CTX* ctx, const uint32_t block[16])
{
	unsigned i, j;
	uint32_t ubc_dv_mask[DVMASKSIZE] = { 0xFFFFFFFF };
	uint32_t ihvtmp[5];

	ctx->ihv1[0] = ctx->ihv[0];
	ctx->ihv1[1] = ctx->ihv[1];
	ctx->ihv1[2] = ctx->ihv[2];
	ctx->ihv1[3] = ctx->ihv[3];
	ctx->ihv1[4] = ctx->ihv[4];

	sha1_compression_states(ctx->ihv, block, ctx->m1, ctx->states);

	if (ctx->detect_coll)
	{
		if (ctx->ubc_check)
		{
			ubc_check(ctx->m1, ubc_dv_mask);
		}

		if (ubc_dv_mask[0] != 0)
		{
			for (i = 0; sha1_dvs[i].dvType != 0; ++i)
			{
				if (ubc_dv_mask[0] & ((uint32_t)(1) << sha1_dvs[i].maskb))
				{
					for (j = 0; j < 80; ++j)
						ctx->m2[j] = ctx->m1[j] ^ sha1_dvs[i].dm[j];

					sha1_recompression_step(sha1_dvs[i].testt, ctx->ihv2, ihvtmp, ctx->m2, ctx->states[sha1_dvs[i].testt]);

					/* to verify SHA-1 collision detection code with collisions for reduced-step SHA-1 */
					if ((0 == ((ihvtmp[0] ^ ctx->ihv[0]) | (ihvtmp[1] ^ ctx->ihv[1]) | (ihvtmp[2] ^ ctx->ihv[2]) | (ihvtmp[3] ^ ctx->ihv[3]) | (ihvtmp[4] ^ ctx->ihv[4])))
						|| (ctx->reduced_round_coll && 0==((ctx->ihv1[0] ^ ctx->ihv2[0]) | (ctx->ihv1[1] ^ ctx->ihv2[1]) | (ctx->ihv1[2] ^ ctx->ihv2[2]) | (ctx->ihv1[3] ^ ctx->ihv2[3]) | (ctx->ihv1[4] ^ ctx->ihv2[4]))))
					{
						ctx->found_collision = 1;

						if (ctx->safe_hash)
						{
							sha1_compression_W(ctx->ihv, ctx->m1);
							sha1_compression_W(ctx->ihv, ctx->m1);
						}

						break;
					}
				}
			}
		}
	}
}

void SHA1DCInit(SHA1_CTX* ctx)
{
	ctx->total = 0;
	ctx->ihv[0] = 0x67452301;
	ctx->ihv[1] = 0xEFCDAB89;
	ctx->ihv[2] = 0x98BADCFE;
	ctx->ihv[3] = 0x10325476;
	ctx->ihv[4] = 0xC3D2E1F0;
	ctx->found_collision = 0;
	ctx->safe_hash = SHA1DC_INIT_SAFE_HASH_DEFAULT;
	ctx->ubc_check = 1;
	ctx->detect_coll = 1;
	ctx->reduced_round_coll = 0;
	ctx->callback = NULL;
}

void SHA1DCSetSafeHash(SHA1_CTX* ctx, int safehash)
{
	if (safehash)
		ctx->safe_hash = 1;
	else
		ctx->safe_hash = 0;
}


void SHA1DCSetUseUBC(SHA1_CTX* ctx, int ubc_check)
{
	if (ubc_check)
		ctx->ubc_check = 1;
	else
		ctx->ubc_check = 0;
}

void SHA1DCSetUseDetectColl(SHA1_CTX* ctx, int detect_coll)
{
	if (detect_coll)
		ctx->detect_coll = 1;
	else
		ctx->detect_coll = 0;
}

void SHA1DCSetDetectReducedRoundCollision(SHA1_CTX* ctx, int reduced_round_coll)
{
	if (reduced_round_coll)
		ctx->reduced_round_coll = 1;
	else
		ctx->reduced_round_coll = 0;
}

void SHA1DCSetCallback(SHA1_CTX* ctx, collision_block_callback callback)
{
	ctx->callback = callback;
}

void SHA1DCUpdate(SHA1_CTX* ctx, const char* buf, size_t len)
{
	unsigned left, fill;

	if (len == 0)
		return;

	left = ctx->total & 63;
	fill = 64 - left;

	if (left && len >= fill)
	{
		ctx->total += fill;
		memcpy(ctx->buffer + left, buf, fill);
		sha1_process(ctx, (uint32_t*)(ctx->buffer));
		buf += fill;
		len -= fill;
		left = 0;
	}
	while (len >= 64)
	{
		ctx->total += 64;

#if defined(SHA1DC_ALLOW_UNALIGNED_ACCESS)
		sha1_process(ctx, (uint32_t*)(buf));
#else
		memcpy(ctx->buffer, buf, 64);
		sha1_process(ctx, (uint32_t*)(ctx->buffer));
#endif /* defined(SHA1DC_ALLOW_UNALIGNED_ACCESS) */
		buf += 64;
		len -= 64;
	}
	if (len > 0)
	{
		ctx->total += len;
		memcpy(ctx->buffer + left, buf, len);
	}
}

static const unsigned char sha1_padding[64] =
{
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

int SHA1DCFinal(unsigned char output[20], SHA1_CTX *ctx)
{
	uint32_t last = ctx->total & 63;
	uint32_t padn = (last < 56) ? (56 - last) : (120 - last);
	uint64_t total;
	SHA1DCUpdate(ctx, (const char*)(sha1_padding), padn);

	total = ctx->total - padn;
	total <<= 3;
	ctx->buffer[56] = (unsigned char)(total >> 56);
	ctx->buffer[57] = (unsigned char)(total >> 48);
	ctx->buffer[58] = (unsigned char)(total >> 40);
	ctx->buffer[59] = (unsigned char)(total >> 32);
	ctx->buffer[60] = (unsigned char)(total >> 24);
	ctx->buffer[61] = (unsigned char)(total >> 16);
	ctx->buffer[62] = (unsigned char)(total >> 8);
	ctx->buffer[63] = (unsigned char)(total);
	sha1_process(ctx, (uint32_t*)(ctx->buffer));
	output[0] = (unsigned char)(ctx->ihv[0] >> 24);
	output[1] = (unsigned char)(ctx->ihv[0] >> 16);
	output[2] = (unsigned char)(ctx->ihv[0] >> 8);
	output[3] = (unsigned char)(ctx->ihv[0]);
	output[4] = (unsigned char)(ctx->ihv[1] >> 24);
	output[5] = (unsigned char)(ctx->ihv[1] >> 16);
	output[6] = (unsigned char)(ctx->ihv[1] >> 8);
	output[7] = (unsigned char)(ctx->ihv[1]);
	output[8] = (unsigned char)(ctx->ihv[2] >> 24);
	output[9] = (unsigned char)(ctx->ihv[2] >> 16);
	output[10] = (unsigned char)(ctx->ihv[2] >> 8);
	output[11] = (unsigned char)(ctx->ihv[2]);
	output[12] = (unsigned char)(ctx->ihv[3] >> 24);
	output[13] = (unsigned char)(ctx->ihv[3] >> 16);
	output[14] = (unsigned char)(ctx->ihv[3] >> 8);
	output[15] = (unsigned char)(ctx->ihv[3]);
	output[16] = (unsigned char)(ctx->ihv[4] >> 24);
	output[17] = (unsigned char)(ctx->ihv[4] >> 16);
	output[18] = (unsigned char)(ctx->ihv[4] >> 8);
	output[19] = (unsigned char)(ctx->ihv[4]);
	return ctx->found_collision;
}

#ifdef SHA1DC_CUSTOM_TRAILING_INCLUDE_SHA1_C
#include SHA1DC_CUSTOM_TRAILING_INCLUDE_SHA1_C
#endif
