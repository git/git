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
identifier f !~ "^set_commit_tree$";
expression c;
expression s;
@@
  f(...) {<...
- c->maybe_tree = s
+ set_commit_tree(c, s)
  ...>}

// These excluded functions must access c->maybe_tree direcly.
// Note that if c->maybe_tree is written somewhere outside of these
// functions, then the recommended transformation will be bogus with
// get_commit_tree() on the LHS.
@@
identifier f !~ "^(get_commit_tree|get_commit_tree_in_graph_one|load_tree_for_commit|set_commit_tree)$";
expression c;
@@
  f(...) {<...
- c->maybe_tree
+ get_commit_tree(c)
  ...>}
