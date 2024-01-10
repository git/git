/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef CONSTANTS_H
#define CONSTANTS_H

#define BLOCK_TYPE_LOG 'g'
#define BLOCK_TYPE_INDEX 'i'
#define BLOCK_TYPE_REF 'r'
#define BLOCK_TYPE_OBJ 'o'
#define BLOCK_TYPE_ANY 0

#define MAX_RESTARTS ((1 << 16) - 1)
#define DEFAULT_BLOCK_SIZE 4096

#endif
