@@
expression c;
@@
- &c->maybe_tree->object.oid
+ get_commit_tree_oid(c)

@@
expression c;
@@
- c->maybe_tree->object.oid.hash
+ get_commit_tree_oid(c)->hash

@@
expression c;
@@
- c->maybe_tree
+ get_commit_tree(c)

@@
expression c;
expression s;
@@
- get_commit_tree(c) = s
+ c->maybe_tree = s

@@
expression c;
@@
- return get_commit_tree(c);
+ return c->maybe_tree;
