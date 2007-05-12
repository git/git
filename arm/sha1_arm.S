/*
 *  SHA transform optimized for ARM
 *
 *  Copyright:	(C) 2005 by Nicolas Pitre <nico@cam.org>
 *  Created:	September 17, 2005
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

	.text
	.globl	sha_transform

/*
 * void sha_transform(uint32_t *hash, const unsigned char *data, uint32_t *W);
 *
 * note: the "data" pointer may be unaligned.
 */

sha_transform:

	stmfd	sp!, {r4 - r8, lr}

	@ for (i = 0; i < 16; i++)
	@         W[i] = ntohl(((uint32_t *)data)[i]);

#ifdef __ARMEB__
	mov	r4, r0
	mov	r0, r2
	mov	r2, #64
	bl	memcpy
	mov	r2, r0
	mov	r0, r4
#else
	mov	r3, r2
	mov	lr, #16
1:	ldrb	r4, [r1], #1
	ldrb	r5, [r1], #1
	ldrb	r6, [r1], #1
	ldrb	r7, [r1], #1
	subs	lr, lr, #1
	orr	r5, r5, r4, lsl #8
	orr	r6, r6, r5, lsl #8
	orr	r7, r7, r6, lsl #8
	str	r7, [r3], #4
	bne	1b
#endif

	@ for (i = 0; i < 64; i++)
	@         W[i+16] = ror(W[i+13] ^ W[i+8] ^ W[i+2] ^ W[i], 31);

	sub	r3, r2, #4
	mov	lr, #64
2:	ldr	r4, [r3, #4]!
	subs	lr, lr, #1
	ldr	r5, [r3, #8]
	ldr	r6, [r3, #32]
	ldr	r7, [r3, #52]
	eor	r4, r4, r5
	eor	r4, r4, r6
	eor	r4, r4, r7
	mov	r4, r4, ror #31
	str	r4, [r3, #64]
	bne	2b

	/*
	 * The SHA functions are:
	 *
	 * f1(B,C,D) = (D ^ (B & (C ^ D)))
	 * f2(B,C,D) = (B ^ C ^ D)
	 * f3(B,C,D) = ((B & C) | (D & (B | C)))
	 *
	 * Then the sub-blocks are processed as follows:
	 *
	 * A' = ror(A, 27) + f(B,C,D) + E + K + *W++
	 * B' = A
	 * C' = ror(B, 2)
	 * D' = C
	 * E' = D
	 *
	 * We therefore unroll each loop 5 times to avoid register shuffling.
	 * Also the ror for C (and also D and E which are successivelyderived
	 * from it) is applied in place to cut on an additional mov insn for
	 * each round.
	 */

	.macro	sha_f1, A, B, C, D, E
	ldr	r3, [r2], #4
	eor	ip, \C, \D
	add	\E, r1, \E, ror #2
	and	ip, \B, ip, ror #2
	add	\E, \E, \A, ror #27
	eor	ip, ip, \D, ror #2
	add	\E, \E, r3
	add	\E, \E, ip
	.endm

	.macro	sha_f2, A, B, C, D, E
	ldr	r3, [r2], #4
	add	\E, r1, \E, ror #2
	eor	ip, \B, \C, ror #2
	add	\E, \E, \A, ror #27
	eor	ip, ip, \D, ror #2
	add	\E, \E, r3
	add	\E, \E, ip
	.endm

	.macro	sha_f3, A, B, C, D, E
	ldr	r3, [r2], #4
	add	\E, r1, \E, ror #2
	orr	ip, \B, \C, ror #2
	add	\E, \E, \A, ror #27
	and	ip, ip, \D, ror #2
	add	\E, \E, r3
	and	r3, \B, \C, ror #2
	orr	ip, ip, r3
	add	\E, \E, ip
	.endm

	ldmia	r0, {r4 - r8}

	mov	lr, #4
	ldr	r1, .L_sha_K + 0

	/* adjust initial values */
	mov	r6, r6, ror #30
	mov	r7, r7, ror #30
	mov	r8, r8, ror #30

3:	subs	lr, lr, #1
	sha_f1	r4, r5, r6, r7, r8
	sha_f1	r8, r4, r5, r6, r7
	sha_f1	r7, r8, r4, r5, r6
	sha_f1	r6, r7, r8, r4, r5
	sha_f1	r5, r6, r7, r8, r4
	bne	3b

	ldr	r1, .L_sha_K + 4
	mov	lr, #4

4:	subs	lr, lr, #1
	sha_f2	r4, r5, r6, r7, r8
	sha_f2	r8, r4, r5, r6, r7
	sha_f2	r7, r8, r4, r5, r6
	sha_f2	r6, r7, r8, r4, r5
	sha_f2	r5, r6, r7, r8, r4
	bne	4b

	ldr	r1, .L_sha_K + 8
	mov	lr, #4

5:	subs	lr, lr, #1
	sha_f3	r4, r5, r6, r7, r8
	sha_f3	r8, r4, r5, r6, r7
	sha_f3	r7, r8, r4, r5, r6
	sha_f3	r6, r7, r8, r4, r5
	sha_f3	r5, r6, r7, r8, r4
	bne	5b

	ldr	r1, .L_sha_K + 12
	mov	lr, #4

6:	subs	lr, lr, #1
	sha_f2	r4, r5, r6, r7, r8
	sha_f2	r8, r4, r5, r6, r7
	sha_f2	r7, r8, r4, r5, r6
	sha_f2	r6, r7, r8, r4, r5
	sha_f2	r5, r6, r7, r8, r4
	bne	6b

	ldmia	r0, {r1, r2, r3, ip, lr}
	add	r4, r1, r4
	add	r5, r2, r5
	add	r6, r3, r6, ror #2
	add	r7, ip, r7, ror #2
	add	r8, lr, r8, ror #2
	stmia	r0, {r4 - r8}

	ldmfd	sp!, {r4 - r8, pc}

.L_sha_K:
	.word	0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6

