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

/*
 * __has_builtin is available since Clang 10 and GCC 10.
 * Below is a fallback for older compilers.
 */
#ifndef __has_builtin
# define __has_builtin(x) 0
#endif

#undef bswap32
#undef bswap64

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM64))

#include <stdlib.h>

#define bswap32(x) _byteswap_ulong(x)
#define bswap64(x) _byteswap_uint64(x)

#define GIT_LITTLE_ENDIAN 1234
#define GIT_BIG_ENDIAN 4321
#define GIT_BYTE_ORDER GIT_LITTLE_ENDIAN

#elif __has_builtin(__builtin_bswap32) && __has_builtin(__builtin_bswap64)

#define bswap32(x) __builtin_bswap32((x))
#define bswap64(x) __builtin_bswap64((x))

#endif

#if defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) && defined(__BIG_ENDIAN)

# define GIT_BYTE_ORDER __BYTE_ORDER
# define GIT_LITTLE_ENDIAN __LITTLE_ENDIAN
# define GIT_BIG_ENDIAN __BIG_ENDIAN

#elif defined(BYTE_ORDER) && defined(LITTLE_ENDIAN) && defined(BIG_ENDIAN)

# define GIT_BYTE_ORDER BYTE_ORDER
# define GIT_LITTLE_ENDIAN LITTLE_ENDIAN
# define GIT_BIG_ENDIAN BIG_ENDIAN

#elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)

# define GIT_BYTE_ORDER __BYTE_ORDER__
# define GIT_LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
# define GIT_BIG_ENDIAN __ORDER_BIG_ENDIAN__

#elif !defined(GIT_BYTE_ORDER)

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

#undef ntohl
#undef htonl
#undef ntohll
#undef htonll

#if GIT_BYTE_ORDER == GIT_BIG_ENDIAN
# define ntohl(x) (x)
# define htonl(x) (x)
# define ntohll(x) (x)
# define htonll(x) (x)
#else

# if defined(bswap32)
#  define ntohl(x) bswap32(x)
#  define htonl(x) bswap32(x)
# else
#  define ntohl(x) default_swab32(x)
#  define htonl(x) default_swab32(x)
# endif

# if defined(bswap64)
#  define ntohll(x) bswap64(x)
#  define htonll(x) bswap64(x)
# else
#  define ntohll(x) default_bswap64(x)
#  define htonll(x) default_bswap64(x)
# endif
#endif

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
	p[0] = (value >> 24) & 0xff;
	p[1] = (value >> 16) & 0xff;
	p[2] = (value >>  8) & 0xff;
	p[3] = (value >>  0) & 0xff;
}

static inline void put_be64(void *ptr, uint64_t value)
{
	unsigned char *p = ptr;
	p[0] = (value >> 56) & 0xff;
	p[1] = (value >> 48) & 0xff;
	p[2] = (value >> 40) & 0xff;
	p[3] = (value >> 32) & 0xff;
	p[4] = (value >> 24) & 0xff;
	p[5] = (value >> 16) & 0xff;
	p[6] = (value >>  8) & 0xff;
	p[7] = (value >>  0) & 0xff;
}

#endif /* COMPAT_BSWAP_H */
