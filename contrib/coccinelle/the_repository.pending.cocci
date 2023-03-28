// This file is used for the ongoing refactoring of
// bringing the index or repository struct in all of
// our code base.

@@
@@
(
// commit.h
- parse_commit_internal
+ repo_parse_commit_internal
|
- parse_commit
+ repo_parse_commit
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
- get_commit_tree
+ repo_get_commit_tree
// diff.h
|
- diff_setup
+ repo_diff_setup
// object-store.h
|
- read_object_file
+ repo_read_object_file
|
- has_object_file
+ repo_has_object_file
|
- has_object_file_with_flags
+ repo_has_object_file_with_flags
// pretty.h
|
- format_commit_message
+ repo_format_commit_message
// packfile.h
|
- approximate_object_count
+ repo_approximate_object_count
// promisor-remote.h
|
- promisor_remote_reinit
+ repo_promisor_remote_reinit
|
- promisor_remote_find
+ repo_promisor_remote_find
|
- has_promisor_remote
+ repo_has_promisor_remote
// refs.h
|
- dwim_ref
+ repo_dwim_ref
// rerere.h
|
- rerere
+ repo_rerere
// revision.h
|
- init_revisions
+ repo_init_revisions
)
  (
+ the_repository,
  ...)
