// This file is used for the ongoing refactoring of
// bringing the index or repository struct in all of
// our code base.

@@
expression E;
expression F;
expression G;
@@
- read_object_file(
+ repo_read_object_file(the_repository,
  E, F, G)
