/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef TREE_H
#define TREE_H

/* tree_node is a generic binary search tree. */
struct tree_node {
	void *key;
	struct tree_node *left, *right;
};

/* looks for `key` in `rootp` using `compare` as comparison function. If insert
 * is set, insert the key if it's not found. Else, return NULL.
 */
struct tree_node *tree_search(void *key, struct tree_node **rootp,
			      int (*compare)(const void *, const void *),
			      int insert);

/* performs an infix walk of the tree. */
void infix_walk(struct tree_node *t, void (*action)(void *arg, void *key),
		void *arg);

/*
 * deallocates the tree nodes recursively. Keys should be deallocated separately
 * by walking over the tree. */
void tree_free(struct tree_node *t);

#endif
