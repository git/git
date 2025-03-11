#ifndef LIB_OID_H
#define LIB_OID_H

#include "hash.h"

/*
 * Convert arbitrary hex string to object_id.
 *
 * For example, passing "abc12" will generate
 * "abc1200000000000000000000000000000000000" hex of length 40 for SHA-1 and
 * create object_id with that.
 * WARNING: passing a string of length more than the hexsz of respective hash
 * algo is not allowed. The hash algo is decided based on GIT_TEST_DEFAULT_HASH
 * environment variable.
 */

void cl_parse_any_oid (const char *s, struct object_id *oid);
/*
 * Returns one of GIT_HASH_{SHA1, SHA256, UNKNOWN} based on the value of
 * GIT_TEST_DEFAULT_HASH environment variable. The fallback value in the
 * absence of GIT_TEST_DEFAULT_HASH is GIT_HASH_SHA1. It also uses
 * cl_assert(algo != GIT_HASH_UNKNOWN) before returning to verify if the
 * GIT_TEST_DEFAULT_HASH's value is valid or not.
 */

int cl_setup_hash_algo(void);

#endif /* LIB_OID_H */
