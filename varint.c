#include "git-compat-util.h"
#include "varint.h"

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
