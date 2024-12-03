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

/*
 * Search the tree for the node matching the given key using `compare` as
 * comparison function. Returns the node whose key matches or `NULL` in case
 * the key does not exist in the tree.
 */
struct tree_node *tree_search(struct tree_node *tree,
			      void *key,
			      int (*compare)(const void *, const void *));

/*
 * Insert a node into the tree. Returns the newly inserted node if the key does
 * not yet exist. Otherwise it returns the preexisting node. Returns `NULL`
 * when allocating the new node fails.
 */
struct tree_node *tree_insert(struct tree_node **rootp,
			      void *key,
			      int (*compare)(const void *, const void *));

/* performs an infix walk of the tree. */
void infix_walk(struct tree_node *t, void (*action)(void *arg, void *key),
		void *arg);

/*
 * deallocates the tree nodes recursively. Keys should be deallocated separately
 * by walking over the tree. */
void tree_free(struct tree_node *t);

#endif
