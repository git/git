#ifndef BANNED_DIE_H
#define BANNED_DIE_H

#include "banned.h"

/*
 * This header lists functions that must not be used by low-level APIs
 * because they can cause Git to terminate.
 */

#undef die
#define die BANNED(die)

#undef xsnprintf
#define xsnprintf(...) BANNED(xsnprintf)

#undef xstrdup
#define xstrdup(str) BANNED(xstrdup)

#undef xcalloc
#define xcalloc(nmemb, size) BANNED(xcalloc)

#undef xstrfmt
#define xstrfmt(...) BANNED(xstrfmt)

#undef ALLOC_ARRAY
#define ALLOC_ARRAY(x, alloc) BANNED(ALLOC_ARRAY)

#undef ALLOC_GROW
#define ALLOC_GROW(x, nr, alloc) BANNED(ALLOC_GROW)

#endif /* BANNED_DIE_H */
