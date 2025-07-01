// Fully migrated "the_repository" additions
@@
@@
(
// cache.h
- get_oid
+ repo_get_oid
|
- get_oid_commit
+ repo_get_oid_commit
|
- get_oid_committish
+ repo_get_oid_committish
|
- get_oid_tree
+ repo_get_oid_tree
|
- get_oid_treeish
+ repo_get_oid_treeish
|
- get_oid_blob
+ repo_get_oid_blob
|
- get_oid_mb
+ repo_get_oid_mb
|
- find_unique_abbrev
+ repo_find_unique_abbrev
|
- find_unique_abbrev_r
+ repo_find_unique_abbrev_r
|
- for_each_abbrev
+ repo_for_each_abbrev
|
- interpret_branch_name
+ repo_interpret_branch_name
|
- peel_to_type
+ repo_peel_to_type
// commit-reach.h
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
// commit.h
|
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
// odb.h
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
