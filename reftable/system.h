/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef SYSTEM_H
#define SYSTEM_H

#if 1 /* REFTABLE_IN_GITCORE */

#include "git-compat-util.h"
#include <zlib.h>

#else

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include "compat.h"

#endif /* REFTABLE_IN_GITCORE */

#define SHA1_ID 0x73686131
#define SHA256_ID 0x73323536
#define SHA1_SIZE 20
#define SHA256_SIZE 32

typedef uint8_t byte;
typedef int bool;

/* This is uncompress2, which is only available in zlib as of 2017.
 *
 * TODO: in git-core, this should fallback to uncompress2 if it is available.
 */
int uncompress_return_consumed(Bytef *dest, uLongf *destLen,
			       const Bytef *source, uLong *sourceLen);
int hash_size(uint32_t id);

#endif
