#include "git-compat-util.h"
#include "base85.h"

static const char en85[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
	'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z',
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z',
	'!', '#', '$', '%', '&', '(', ')', '*', '+', '-',
	';', '<', '=', '>', '?', '@', '^', '_',	'`', '{',
	'|', '}', '~'
};

static char de85[256];
static void prep_base85(void)
{
	if (de85['Z'])
		return;
	for (size_t i = 0; i < ARRAY_SIZE(en85); i++) {
		int ch = en85[i];
		de85[ch] = i + 1;
	}
}

int decode_85(char *dst, const char *buffer, int len)
{
	prep_base85();

	while (len) {
		unsigned acc = 0;
		int de, cnt = 4;
		unsigned char ch;
		do {
			ch = *buffer++;
			de = de85[ch];
			if (--de < 0)
				return error("invalid base85 alphabet %c", ch);
			acc = acc * 85 + de;
		} while (--cnt);
		ch = *buffer++;
		de = de85[ch];
		if (--de < 0)
			return error("invalid base85 alphabet %c", ch);
		/* Detect overflow. */
		if (0xffffffff / 85 < acc ||
		    0xffffffff - de < (acc *= 85))
			return error("invalid base85 sequence %.5s", buffer-5);
		acc += de;

		cnt = (len < 4) ? len : 4;
		len -= cnt;
		do {
			acc = (acc << 8) | (acc >> 24);
			*dst++ = acc;
		} while (--cnt);
	}

	return 0;
}

static void encode_85(char *buf, const unsigned char *data, int bytes)
{
	while (bytes) {
		unsigned acc = 0;
		int cnt;
		for (cnt = 24; cnt >= 0; cnt -= 8) {
			unsigned ch = *data++;
			acc |= ch << cnt;
			if (--bytes == 0)
				break;
		}
		for (cnt = 4; cnt >= 0; cnt--) {
			int val = acc % 85;
			acc /= 85;
			buf[cnt] = en85[val];
		}
		buf += 5;
	}

	*buf = 0;
}

/*
 * Split the provided data into chunks of at most 52 bytes. Each chunk
 * is base85-encoded into a line with the following format:
 *  - Byte 0: encodes the chunk size (1-52) as a letter: 'A'-'Z' for
 *    1-26 bytes, 'a'-'z' for 27-52 bytes.
 *  - Bytes 1 onwards: base85-encoded data (up to 65 bytes).
 *  - Followed by a newline and NUL terminator.
 * The output callback is called once per line.
 */
void chunk_and_encode85(const unsigned char *data, unsigned long len,
			each_encoded_line_fn output, void *output_data)
{
	while (len) {
		int line_len;
		int bytes = (BASE85_CHUNK_SIZE < len) ? BASE85_CHUNK_SIZE : len;
		char line[71];
		len -= bytes;
		if (bytes <= 26)
			line[0] = bytes + 'A' - 1;
		else
			line[0] = bytes - 26 + 'a' - 1;
		encode_85(line + 1, data, bytes);
		data = data + bytes;

		line_len = strlen(line);
		line[line_len++] = '\n';
		line[line_len] = '\0';
		output(line, line_len, output_data);
	}
}
