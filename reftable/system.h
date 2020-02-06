/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef SYSTEM_H
#define SYSTEM_H

#include "config.h"

#ifndef REFTABLE_STANDALONE

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

#define ARRAY_SIZE(a) sizeof((a)) / sizeof((a)[0])
#define FREE_AND_NULL(x)    \
	do {                \
		free(x);    \
		(x) = NULL; \
	} while (0)
#define QSORT(arr, n, cmp) qsort(arr, n, sizeof(arr[0]), cmp)
#define SWAP(a, b)                              \
	{                                       \
		char tmp[sizeof(a)];            \
		assert(sizeof(a) == sizeof(b)); \
		memcpy(&tmp[0], &a, sizeof(a)); \
		memcpy(&a, &b, sizeof(a));      \
		memcpy(&b, &tmp[0], sizeof(a)); \
	}
#endif

int uncompress_return_consumed(Bytef *dest, uLongf *destLen,
			       const Bytef *source, uLong *sourceLen);

#endif
