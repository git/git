#include "cache.h"

#undef DEBUG_85

#ifdef DEBUG_85
#define say(a) fprintf(stderr, a)
#define say1(a,b) fprintf(stderr, a, b)
#define say2(a,b,c) fprintf(stderr, a, b, c)
#else
#define say(a) do { /* nothing */ } while (0)
#define say1(a,b) do { /* nothing */ } while (0)
#define say2(a,b,c) do { /* nothing */ } while (0)
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

int decode_85(char *dst, const char *buffer, int len)
{
	prep_base85();

	say2("decode 85 <%.*s>", len / 4 * 5, buffer);
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
		say1(" %08x", acc);

		cnt = (len < 4) ? len : 4;
		len -= cnt;
		do {
			acc = (acc << 8) | (acc >> 24);
			*dst++ = acc;
		} while (--cnt);
	}
	say("\n");

	return 0;
}

void encode_85(char *buf, const unsigned char *data, int bytes)
{
	say("encode 85");
	while (bytes) {
		unsigned acc = 0;
		int cnt;
		for (cnt = 24; cnt >= 0; cnt -= 8) {
			unsigned ch = *data++;
			acc |= ch << cnt;
			if (--bytes == 0)
				break;
		}
		say1(" %08x", acc);
		for (cnt = 4; cnt >= 0; cnt--) {
			int val = acc % 85;
			acc /= 85;
			buf[cnt] = en85[val];
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
		else len = len + 'a' - 26 - 1;
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
