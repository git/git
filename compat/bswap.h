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

#undef bswap32

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))

#define bswap32(x) ({ \
	uint32_t __res; \
	if (__builtin_constant_p(x)) { \
		__res = default_swab32(x); \
	} else { \
		__asm__("bswap %0" : "=r" (__res) : "0" ((uint32_t)(x))); \
	} \
	__res; })

#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))

#include <stdlib.h>

#define bswap32(x) _byteswap_ulong(x)

#endif

#ifdef bswap32

#undef ntohl
#undef htonl
#define ntohl(x) bswap32(x)
#define htonl(x) bswap32(x)

#endif
