#ifndef LIB_OID_H
#define LIB_OID_H

#include "hash.h"

/*
 * Convert arbitrary hex string to object_id.
 * For example, passing "abc12" will generate
 * "abc1200000000000000000000000000000000000" hex of length 40 for SHA-1 and
 * create object_id with that.
 * WARNING: passing a string of length more than the hexsz of respective hash
 * algo is not allowed. The hash algo is decided based on GIT_TEST_DEFAULT_HASH
 * environment variable.
 */
int get_oid_arbitrary_hex(const char *s, struct object_id *oid);

#endif /* LIB_OID_H */
