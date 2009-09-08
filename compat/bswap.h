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
static inline unsigned int default_swab32(unsigned int val)
{
	return (((val & 0xff000000) >> 24) |
		((val & 0x00ff0000) >>  8) |
		((val & 0x0000ff00) <<  8) |
		((val & 0x000000ff) << 24));
}

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))

#define bswap32(x) ({ \
	unsigned int __res; \
	if (__builtin_constant_p(x)) { \
		__res = default_swab32(x); \
	} else { \
		__asm__("bswap %0" : "=r" (__res) : "0" (x)); \
	} \
	__res; })

#undef ntohl
#undef htonl
#define ntohl(x) bswap32(x)
#define htonl(x) bswap32(x)

#endif
