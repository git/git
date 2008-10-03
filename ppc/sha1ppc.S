/*
 * SHA-1 implementation for PowerPC.
 *
 * Copyright (C) 2005 Paul Mackerras <paulus@samba.org>
 */

/*
 * PowerPC calling convention:
 * %r0 - volatile temp
 * %r1 - stack pointer.
 * %r2 - reserved
 * %r3-%r12 - Incoming arguments & return values; volatile.
 * %r13-%r31 - Callee-save registers
 * %lr - Return address, volatile
 * %ctr - volatile
 *
 * Register usage in this routine:
 * %r0 - temp
 * %r3 - argument (pointer to 5 words of SHA state)
 * %r4 - argument (pointer to data to hash)
 * %r5 - Constant K in SHA round (initially number of blocks to hash)
 * %r6-%r10 - Working copies of SHA variables A..E (actually E..A order)
 * %r11-%r26 - Data being hashed W[].
 * %r27-%r31 - Previous copies of A..E, for final add back.
 * %ctr - loop count
 */


/*
 * We roll the registers for A, B, C, D, E around on each
 * iteration; E on iteration t is D on iteration t+1, and so on.
 * We use registers 6 - 10 for this.  (Registers 27 - 31 hold
 * the previous values.)
 */
#define RA(t)	(((t)+4)%5+6)
#define RB(t)	(((t)+3)%5+6)
#define RC(t)	(((t)+2)%5+6)
#define RD(t)	(((t)+1)%5+6)
#define RE(t)	(((t)+0)%5+6)

/* We use registers 11 - 26 for the W values */
#define W(t)	((t)%16+11)

/* Register 5 is used for the constant k */

/*
 * The basic SHA-1 round function is:
 * E += ROTL(A,5) + F(B,C,D) + W[i] + K;  B = ROTL(B,30)
 * Then the variables are renamed: (A,B,C,D,E) = (E,A,B,C,D).
 *
 * Every 20 rounds, the function F() and the constant K changes:
 * - 20 rounds of f0(b,c,d) = "bit wise b ? c : d" =  (^b & d) + (b & c)
 * - 20 rounds of f1(b,c,d) = b^c^d = (b^d)^c
 * - 20 rounds of f2(b,c,d) = majority(b,c,d) = (b&d) + ((b^d)&c)
 * - 20 more rounds of f1(b,c,d)
 *
 * These are all scheduled for near-optimal performance on a G4.
 * The G4 is a 3-issue out-of-order machine with 3 ALUs, but it can only
 * *consider* starting the oldest 3 instructions per cycle.  So to get
 * maximum performance out of it, you have to treat it as an in-order
 * machine.  Which means interleaving the computation round t with the
 * computation of W[t+4].
 *
 * The first 16 rounds use W values loaded directly from memory, while the
 * remaining 64 use values computed from those first 16.  We preload
 * 4 values before starting, so there are three kinds of rounds:
 * - The first 12 (all f0) also load the W values from memory.
 * - The next 64 compute W(i+4) in parallel. 8*f0, 20*f1, 20*f2, 16*f1.
 * - The last 4 (all f1) do not do anything with W.
 *
 * Therefore, we have 6 different round functions:
 * STEPD0_LOAD(t,s) - Perform round t and load W(s).  s < 16
 * STEPD0_UPDATE(t,s) - Perform round t and compute W(s).  s >= 16.
 * STEPD1_UPDATE(t,s)
 * STEPD2_UPDATE(t,s)
 * STEPD1(t) - Perform round t with no load or update.
 *
 * The G5 is more fully out-of-order, and can find the parallelism
 * by itself.  The big limit is that it has a 2-cycle ALU latency, so
 * even though it's 2-way, the code has to be scheduled as if it's
 * 4-way, which can be a limit.  To help it, we try to schedule the
 * read of RA(t) as late as possible so it doesn't stall waiting for
 * the previous round's RE(t-1), and we try to rotate RB(t) as early
 * as possible while reading RC(t) (= RB(t-1)) as late as possible.
 */

/* the initial loads. */
#define LOADW(s) \
	lwz	W(s),(s)*4(%r4)

/*
 * Perform a step with F0, and load W(s).  Uses W(s) as a temporary
 * before loading it.
 * This is actually 10 instructions, which is an awkward fit.
 * It can execute grouped as listed, or delayed one instruction.
 * (If delayed two instructions, there is a stall before the start of the
 * second line.)  Thus, two iterations take 7 cycles, 3.5 cycles per round.
 */
#define STEPD0_LOAD(t,s) \
add RE(t),RE(t),W(t); andc   %r0,RD(t),RB(t);  and    W(s),RC(t),RB(t); \
add RE(t),RE(t),%r0;  rotlwi %r0,RA(t),5;      rotlwi RB(t),RB(t),30;   \
add RE(t),RE(t),W(s); add    %r0,%r0,%r5;      lwz    W(s),(s)*4(%r4);  \
add RE(t),RE(t),%r0

/*
 * This is likewise awkward, 13 instructions.  However, it can also
 * execute starting with 2 out of 3 possible moduli, so it does 2 rounds
 * in 9 cycles, 4.5 cycles/round.
 */
#define STEPD0_UPDATE(t,s,loadk...) \
add RE(t),RE(t),W(t); andc   %r0,RD(t),RB(t); xor    W(s),W((s)-16),W((s)-3); \
add RE(t),RE(t),%r0;  and    %r0,RC(t),RB(t); xor    W(s),W(s),W((s)-8);      \
add RE(t),RE(t),%r0;  rotlwi %r0,RA(t),5;     xor    W(s),W(s),W((s)-14);     \
add RE(t),RE(t),%r5;  loadk; rotlwi RB(t),RB(t),30;  rotlwi W(s),W(s),1;     \
add RE(t),RE(t),%r0

/* Nicely optimal.  Conveniently, also the most common. */
#define STEPD1_UPDATE(t,s,loadk...) \
add RE(t),RE(t),W(t); xor    %r0,RD(t),RB(t); xor    W(s),W((s)-16),W((s)-3); \
add RE(t),RE(t),%r5;  loadk; xor %r0,%r0,RC(t);  xor W(s),W(s),W((s)-8);      \
add RE(t),RE(t),%r0;  rotlwi %r0,RA(t),5;     xor    W(s),W(s),W((s)-14);     \
add RE(t),RE(t),%r0;  rotlwi RB(t),RB(t),30;  rotlwi W(s),W(s),1

/*
 * The naked version, no UPDATE, for the last 4 rounds.  3 cycles per.
 * We could use W(s) as a temp register, but we don't need it.
 */
#define STEPD1(t) \
                        add   RE(t),RE(t),W(t); xor    %r0,RD(t),RB(t); \
rotlwi RB(t),RB(t),30;  add   RE(t),RE(t),%r5;  xor    %r0,%r0,RC(t);   \
add    RE(t),RE(t),%r0; rotlwi %r0,RA(t),5;     /* spare slot */        \
add    RE(t),RE(t),%r0

/*
 * 14 instructions, 5 cycles per.  The majority function is a bit
 * awkward to compute.  This can execute with a 1-instruction delay,
 * but it causes a 2-instruction delay, which triggers a stall.
 */
#define STEPD2_UPDATE(t,s,loadk...) \
add RE(t),RE(t),W(t); and    %r0,RD(t),RB(t); xor    W(s),W((s)-16),W((s)-3); \
add RE(t),RE(t),%r0;  xor    %r0,RD(t),RB(t); xor    W(s),W(s),W((s)-8);      \
add RE(t),RE(t),%r5;  loadk; and %r0,%r0,RC(t);  xor W(s),W(s),W((s)-14);     \
add RE(t),RE(t),%r0;  rotlwi %r0,RA(t),5;     rotlwi W(s),W(s),1;             \
add RE(t),RE(t),%r0;  rotlwi RB(t),RB(t),30

#define STEP0_LOAD4(t,s)		\
	STEPD0_LOAD(t,s);		\
	STEPD0_LOAD((t+1),(s)+1);	\
	STEPD0_LOAD((t)+2,(s)+2);	\
	STEPD0_LOAD((t)+3,(s)+3)

#define STEPUP4(fn, t, s, loadk...)		\
	STEP##fn##_UPDATE(t,s,);		\
	STEP##fn##_UPDATE((t)+1,(s)+1,);	\
	STEP##fn##_UPDATE((t)+2,(s)+2,);	\
	STEP##fn##_UPDATE((t)+3,(s)+3,loadk)

#define STEPUP20(fn, t, s, loadk...)	\
	STEPUP4(fn, t, s,);		\
	STEPUP4(fn, (t)+4, (s)+4,);	\
	STEPUP4(fn, (t)+8, (s)+8,);	\
	STEPUP4(fn, (t)+12, (s)+12,);	\
	STEPUP4(fn, (t)+16, (s)+16, loadk)

	.globl	ppc_sha1_core
ppc_sha1_core:
	stwu	%r1,-80(%r1)
	stmw	%r13,4(%r1)

	/* Load up A - E */
	lmw	%r27,0(%r3)

	mtctr	%r5

1:
	LOADW(0)
	lis	%r5,0x5a82
	mr	RE(0),%r31
	LOADW(1)
	mr	RD(0),%r30
	mr	RC(0),%r29
	LOADW(2)
	ori	%r5,%r5,0x7999	/* K0-19 */
	mr	RB(0),%r28
	LOADW(3)
	mr	RA(0),%r27

	STEP0_LOAD4(0, 4)
	STEP0_LOAD4(4, 8)
	STEP0_LOAD4(8, 12)
	STEPUP4(D0, 12, 16,)
	STEPUP4(D0, 16, 20, lis %r5,0x6ed9)

	ori	%r5,%r5,0xeba1	/* K20-39 */
	STEPUP20(D1, 20, 24, lis %r5,0x8f1b)

	ori	%r5,%r5,0xbcdc	/* K40-59 */
	STEPUP20(D2, 40, 44, lis %r5,0xca62)

	ori	%r5,%r5,0xc1d6	/* K60-79 */
	STEPUP4(D1, 60, 64,)
	STEPUP4(D1, 64, 68,)
	STEPUP4(D1, 68, 72,)
	STEPUP4(D1, 72, 76,)
	addi	%r4,%r4,64
	STEPD1(76)
	STEPD1(77)
	STEPD1(78)
	STEPD1(79)

	/* Add results to original values */
	add	%r31,%r31,RE(0)
	add	%r30,%r30,RD(0)
	add	%r29,%r29,RC(0)
	add	%r28,%r28,RB(0)
	add	%r27,%r27,RA(0)

	bdnz	1b

	/* Save final hash, restore registers, and return */
	stmw	%r27,0(%r3)
	lmw	%r13,4(%r1)
	addi	%r1,%r1,80
	blr
