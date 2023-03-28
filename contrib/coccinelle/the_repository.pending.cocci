// This file is used for the ongoing refactoring of
// bringing the index or repository struct in all of
// our code base.

@@
@@
(
- read_object_file
+ repo_read_object_file
|
- has_object_file
+ repo_has_object_file
|
- has_object_file_with_flags
+ repo_has_object_file_with_flags
|
- parse_commit_internal
+ repo_parse_commit_internal
|
- parse_commit
+ repo_parse_commit
|
- get_merge_bases
+ repo_get_merge_bases
|
- get_merge_bases_many
+ repo_get_merge_bases_many
|
- get_merge_bases_many_dirty
+ repo_get_merge_bases_many_dirty
|
- in_merge_bases
+ repo_in_merge_bases
|
- in_merge_bases_many
+ repo_in_merge_bases_many
|
- get_commit_buffer
+ repo_get_commit_buffer
|
- unuse_commit_buffer
+ repo_unuse_commit_buffer
|
- logmsg_reencode
+ repo_logmsg_reencode
|
- format_commit_message
+ repo_format_commit_message
)
  (
+ the_repository,
  ...)
