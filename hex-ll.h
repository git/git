#ifndef HEX_LL_H
#define HEX_LL_H

enum hexkind {
	HEX_KIND_MIXED = 0,
	HEX_KIND_LOWER = 1,
};

#ifdef WITH_BREAKING_CHANGES
#define HEX_KIND_OID HEX_KIND_LOWER
#else
#define HEX_KIND_OID HEX_KIND_MIXED
#endif

extern const signed char hexval_table[256];
extern const signed char hexval_lc_table[256];
static inline unsigned int hexval(unsigned char c, enum hexkind kind)
{
	return kind == HEX_KIND_MIXED ? hexval_table[c] : hexval_lc_table[c];
}

/*
 * Convert two consecutive hexadecimal digits into a char.  Return a
 * negative value on error.  Don't run over the end of short strings.
 */
static inline int hex2chr(const char *s, enum hexkind kind)
{
	unsigned int val = hexval(s[0], kind);
	return (val & ~0xf) ? val : (val << 4) | hexval(s[1], kind);
}

/*
 * Read `len` pairs of hexadecimal digits from `hex` and write the
 * values to `binary` as `len` bytes. Return 0 on success, or -1 if
 * the input does not consist of hex digits).
 */
int hex_to_bytes(unsigned char *binary, const char *hex, size_t len, enum hexkind kind);

#endif
