#include "git-compat-util.h"
#include "varint.h"

/*
 * When building with Rust we don't compile the C code, but we only verify
 * whether the function signatures of our C bindings match the ones we have
 * declared in "varint.h".
 */
#ifdef WITH_RUST
# include "c-bindings.h"
#else
uint64_t decode_varint(const unsigned char **bufp)
{
	const unsigned char *buf = *bufp;
	unsigned char c = *buf++;
	uint64_t val = c & 127;
	while (c & 128) {
		val += 1;
		if (!val || MSB(val, 7))
			return 0; /* overflow */
		c = *buf++;
		val = (val << 7) + (c & 127);
	}
	*bufp = buf;
	return val;
}

uint8_t encode_varint(uint64_t value, unsigned char *buf)
{
	unsigned char varint[16];
	unsigned pos = sizeof(varint) - 1;
	varint[pos] = value & 127;
	while (value >>= 7)
		varint[--pos] = 128 | (--value & 127);
	if (buf)
		memcpy(buf, varint + pos, sizeof(varint) - pos);
	return sizeof(varint) - pos;
}
#endif
