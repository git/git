/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef SYSTEM_H
#define SYSTEM_H

/* This header glues the reftable library to the rest of Git */

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "lockfile.h"
#include "tempfile.h"
#include "hash.h" /* hash ID, sizes.*/
#include "dir.h" /* remove_dir_recursively, for tests.*/

int hash_size(uint32_t id);

#endif
