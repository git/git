/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef TREE_H
#define TREE_H

struct tree_node {
	void *key;
	struct tree_node *left, *right;
};

struct tree_node *tree_search(void *key, struct tree_node **rootp,
			      int (*compare)(const void *, const void *),
			      int insert);
void infix_walk(struct tree_node *t, void (*action)(void *arg, void *key),
		void *arg);
void tree_free(struct tree_node *t);

#endif
