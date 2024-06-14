#ifndef HEX_H
#define HEX_H

#include "hash.h"
#include "hex-ll.h"

/*
 * Try to read a hash (specified by the_hash_algo) in hexadecimal
 * format from the 40 (or whatever length the hash algorithm uses)
 * characters starting at hex.  Write the 20-byte (or the length of
 * the hash) result to hash in binary form.
 * Return 0 on success.  Reading stops if a NUL is encountered in the
 * input, so it is safe to pass this function an arbitrary
 * null-terminated string.
 */
int get_oid_hex_algop(const char *hex, struct object_id *oid, const struct git_hash_algo *algop);

/*
 * Convert a binary hash in "unsigned char []" or an object name in
 * "struct object_id *" to its hex equivalent. The `_r` variant is reentrant,
 * and writes the NUL-terminated output to the buffer `out`, which must be at
 * least `GIT_MAX_HEXSZ + 1` bytes, and returns a pointer to out for
 * convenience.
 *
 * The non-`_r` variant returns a static buffer, but uses a ring of 4
 * buffers, making it safe to make multiple calls for a single statement, like:
 *
 *   printf("%s -> %s", hash_to_hex(one), hash_to_hex(two));
 *   printf("%s -> %s", oid_to_hex(one), oid_to_hex(two));
 */
char *hash_to_hex_algop_r(char *buffer, const unsigned char *hash, const struct git_hash_algo *);
char *oid_to_hex_r(char *out, const struct object_id *oid);
char *hash_to_hex_algop(const unsigned char *hash, const struct git_hash_algo *);	/* static buffer result! */
char *oid_to_hex(const struct object_id *oid);						/* same static buffer */

/*
 * Parse a 40-character hexadecimal object ID starting from hex, updating the
 * pointer specified by end when parsing stops.  The resulting object ID is
 * stored in oid.  Returns 0 on success.  Parsing will stop on the first NUL or
 * other invalid character.  end is only updated on success; otherwise, it is
 * unmodified.
 */
int parse_oid_hex_algop(const char *hex, struct object_id *oid, const char **end,
			const struct git_hash_algo *algo);

/*
 * These functions work like get_oid_hex and parse_oid_hex, but they will parse
 * a hex value for any algorithm. The algorithm is detected based on the length
 * and the algorithm in use is returned. If this is not a hex object ID in any
 * algorithm, returns GIT_HASH_UNKNOWN.
 */
int get_oid_hex_any(const char *hex, struct object_id *oid);
int parse_oid_hex_any(const char *hex, struct object_id *oid, const char **end);

#ifdef USE_THE_REPOSITORY_VARIABLE

/* Like get_oid_hex_algop, but for `the_hash_algo`. */
int get_hash_hex(const char *hex, unsigned char *hash);
int get_oid_hex(const char *hex, struct object_id *oid);

/* Like parse_oid_hex_algop, but uses `the_hash_algo`. */
int parse_oid_hex(const char *hex, struct object_id *oid, const char **end);

/*
 * Same as `hash_to_hex_algop()`, but uses `the_hash_algo`.
 */
char *hash_to_hex(const unsigned char *hash);

#endif /* USE_THE_REPOSITORY_VARIABLE */
#endif /* HEX_H */
