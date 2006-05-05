#include "cache.h"

#undef DEBUG_85

#ifdef DEBUG_85
#define say(a) fprintf(stderr, a)
#define say1(a,b) fprintf(stderr, a, b)
#define say2(a,b,c) fprintf(stderr, a, b, c)
#else
#define say(a) do {} while(0)
#define say1(a,b) do {} while(0)
#define say2(a,b,c) do {} while(0)
#endif

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
	int i;
	if (de85['Z'])
		return;
	for (i = 0; i < ARRAY_SIZE(en85); i++) {
		int ch = en85[i];
		de85[ch] = i + 1;
	}
}

int decode_85(char *dst, char *buffer, int len)
{
	prep_base85();

	say2("decode 85 <%.*s>", len/4*5, buffer);
	while (len) {
		unsigned acc = 0;
		int cnt;
		for (cnt = 0; cnt < 5; cnt++, buffer++) {
			int ch = *((unsigned char *)buffer);
			int de = de85[ch];
			if (!de)
				return error("invalid base85 alphabet %c", ch);
			de--;
			if (cnt == 4) {
				/*
				 * Detect overflow.  The largest
				 * 5-letter possible is "|NsC0" to
				 * encode 0xffffffff, and "|NsC" gives
				 * 0x03030303 at this point (i.e.
				 * 0xffffffff = 0x03030303 * 85).
				 */
				if (0x03030303 < acc ||
				    (0x03030303 == acc && de))
					error("invalid base85 sequence %.5s",
					      buffer-3);
			}
			acc = acc * 85 + de;
			say1(" <%08x>", acc);
		}
		say1(" %08x", acc);
		for (cnt = 0; cnt < 4 && len; cnt++, len--) {
			*dst++ = (acc >> 24) & 0xff;
			acc = acc << 8;
		}
	}
	say("\n");

	return 0;
}

void encode_85(char *buf, unsigned char *data, int bytes)
{
	prep_base85();

	say("encode 85");
	while (bytes) {
		unsigned acc = 0;
		int cnt;
		for (cnt = 0; cnt < 4 && bytes; cnt++, bytes--) {
			int ch = *data++;
			acc |= ch << ((3-cnt)*8);
		}
		say1(" %08x", acc);
		for (cnt = 0; cnt < 5; cnt++) {
			int val = acc % 85;
			acc /= 85;
			buf[4-cnt] = en85[val];
		}
		buf += 5;
	}
	say("\n");

	*buf = 0;
}

#ifdef DEBUG_85
int main(int ac, char **av)
{
	char buf[1024];

	if (!strcmp(av[1], "-e")) {
		int len = strlen(av[2]);
		encode_85(buf, av[2], len);
		if (len <= 26) len = len + 'A' - 1;
		else len = len + 'a' - 26 + 1;
		printf("encoded: %c%s\n", len, buf);
		return 0;
	}
	if (!strcmp(av[1], "-d")) {
		int len = *av[2];
		if ('A' <= len && len <= 'Z') len = len - 'A' + 1;
		else len = len - 'a' + 26 + 1;
		decode_85(buf, av[2]+1, len);
		printf("decoded: %.*s\n", len, buf);
		return 0;
	}
	if (!strcmp(av[1], "-t")) {
		char t[4] = { -1,-1,-1,-1 };
		encode_85(buf, t, 4);
		printf("encoded: D%s\n", buf);
		return 0;
	}
}
#endif
