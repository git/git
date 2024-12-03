/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"
#include "tree.h"

#include "basics.h"

struct tree_node *tree_search(struct tree_node *tree,
			      void *key,
			      int (*compare)(const void *, const void *))
{
	int res;
	if (!tree)
		return NULL;
	res = compare(key, tree->key);
	if (res < 0)
		return tree_search(tree->left, key, compare);
	else if (res > 0)
		return tree_search(tree->right, key, compare);
	return tree;
}

struct tree_node *tree_insert(struct tree_node **rootp,
			      void *key,
			      int (*compare)(const void *, const void *))
{
	int res;

	if (!*rootp) {
		struct tree_node *n;

		REFTABLE_CALLOC_ARRAY(n, 1);
		if (!n)
			return NULL;

		n->key = key;
		*rootp = n;
		return *rootp;
	}

	res = compare(key, (*rootp)->key);
	if (res < 0)
		return tree_insert(&(*rootp)->left, key, compare);
	else if (res > 0)
		return tree_insert(&(*rootp)->right, key, compare);
	return *rootp;
}

void infix_walk(struct tree_node *t, void (*action)(void *arg, void *key),
		void *arg)
{
	if (t->left)
		infix_walk(t->left, action, arg);
	action(arg, t->key);
	if (t->right)
		infix_walk(t->right, action, arg);
}

void tree_free(struct tree_node *t)
{
	if (!t)
		return;
	if (t->left)
		tree_free(t->left);
	if (t->right)
		tree_free(t->right);
	reftable_free(t);
}
