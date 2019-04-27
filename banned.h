#ifndef BANNED_H
#define BANNED_H

/*
 * This header lists functions that have been banned from our code base,
 * because they're too easy to misuse (and even if used correctly,
 * complicate audits). Including this header turns them into compile-time
 * errors.
 */

#define BANNED(func) sorry_##func##_is_a_banned_function

#undef strcpy
#define strcpy(x,y) BANNED(strcpy)
#undef strcat
#define strcat(x,y) BANNED(strcat)
#undef strncpy
#define strncpy(x,y,n) BANNED(strncpy)
#undef strncat
#define strncat(x,y,n) BANNED(strncat)

#undef sprintf
#undef vsprintf
#ifdef HAVE_VARIADIC_MACROS
#define sprintf(...) BANNED(sprintf)
#define vsprintf(...) BANNED(vsprintf)
#else
#define sprintf(buf,fmt,arg) BANNED(sprintf)
#define vsprintf(buf,fmt,arg) BANNED(sprintf)
#endif

#endif /* BANNED_H */
