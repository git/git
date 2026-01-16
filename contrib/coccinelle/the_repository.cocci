// Fully migrated "the_repository" additions
@@
@@
(
// TODO: remove the rules below and the macros from tree.h after the
// next Git release.
- parse_tree
+ repo_parse_tree
|
- parse_tree_gently
+ repo_parse_tree_gently
|
- parse_tree_indirect
+ repo_parse_tree_indirect
)
  (
+ the_repository,
  ...)
