/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "unit-test.h"
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

void test_reftable_tree__tree_search(void)
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
		cl_assert(nodes[i] != NULL);
		i = (i * 7) % 11;
	} while (i != 1);

	for (i = 1; i < ARRAY_SIZE(nodes); i++) {
		cl_assert_equal_p(&values[i], nodes[i]->key);
		cl_assert_equal_p(nodes[i], tree_search(root, &values[i], &t_compare));
	}

	cl_assert(tree_search(root, values, t_compare) == NULL);
	tree_free(root);
}

void test_reftable_tree__infix_walk(void)
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
		cl_assert(node != NULL);
		i = (i * 7) % 11;
		count++;
	} while (i != 1);

	infix_walk(root, &store, &c);
	for (i = 1; i < ARRAY_SIZE(values); i++)
		cl_assert_equal_p(&values[i], out[i - 1]);
	cl_assert(out[i - 1] == NULL);
	cl_assert_equal_i(c.len, count);
	tree_free(root);
}
