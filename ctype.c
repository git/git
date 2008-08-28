/*
 * Sane locale-independent, ASCII ctype.
 *
 * No surprises, and works with signed and unsigned chars.
 */
#include "cache.h"

/* Just so that no insane platform contaminate namespace with these symbols */
#undef SS
#undef AA
#undef DD
#undef GS

#define SS GIT_SPACE
#define AA GIT_ALPHA
#define DD GIT_DIGIT
#define GS GIT_SPECIAL  /* \0, *, ?, [, \\ */

unsigned char sane_ctype[256] = {
	GS,  0,  0,  0,  0,  0,  0,  0,  0, SS, SS,  0,  0, SS,  0,  0,		/* 0-15 */
	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,		/* 16-15 */
	SS,  0,  0,  0,  0,  0,  0,  0,  0,  0, GS,  0,  0,  0,  0,  0,		/* 32-15 */
	DD, DD, DD, DD, DD, DD, DD, DD, DD, DD,  0,  0,  0,  0,  0, GS,		/* 48-15 */
	 0, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA,		/* 64-15 */
	AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, GS, GS,  0,  0,  0,		/* 80-15 */
	 0, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA,		/* 96-15 */
	AA, AA, AA, AA, AA, AA, AA, AA, AA, AA, AA,  0,  0,  0,  0,  0,		/* 112-15 */
	/* Nothing in the 128.. range */
};
