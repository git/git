#ifndef HASH_H
#define HASH_H

#if defined(SHA1_PPC)
#include "ppc/sha1.h"
#elif defined(SHA1_APPLE)
#include <CommonCrypto/CommonDigest.h>
#elif defined(SHA1_OPENSSL)
#include <openssl/sha.h>
#elif defined(SHA1_DC)
#include "sha1dc_git.h"
#else /* SHA1_BLK */
#include "block-sha1/sha1.h"
#endif

#endif
