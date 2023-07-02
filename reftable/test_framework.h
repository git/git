/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include "system.h"
#include "reftable-error.h"

#define EXPECT_ERR(c)                                                  \
	if (c != 0) {                                                  \
		fflush(stderr);                                        \
		fflush(stdout);                                        \
		fprintf(stderr, "%s: %d: error == %d (%s), want 0\n",  \
			__FILE__, __LINE__, c, reftable_error_str(c)); \
		abort();                                               \
	}

#define EXPECT_STREQ(a, b)                                               \
	if (strcmp(a, b)) {                                              \
		fflush(stderr);                                          \
		fflush(stdout);                                          \
		fprintf(stderr, "%s:%d: %s (%s) != %s (%s)\n", __FILE__, \
			__LINE__, #a, a, #b, b);                         \
		abort();                                                 \
	}

#define EXPECT(c)                                                          \
	if (!(c)) {                                                        \
		fflush(stderr);                                            \
		fflush(stdout);                                            \
		fprintf(stderr, "%s: %d: failed assertion %s\n", __FILE__, \
			__LINE__, #c);                                     \
		abort();                                                   \
	}

#define RUN_TEST(f)                          \
	fprintf(stderr, "running %s\n", #f); \
	fflush(stderr);                      \
	f();

void set_test_hash(uint8_t *p, int i);

/* Like strbuf_add, but suitable for passing to reftable_new_writer
 */
ssize_t strbuf_add_void(void *b, const void *data, size_t sz);

#endif
