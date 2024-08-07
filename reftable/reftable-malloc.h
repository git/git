/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_H
#define REFTABLE_H

#include <stddef.h>

/* Overrides the functions to use for memory management. */
void reftable_set_alloc(void *(*malloc)(size_t),
			void *(*realloc)(void *, size_t), void (*free)(void *));

#endif
