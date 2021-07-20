/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"
#include "test_framework.h"

#include "basics.h"

void set_test_hash(uint8_t *p, int i)
{
	memset(p, (uint8_t)i, hash_size(GIT_SHA1_FORMAT_ID));
}

ssize_t strbuf_add_void(void *b, const void *data, size_t sz)
{
	strbuf_add(b, data, sz);
	return sz;
}
