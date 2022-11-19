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
- has_object_file(
+ repo_has_object_file(the_repository,
  E)

@@
expression E;
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

@@
expression E;
expression F;
@@
- get_merge_bases(
+ repo_get_merge_bases(the_repository,
  E, F);

@@
expression E;
expression F;
expression G;
@@
- get_merge_bases_many(
+ repo_get_merge_bases_many(the_repository,
  E, F, G);

@@
expression E;
expression F;
expression G;
@@
- get_merge_bases_many_dirty(
+ repo_get_merge_bases_many_dirty(the_repository,
  E, F, G);

@@
expression E;
expression F;
@@
- in_merge_bases(
+ repo_in_merge_bases(the_repository,
  E, F);

@@
expression E;
expression F;
expression G;
@@
- in_merge_bases_many(
+ repo_in_merge_bases_many(the_repository,
  E, F, G);

@@
expression E;
expression F;
@@
- get_commit_buffer(
+ repo_get_commit_buffer(the_repository,
  E, F);

@@
expression E;
expression F;
@@
- unuse_commit_buffer(
+ repo_unuse_commit_buffer(the_repository,
  E, F);

@@
expression E;
expression F;
expression G;
@@
- logmsg_reencode(
+ repo_logmsg_reencode(the_repository,
  E, F, G);

@@
expression E;
expression F;
expression G;
expression H;
@@
- format_commit_message(
+ repo_format_commit_message(the_repository,
  E, F, G, H);
