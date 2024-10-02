/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "test-lib.h"
#include "reftable/tree.h"

static int t_compare(const void *a, const void *b)
{
	return (char *)a - (char *)b;
}

struct curry {
	void **arr;
	size_t len;
};

static void store(void *arg, void *key)
{
	struct curry *c = arg;
	c->arr[c->len++] = key;
}

static void t_tree_search(void)
{
	struct tree_node *root = NULL;
	void *values[11] = { 0 };
	struct tree_node *nodes[11] = { 0 };
	size_t i = 1;

	/*
	 * Pseudo-randomly insert the pointers for elements between
	 * values[1] and values[10] (inclusive) in the tree.
	 */
	do {
		nodes[i] = tree_insert(&root, &values[i], &t_compare);
		check(nodes[i] != NULL);
		i = (i * 7) % 11;
	} while (i != 1);

	for (i = 1; i < ARRAY_SIZE(nodes); i++) {
		check_pointer_eq(&values[i], nodes[i]->key);
		check_pointer_eq(nodes[i], tree_search(root, &values[i], &t_compare));
	}

	check(!tree_search(root, values, t_compare));
	tree_free(root);
}

static void t_infix_walk(void)
{
	struct tree_node *root = NULL;
	void *values[11] = { 0 };
	void *out[11] = { 0 };
	struct curry c = {
		.arr = (void **) &out,
	};
	size_t i = 1;
	size_t count = 0;

	do {
		struct tree_node *node = tree_insert(&root, &values[i], t_compare);
		check(node != NULL);
		i = (i * 7) % 11;
		count++;
	} while (i != 1);

	infix_walk(root, &store, &c);
	for (i = 1; i < ARRAY_SIZE(values); i++)
		check_pointer_eq(&values[i], out[i - 1]);
	check(!out[i - 1]);
	check_int(c.len, ==, count);
	tree_free(root);
}

int cmd_main(int argc UNUSED, const char *argv[] UNUSED)
{
	TEST(t_tree_search(), "tree_search works");
	TEST(t_infix_walk(), "infix_walk works");

	return test_done();
}
