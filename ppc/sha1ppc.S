/*
 * SHA-1 implementation for PowerPC.
 *
 * Copyright (C) 2005 Paul Mackerras <paulus@samba.org>
 */
#define FS	80

/*
 * We roll the registers for T, A, B, C, D, E around on each
 * iteration; T on iteration t is A on iteration t+1, and so on.
 * We use registers 7 - 12 for this.
 */
#define RT(t)	((((t)+5)%6)+7)
#define RA(t)	((((t)+4)%6)+7)
#define RB(t)	((((t)+3)%6)+7)
#define RC(t)	((((t)+2)%6)+7)
#define RD(t)	((((t)+1)%6)+7)
#define RE(t)	((((t)+0)%6)+7)

/* We use registers 16 - 31 for the W values */
#define W(t)	(((t)%16)+16)

#define STEPD0(t)				\
	and	%r6,RB(t),RC(t);		\
	andc	%r0,RD(t),RB(t);		\
	rotlwi	RT(t),RA(t),5;			\
	rotlwi	RB(t),RB(t),30;			\
	or	%r6,%r6,%r0;			\
	add	%r0,RE(t),%r15;			\
	add	RT(t),RT(t),%r6;		\
	add	%r0,%r0,W(t);			\
	add	RT(t),RT(t),%r0

#define STEPD1(t)				\
	xor	%r6,RB(t),RC(t);		\
	rotlwi	RT(t),RA(t),5;			\
	rotlwi	RB(t),RB(t),30;			\
	xor	%r6,%r6,RD(t);			\
	add	%r0,RE(t),%r15;			\
	add	RT(t),RT(t),%r6;		\
	add	%r0,%r0,W(t);			\
	add	RT(t),RT(t),%r0

#define STEPD2(t)				\
	and	%r6,RB(t),RC(t);		\
	and	%r0,RB(t),RD(t);		\
	rotlwi	RT(t),RA(t),5;			\
	rotlwi	RB(t),RB(t),30;			\
	or	%r6,%r6,%r0;			\
	and	%r0,RC(t),RD(t);		\
	or	%r6,%r6,%r0;			\
	add	%r0,RE(t),%r15;			\
	add	RT(t),RT(t),%r6;		\
	add	%r0,%r0,W(t);			\
	add	RT(t),RT(t),%r0

#define LOADW(t)				\
	lwz	W(t),(t)*4(%r4)

#define UPDATEW(t)				\
	xor	%r0,W((t)-3),W((t)-8);		\
	xor	W(t),W((t)-16),W((t)-14);	\
	xor	W(t),W(t),%r0;			\
	rotlwi	W(t),W(t),1

#define STEP0LD4(t)				\
	STEPD0(t);   LOADW((t)+4);		\
	STEPD0((t)+1); LOADW((t)+5);		\
	STEPD0((t)+2); LOADW((t)+6);		\
	STEPD0((t)+3); LOADW((t)+7)

#define STEPUP4(t, fn)				\
	STEP##fn(t);   UPDATEW((t)+4);		\
	STEP##fn((t)+1); UPDATEW((t)+5);	\
	STEP##fn((t)+2); UPDATEW((t)+6);	\
	STEP##fn((t)+3); UPDATEW((t)+7)

#define STEPUP20(t, fn)				\
	STEPUP4(t, fn);				\
	STEPUP4((t)+4, fn);			\
	STEPUP4((t)+8, fn);			\
	STEPUP4((t)+12, fn);			\
	STEPUP4((t)+16, fn)

	.globl	sha1_core
sha1_core:
	stwu	%r1,-FS(%r1)
	stw	%r15,FS-68(%r1)
	stw	%r16,FS-64(%r1)
	stw	%r17,FS-60(%r1)
	stw	%r18,FS-56(%r1)
	stw	%r19,FS-52(%r1)
	stw	%r20,FS-48(%r1)
	stw	%r21,FS-44(%r1)
	stw	%r22,FS-40(%r1)
	stw	%r23,FS-36(%r1)
	stw	%r24,FS-32(%r1)
	stw	%r25,FS-28(%r1)
	stw	%r26,FS-24(%r1)
	stw	%r27,FS-20(%r1)
	stw	%r28,FS-16(%r1)
	stw	%r29,FS-12(%r1)
	stw	%r30,FS-8(%r1)
	stw	%r31,FS-4(%r1)

	/* Load up A - E */
	lwz	RA(0),0(%r3)	/* A */
	lwz	RB(0),4(%r3)	/* B */
	lwz	RC(0),8(%r3)	/* C */
	lwz	RD(0),12(%r3)	/* D */
	lwz	RE(0),16(%r3)	/* E */

	mtctr	%r5

1:	LOADW(0)
	LOADW(1)
	LOADW(2)
	LOADW(3)

	lis	%r15,0x5a82	/* K0-19 */
	ori	%r15,%r15,0x7999
	STEP0LD4(0)
	STEP0LD4(4)
	STEP0LD4(8)
	STEPUP4(12, D0)
	STEPUP4(16, D0)

	lis	%r15,0x6ed9	/* K20-39 */
	ori	%r15,%r15,0xeba1
	STEPUP20(20, D1)

	lis	%r15,0x8f1b	/* K40-59 */
	ori	%r15,%r15,0xbcdc
	STEPUP20(40, D2)

	lis	%r15,0xca62	/* K60-79 */
	ori	%r15,%r15,0xc1d6
	STEPUP4(60, D1)
	STEPUP4(64, D1)
	STEPUP4(68, D1)
	STEPUP4(72, D1)
	STEPD1(76)
	STEPD1(77)
	STEPD1(78)
	STEPD1(79)

	lwz	%r20,16(%r3)
	lwz	%r19,12(%r3)
	lwz	%r18,8(%r3)
	lwz	%r17,4(%r3)
	lwz	%r16,0(%r3)
	add	%r20,RE(80),%r20
	add	RD(0),RD(80),%r19
	add	RC(0),RC(80),%r18
	add	RB(0),RB(80),%r17
	add	RA(0),RA(80),%r16
	mr	RE(0),%r20
	stw	RA(0),0(%r3)
	stw	RB(0),4(%r3)
	stw	RC(0),8(%r3)
	stw	RD(0),12(%r3)
	stw	RE(0),16(%r3)

	addi	%r4,%r4,64
	bdnz	1b

	lwz	%r15,FS-68(%r1)
	lwz	%r16,FS-64(%r1)
	lwz	%r17,FS-60(%r1)
	lwz	%r18,FS-56(%r1)
	lwz	%r19,FS-52(%r1)
	lwz	%r20,FS-48(%r1)
	lwz	%r21,FS-44(%r1)
	lwz	%r22,FS-40(%r1)
	lwz	%r23,FS-36(%r1)
	lwz	%r24,FS-32(%r1)
	lwz	%r25,FS-28(%r1)
	lwz	%r26,FS-24(%r1)
	lwz	%r27,FS-20(%r1)
	lwz	%r28,FS-16(%r1)
	lwz	%r29,FS-12(%r1)
	lwz	%r30,FS-8(%r1)
	lwz	%r31,FS-4(%r1)
	addi	%r1,%r1,FS
	blr
