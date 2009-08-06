/*
 * Based on the Mozilla SHA1 (see mozilla-sha1/sha1.c),
 * optimized to do word accesses rather than byte accesses,
 * and to avoid unnecessary copies into the context array.
 */

#include <string.h>
#include <arpa/inet.h>

#include "sha1.h"

/* Hash one 64-byte block of data */
static void blk_SHA1Block(blk_SHA_CTX *ctx, const unsigned int *data);

void blk_SHA1_Init(blk_SHA_CTX *ctx)
{
	ctx->lenW = 0;
	ctx->size = 0;

	/* Initialize H with the magic constants (see FIPS180 for constants)
	 */
	ctx->H[0] = 0x67452301;
	ctx->H[1] = 0xefcdab89;
	ctx->H[2] = 0x98badcfe;
	ctx->H[3] = 0x10325476;
	ctx->H[4] = 0xc3d2e1f0;
}


void blk_SHA1_Update(blk_SHA_CTX *ctx, const void *data, unsigned long len)
{
	int lenW = ctx->lenW;

	ctx->size += (unsigned long long) len << 3;

	/* Read the data into W and process blocks as they get full
	 */
	if (lenW) {
		int left = 64 - lenW;
		if (len < left)
			left = len;
		memcpy(lenW + (char *)ctx->W, data, left);
		lenW = (lenW + left) & 63;
		len -= left;
		data += left;
		ctx->lenW = lenW;
		if (lenW)
			return;
		blk_SHA1Block(ctx, ctx->W);
	}
	while (len >= 64) {
		blk_SHA1Block(ctx, data);
		data += 64;
		len -= 64;
	}
	if (len) {
		memcpy(ctx->W, data, len);
		ctx->lenW = len;
	}
}


void blk_SHA1_Final(unsigned char hashout[20], blk_SHA_CTX *ctx)
{
	static const unsigned char pad[64] = { 0x80 };
	unsigned int padlen[2];
	int i;

	/* Pad with a binary 1 (ie 0x80), then zeroes, then length
	 */
	padlen[0] = htonl(ctx->size >> 32);
	padlen[1] = htonl(ctx->size);

	blk_SHA1_Update(ctx, pad, 1+ (63 & (55 - ctx->lenW)));
	blk_SHA1_Update(ctx, padlen, 8);

	/* Output hash
	 */
	for (i = 0; i < 5; i++)
		((unsigned int *)hashout)[i] = htonl(ctx->H[i]);
}

#if defined(__i386__) || defined(__x86_64__)

#define SHA_ASM(op, x, n) ({ unsigned int __res; __asm__(op " %1,%0":"=r" (__res):"i" (n), "0" (x)); __res; })
#define SHA_ROL(x,n)	SHA_ASM("rol", x, n)
#define SHA_ROR(x,n)	SHA_ASM("ror", x, n)

#else

#define SHA_ROT(X,l,r)	(((X) << (l)) | ((X) >> (r)))
#define SHA_ROL(X,n)	SHA_ROT(X,n,32-(n))
#define SHA_ROR(X,n)	SHA_ROT(X,32-(n),n)

#endif

/* This "rolls" over the 512-bit array */
#define W(x) (array[(x)&15])

/*
 * Where do we get the source from? The first 16 iterations get it from
 * the input data, the next mix it from the 512-bit array.
 */
#define SHA_SRC(t) htonl(data[t])
#define SHA_MIX(t) SHA_ROL(W(t+13) ^ W(t+8) ^ W(t+2) ^ W(t), 1)

#define SHA_ROUND(t, input, fn, constant) \
	TEMP = input(t); W(t) = TEMP; \
	TEMP += SHA_ROL(A,5) + (fn) + E + (constant); \
	E = D; D = C; C = SHA_ROR(B, 2); B = A; A = TEMP

#define T_0_15(t)  SHA_ROUND(t, SHA_SRC, (((C^D)&B)^D) , 0x5a827999 )
#define T_16_19(t) SHA_ROUND(t, SHA_MIX, (((C^D)&B)^D) , 0x5a827999 )
#define T_20_39(t) SHA_ROUND(t, SHA_MIX, (B^C^D) , 0x6ed9eba1 )
#define T_40_59(t) SHA_ROUND(t, SHA_MIX, ((B&C)+(D&(B^C))) , 0x8f1bbcdc )
#define T_60_79(t) SHA_ROUND(t, SHA_MIX, (B^C^D) ,  0xca62c1d6 )

static void blk_SHA1Block(blk_SHA_CTX *ctx, const unsigned int *data)
{
	unsigned int A,B,C,D,E,TEMP;
	unsigned int array[16];

	A = ctx->H[0];
	B = ctx->H[1];
	C = ctx->H[2];
	D = ctx->H[3];
	E = ctx->H[4];

	/* Round 1 - iterations 0-16 take their input from 'data' */
	T_0_15( 0); T_0_15( 1); T_0_15( 2); T_0_15( 3); T_0_15( 4);
	T_0_15( 5); T_0_15( 6); T_0_15( 7); T_0_15( 8); T_0_15( 9);
	T_0_15(10); T_0_15(11); T_0_15(12); T_0_15(13); T_0_15(14);
	T_0_15(15);

	/* Round 1 - tail. Input from 512-bit mixing array */
	T_16_19(16); T_16_19(17); T_16_19(18); T_16_19(19);

	/* Round 2 */
	T_20_39(20); T_20_39(21); T_20_39(22); T_20_39(23); T_20_39(24);
	T_20_39(25); T_20_39(26); T_20_39(27); T_20_39(28); T_20_39(29);
	T_20_39(30); T_20_39(31); T_20_39(32); T_20_39(33); T_20_39(34);
	T_20_39(35); T_20_39(36); T_20_39(37); T_20_39(38); T_20_39(39);

	/* Round 3 */
	T_40_59(40); T_40_59(41); T_40_59(42); T_40_59(43); T_40_59(44);
	T_40_59(45); T_40_59(46); T_40_59(47); T_40_59(48); T_40_59(49);
	T_40_59(50); T_40_59(51); T_40_59(52); T_40_59(53); T_40_59(54);
	T_40_59(55); T_40_59(56); T_40_59(57); T_40_59(58); T_40_59(59);

	/* Round 4 */
	T_60_79(60); T_60_79(61); T_60_79(62); T_60_79(63); T_60_79(64);
	T_60_79(65); T_60_79(66); T_60_79(67); T_60_79(68); T_60_79(69);
	T_60_79(70); T_60_79(71); T_60_79(72); T_60_79(73); T_60_79(74);
	T_60_79(75); T_60_79(76); T_60_79(77); T_60_79(78); T_60_79(79);

	ctx->H[0] += A;
	ctx->H[1] += B;
	ctx->H[2] += C;
	ctx->H[3] += D;
	ctx->H[4] += E;
}
