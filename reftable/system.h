/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef SYSTEM_H
#define SYSTEM_H

/* This header glues the reftable library to the rest of Git */

#include "git-compat-util.h"
#include "strbuf.h"
#include "hash.h" /* hash ID, sizes.*/
#include "dir.h" /* remove_dir_recursively, for tests.*/

#include <zlib.h>

#ifdef NO_UNCOMPRESS2
/*
 * This is uncompress2, which is only available in zlib >= 1.2.9
 * (released as of early 2017)
 */
int uncompress2(Bytef *dest, uLongf *destLen, const Bytef *source,
		uLong *sourceLen);
#endif

int hash_size(uint32_t id);

#endif
