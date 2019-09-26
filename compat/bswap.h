#ifndef COMPAT_BSWAP_H
#define COMPAT_BSWAP_H

/*
 * Let's make sure we always have a sane definition for ntohl()/htonl().
 * Some libraries define those as a function call, just to perform byte
 * shifting, bringing significant overhead to what should be a simple
 * operation.
 */

/*
 * Default version that the compiler ought to optimize properly with
 * constant values.
 */
static inline uint32_t default_swab32(uint32_t val)
{
	return (((val & 0xff000000) >> 24) |
		((val & 0x00ff0000) >>  8) |
		((val & 0x0000ff00) <<  8) |
		((val & 0x000000ff) << 24));
}

static inline uint64_t default_bswap64(uint64_t val)
{
	return (((val & (uint64_t)0x00000000000000ffULL) << 56) |
		((val & (uint64_t)0x000000000000ff00ULL) << 40) |
		((val & (uint64_t)0x0000000000ff0000ULL) << 24) |
		((val & (uint64_t)0x00000000ff000000ULL) <<  8) |
		((val & (uint64_t)0x000000ff00000000ULL) >>  8) |
		((val & (uint64_t)0x0000ff0000000000ULL) >> 24) |
		((val & (uint64_t)0x00ff000000000000ULL) >> 40) |
		((val & (uint64_t)0xff00000000000000ULL) >> 56));
}

#undef bswap32
#undef bswap64

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))

#define bswap32 git_bswap32
static inline uint32_t git_bswap32(uint32_t x)
{
	uint32_t result;
	if (__builtin_constant_p(x))
		result = default_swab32(x);
	else
		__asm__("bswap %0" : "=r" (result) : "0" (x));
	return result;
}

#define bswap64 git_bswap64
#if defined(__x86_64__)
static inline uint64_t git_bswap64(uint64_t x)
{
	uint64_t result;
	if (__builtin_constant_p(x))
		result = default_bswap64(x);
	else
		__asm__("bswap %q0" : "=r" (result) : "0" (x));
	return result;
}
#else
static inline uint64_t git_bswap64(uint64_t x)
{
	union { uint64_t i64; uint32_t i32[2]; } tmp, result;
	if (__builtin_constant_p(x))
		result.i64 = default_bswap64(x);
	else {
		tmp.i64 = x;
		result.i32[0] = git_bswap32(tmp.i32[1]);
		result.i32[1] = git_bswap32(tmp.i32[0]);
	}
	return result.i64;
}
#endif

#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))

#include <stdlib.h>

#define bswap32(x) _byteswap_ulong(x)
#define bswap64(x) _byteswap_uint64(x)

#endif

#if defined(bswap32)

#undef ntohl
#undef htonl
#define ntohl(x) bswap32(x)
#define htonl(x) bswap32(x)

#endif

#if defined(bswap64)

#undef ntohll
#undef htonll
#define ntohll(x) bswap64(x)
#define htonll(x) bswap64(x)

#else

#undef ntohll
#undef htonll

#if defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && defined(__BIG_ENDIAN)

# define GIT_BYTE_ORDER __BYTE_ORDER
# define GIT_LITTLE_ENDIAN __LITTLE_ENDIAN
# define GIT_BIG_ENDIAN __BIG_ENDIAN

#elif defined(BYTE_ORDER) && defined(LITTLE_ENDIAN) && defined(BIG_ENDIAN)

# define GIT_BYTE_ORDER BYTE_ORDER
# define GIT_LITTLE_ENDIAN LITTLE_ENDIAN
# define GIT_BIG_ENDIAN BIG_ENDIAN

#else

# define GIT_BIG_ENDIAN 4321
# define GIT_LITTLE_ENDIAN 1234

# if defined(_BIG_ENDIAN) && !defined(_LITTLE_ENDIAN)
#  define GIT_BYTE_ORDER GIT_BIG_ENDIAN
# elif defined(_LITTLE_ENDIAN) && !defined(_BIG_ENDIAN)
#  define GIT_BYTE_ORDER GIT_LITTLE_ENDIAN
# elif defined(__THW_BIG_ENDIAN__) && !defined(__THW_LITTLE_ENDIAN__)
#  define GIT_BYTE_ORDER GIT_BIG_ENDIAN
# elif defined(__THW_LITTLE_ENDIAN__) && !defined(__THW_BIG_ENDIAN__)
#  define GIT_BYTE_ORDER GIT_LITTLE_ENDIAN
# else
#  error "Cannot determine endianness"
# endif

#endif

#if GIT_BYTE_ORDER == GIT_BIG_ENDIAN
# define ntohll(n) (n)
# define htonll(n) (n)
#else
# define ntohll(n) default_bswap64(n)
# define htonll(n) default_bswap64(n)
#endif

#endif

/*
 * Performance might be improved if the CPU architecture is OK with
 * unaligned 32-bit loads and a fast ntohl() is available.
 * Otherwise fall back to byte loads and shifts which is portable,
 * and is faster on architectures with memory alignment issues.
 */

#if !defined(NO_UNALIGNED_LOADS) && ( \
    defined(__i386__) || defined(__x86_64__) || \
    defined(_M_IX86) || defined(_M_X64) || \
    defined(__ppc__) || defined(__ppc64__) || \
    defined(__powerpc__) || defined(__powerpc64__) || \
    defined(__s390__) || defined(__s390x__))

#define get_be16(p)	ntohs(*(unsigned short *)(p))
#define get_be32(p)	ntohl(*(unsigned int *)(p))
#define get_be64(p)	ntohll(*(uint64_t *)(p))
#define put_be32(p, v)	do { *(unsigned int *)(p) = htonl(v); } while (0)
#define put_be64(p, v)	do { *(uint64_t *)(p) = htonll(v); } while (0)

#else

static inline uint16_t get_be16(const void *ptr)
{
	const unsigned char *p = ptr;
	return	(uint16_t)p[0] << 8 |
		(uint16_t)p[1] << 0;
}

static inline uint32_t get_be32(const void *ptr)
{
	const unsigned char *p = ptr;
	return	(uint32_t)p[0] << 24 |
		(uint32_t)p[1] << 16 |
		(uint32_t)p[2] <<  8 |
		(uint32_t)p[3] <<  0;
}

static inline uint64_t get_be64(const void *ptr)
{
	const unsigned char *p = ptr;
	return	(uint64_t)get_be32(&p[0]) << 32 |
		(uint64_t)get_be32(&p[4]) <<  0;
}

static inline void put_be32(void *ptr, uint32_t value)
{
	unsigned char *p = ptr;
	p[0] = value >> 24;
	p[1] = value >> 16;
	p[2] = value >>  8;
	p[3] = value >>  0;
}

static inline void put_be64(void *ptr, uint64_t value)
{
	unsigned char *p = ptr;
	p[0] = value >> 56;
	p[1] = value >> 48;
	p[2] = value >> 40;
	p[3] = value >> 32;
	p[4] = value >> 24;
	p[5] = value >> 16;
	p[6] = value >>  8;
	p[7] = value >>  0;
}

#endif

#endif /* COMPAT_BSWAP_H */
