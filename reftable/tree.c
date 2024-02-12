/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"
#include "tree.h"

#include "basics.h"

struct tree_node *tree_search(void *key, struct tree_node **rootp,
			      int (*compare)(const void *, const void *),
			      int insert)
{
	int res;
	if (!*rootp) {
		if (!insert) {
			return NULL;
		} else {
			struct tree_node *n =
				reftable_calloc(sizeof(struct tree_node));
			n->key = key;
			*rootp = n;
			return *rootp;
		}
	}

	res = compare(key, (*rootp)->key);
	if (res < 0)
		return tree_search(key, &(*rootp)->left, compare, insert);
	else if (res > 0)
		return tree_search(key, &(*rootp)->right, compare, insert);
	return *rootp;
}

void infix_walk(struct tree_node *t, void (*action)(void *arg, void *key),
		void *arg)
{
	if (t->left) {
		infix_walk(t->left, action, arg);
	}
	action(arg, t->key);
	if (t->right) {
		infix_walk(t->right, action, arg);
	}
}

void tree_free(struct tree_node *t)
{
	if (!t) {
		return;
	}
	if (t->left) {
		tree_free(t->left);
	}
	if (t->right) {
		tree_free(t->right);
	}
	reftable_free(t);
}
