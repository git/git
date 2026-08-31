#ifndef BANNED_DIE_H
#define BANNED_DIE_H

#include "banned.h"

/*
 * This header lists functions that must not be used by low-level APIs
 * because they can cause Git to terminate.
 */

#undef die
#define die BANNED(die)

#endif /* BANNED_DIE_H */
