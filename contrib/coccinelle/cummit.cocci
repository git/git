@@
expression c;
@@
- &c->maybe_tree->object.oid
+ get_cummit_tree_oid(c)

@@
expression c;
@@
- c->maybe_tree->object.oid.hash
+ get_cummit_tree_oid(c)->hash

@@
identifier f !~ "^set_cummit_tree$";
expression c;
expression s;
@@
  f(...) {<...
- c->maybe_tree = s
+ set_cummit_tree(c, s)
  ...>}

// These excluded functions must access c->maybe_tree directly.
// Note that if c->maybe_tree is written somewhere outside of these
// functions, then the recommended transformation will be bogus with
// repo_get_cummit_tree() on the LHS.
@@
identifier f !~ "^(repo_get_cummit_tree|get_cummit_tree_in_graph_one|load_tree_for_cummit|set_cummit_tree)$";
expression c;
@@
  f(...) {<...
- c->maybe_tree
+ repo_get_cummit_tree(specify_the_right_repo_here, c)
  ...>}

@@
struct cummit *c;
expression E;
@@
(
- c->generation = E;
+ cummit_graph_data_at(c)->generation = E;
|
- c->graph_pos = E;
+ cummit_graph_data_at(c)->graph_pos = E;
|
- c->generation
+ cummit_graph_generation(c)
|
- c->graph_pos
+ cummit_graph_position(c)
)
