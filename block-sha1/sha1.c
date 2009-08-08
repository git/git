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
	int lenW = ctx->size & 63;

	ctx->size += len;

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
		if (lenW)
			return;
		blk_SHA1Block(ctx, ctx->W);
	}
	while (len >= 64) {
		blk_SHA1Block(ctx, data);
		data += 64;
		len -= 64;
	}
	if (len)
		memcpy(ctx->W, data, len);
}


void blk_SHA1_Final(unsigned char hashout[20], blk_SHA_CTX *ctx)
{
	static const unsigned char pad[64] = { 0x80 };
	unsigned int padlen[2];
	int i;

	/* Pad with a binary 1 (ie 0x80), then zeroes, then length
	 */
	padlen[0] = htonl(ctx->size >> 29);
	padlen[1] = htonl(ctx->size << 3);

	i = ctx->size & 63;
	blk_SHA1_Update(ctx, pad, 1+ (63 & (55 - i)));
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
#define setW(x, val) (*(volatile unsigned int *)&W(x) = (val))

/*
 * Where do we get the source from? The first 16 iterations get it from
 * the input data, the next mix it from the 512-bit array.
 */
#define SHA_SRC(t) htonl(data[t])
#define SHA_MIX(t) SHA_ROL(W(t+13) ^ W(t+8) ^ W(t+2) ^ W(t), 1)

#define SHA_ROUND(t, input, fn, constant, A, B, C, D, E) do { \
	unsigned int TEMP = input(t); setW(t, TEMP); \
	E += TEMP + SHA_ROL(A,5) + (fn) + (constant); \
	B = SHA_ROR(B, 2); } while (0)

#define T_0_15(t, A, B, C, D, E)  SHA_ROUND(t, SHA_SRC, (((C^D)&B)^D) , 0x5a827999, A, B, C, D, E )
#define T_16_19(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (((C^D)&B)^D) , 0x5a827999, A, B, C, D, E )
#define T_20_39(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (B^C^D) , 0x6ed9eba1, A, B, C, D, E )
#define T_40_59(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, ((B&C)+(D&(B^C))) , 0x8f1bbcdc, A, B, C, D, E )
#define T_60_79(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (B^C^D) ,  0xca62c1d6, A, B, C, D, E )

static void blk_SHA1Block(blk_SHA_CTX *ctx, const unsigned int *data)
{
	unsigned int A,B,C,D,E;
	unsigned int array[16];

	A = ctx->H[0];
	B = ctx->H[1];
	C = ctx->H[2];
	D = ctx->H[3];
	E = ctx->H[4];

	/* Round 1 - iterations 0-16 take their input from 'data' */
	T_0_15( 0, A, B, C, D, E);
	T_0_15( 1, E, A, B, C, D);
	T_0_15( 2, D, E, A, B, C);
	T_0_15( 3, C, D, E, A, B);
	T_0_15( 4, B, C, D, E, A);
	T_0_15( 5, A, B, C, D, E);
	T_0_15( 6, E, A, B, C, D);
	T_0_15( 7, D, E, A, B, C);
	T_0_15( 8, C, D, E, A, B);
	T_0_15( 9, B, C, D, E, A);
	T_0_15(10, A, B, C, D, E);
	T_0_15(11, E, A, B, C, D);
	T_0_15(12, D, E, A, B, C);
	T_0_15(13, C, D, E, A, B);
	T_0_15(14, B, C, D, E, A);
	T_0_15(15, A, B, C, D, E);

	/* Round 1 - tail. Input from 512-bit mixing array */
	T_16_19(16, E, A, B, C, D);
	T_16_19(17, D, E, A, B, C);
	T_16_19(18, C, D, E, A, B);
	T_16_19(19, B, C, D, E, A);

	/* Round 2 */
	T_20_39(20, A, B, C, D, E);
	T_20_39(21, E, A, B, C, D);
	T_20_39(22, D, E, A, B, C);
	T_20_39(23, C, D, E, A, B);
	T_20_39(24, B, C, D, E, A);
	T_20_39(25, A, B, C, D, E);
	T_20_39(26, E, A, B, C, D);
	T_20_39(27, D, E, A, B, C);
	T_20_39(28, C, D, E, A, B);
	T_20_39(29, B, C, D, E, A);
	T_20_39(30, A, B, C, D, E);
	T_20_39(31, E, A, B, C, D);
	T_20_39(32, D, E, A, B, C);
	T_20_39(33, C, D, E, A, B);
	T_20_39(34, B, C, D, E, A);
	T_20_39(35, A, B, C, D, E);
	T_20_39(36, E, A, B, C, D);
	T_20_39(37, D, E, A, B, C);
	T_20_39(38, C, D, E, A, B);
	T_20_39(39, B, C, D, E, A);

	/* Round 3 */
	T_40_59(40, A, B, C, D, E);
	T_40_59(41, E, A, B, C, D);
	T_40_59(42, D, E, A, B, C);
	T_40_59(43, C, D, E, A, B);
	T_40_59(44, B, C, D, E, A);
	T_40_59(45, A, B, C, D, E);
	T_40_59(46, E, A, B, C, D);
	T_40_59(47, D, E, A, B, C);
	T_40_59(48, C, D, E, A, B);
	T_40_59(49, B, C, D, E, A);
	T_40_59(50, A, B, C, D, E);
	T_40_59(51, E, A, B, C, D);
	T_40_59(52, D, E, A, B, C);
	T_40_59(53, C, D, E, A, B);
	T_40_59(54, B, C, D, E, A);
	T_40_59(55, A, B, C, D, E);
	T_40_59(56, E, A, B, C, D);
	T_40_59(57, D, E, A, B, C);
	T_40_59(58, C, D, E, A, B);
	T_40_59(59, B, C, D, E, A);

	/* Round 4 */
	T_60_79(60, A, B, C, D, E);
	T_60_79(61, E, A, B, C, D);
	T_60_79(62, D, E, A, B, C);
	T_60_79(63, C, D, E, A, B);
	T_60_79(64, B, C, D, E, A);
	T_60_79(65, A, B, C, D, E);
	T_60_79(66, E, A, B, C, D);
	T_60_79(67, D, E, A, B, C);
	T_60_79(68, C, D, E, A, B);
	T_60_79(69, B, C, D, E, A);
	T_60_79(70, A, B, C, D, E);
	T_60_79(71, E, A, B, C, D);
	T_60_79(72, D, E, A, B, C);
	T_60_79(73, C, D, E, A, B);
	T_60_79(74, B, C, D, E, A);
	T_60_79(75, A, B, C, D, E);
	T_60_79(76, E, A, B, C, D);
	T_60_79(77, D, E, A, B, C);
	T_60_79(78, C, D, E, A, B);
	T_60_79(79, B, C, D, E, A);

	ctx->H[0] += A;
	ctx->H[1] += B;
	ctx->H[2] += C;
	ctx->H[3] += D;
	ctx->H[4] += E;
}
