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

@@
expression E;
@@
- has_sha1_file(
+ repo_has_sha1_file(the_repository,
  E)

@@
expression E;
expression F;
@@
- has_sha1_file_with_flags(
+ repo_has_sha1_file_with_flags(the_repository,
  E)

@@
expression E;
@@
- has_object_file(
+ repo_has_object_file(the_repository,
  E)

@@
expression E;
expression F;
@@
- has_object_file_with_flags(
+ repo_has_object_file_with_flags(the_repository,
  E)

@@
expression E;
expression F;
expression G;
@@
- parse_commit_internal(
+ repo_parse_commit_internal(the_repository,
  E, F, G)

@@
expression E;
expression F;
@@
- parse_commit_gently(
+ repo_parse_commit_gently(the_repository,
  E, F)

@@
expression E;
@@
- parse_commit(
+ repo_parse_commit(the_repository,
  E)
