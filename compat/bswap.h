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

#if !defined(__BYTE_ORDER)
# if defined(BYTE_ORDER) && defined(LITTLE_ENDIAN) && defined(BIG_ENDIAN)
#  define __BYTE_ORDER BYTE_ORDER
#  define __LITTLE_ENDIAN LITTLE_ENDIAN
#  define __BIG_ENDIAN BIG_ENDIAN
# endif
#endif

#if !defined(__BYTE_ORDER)
# error "Cannot determine endianness"
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
# define ntohll(n) (n)
# define htonll(n) (n)
#else
# define ntohll(n) default_bswap64(n)
# define htonll(n) default_bswap64(n)
#endif

#endif
