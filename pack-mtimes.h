#ifndef PACK_MTIMES_H
#define PACK_MTIMES_H

#define MTIMES_SIGNATURE 0x4d544d45 /* "MTME" */
#define MTIMES_VERSION 1

struct packed_git;

/*
 * Loads the .mtimes file corresponding to "p", if any, returning zero
 * on success.
 */
int load_pack_mtimes(struct packed_git *p);

/* Returns the mtime associated with the object at position "pos" (in
 * lexicographic/index order) in pack "p".
 *
 * Note that it is a BUG() to call this function if either (a) "p" does
 * not have a corresponding .mtimes file, or (b) it does, but it hasn't
 * been loaded
 */
uint32_t nth_packed_mtime(struct packed_git *p, uint32_t pos);

#endif
