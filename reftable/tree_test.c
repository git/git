/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"
#include "tree.h"

#include "basics.h"
#include "record.h"
#include "test_framework.h"
#include "reftable-tests.h"

static int test_compare(const void *a, const void *b)
{
	return (char *)a - (char *)b;
}

struct curry {
	void *last;
};

static void check_increasing(void *arg, void *key)
{
	struct curry *c = arg;
	if (c->last) {
		EXPECT(test_compare(c->last, key) < 0);
	}
	c->last = key;
}

static void test_tree(void)
{
	struct tree_node *root = NULL;

	void *values[11] = { NULL };
	struct tree_node *nodes[11] = { NULL };
	int i = 1;
	struct curry c = { NULL };
	do {
		nodes[i] = tree_search(values + i, &root, &test_compare, 1);
		i = (i * 7) % 11;
	} while (i != 1);

	for (i = 1; i < ARRAY_SIZE(nodes); i++) {
		EXPECT(values + i == nodes[i]->key);
		EXPECT(nodes[i] ==
		       tree_search(values + i, &root, &test_compare, 0));
	}

	infix_walk(root, check_increasing, &c);
	tree_free(root);
}

int tree_test_main(int argc, const char *argv[])
{
	RUN_TEST(test_tree);
	return 0;
}
