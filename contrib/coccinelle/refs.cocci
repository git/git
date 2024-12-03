// Migrate "refs.h" to not rely on `the_repository` implicitly anymore.
@@
@@
(
- resolve_ref_unsafe
+ refs_resolve_ref_unsafe
|
- resolve_refdup
+ refs_resolve_refdup
|
- read_ref_full
+ refs_read_ref_full
|
- read_ref
+ refs_read_ref
|
- ref_exists
+ refs_ref_exists
|
- head_ref
+ refs_head_ref
|
- for_each_ref
+ refs_for_each_ref
|
- for_each_ref_in
+ refs_for_each_ref_in
|
- for_each_fullref_in
+ refs_for_each_fullref_in
|
- for_each_tag_ref
+ refs_for_each_tag_ref
|
- for_each_branch_ref
+ refs_for_each_branch_ref
|
- for_each_remote_ref
+ refs_for_each_remote_ref
|
- for_each_glob_ref
+ refs_for_each_glob_ref
|
- for_each_glob_ref_in
+ refs_for_each_glob_ref_in
|
- head_ref_namespaced
+ refs_head_ref_namespaced
|
- for_each_namespaced_ref
+ refs_for_each_namespaced_ref
|
- for_each_rawref
+ refs_for_each_rawref
|
- safe_create_reflog
+ refs_create_reflog
|
- reflog_exists
+ refs_reflog_exists
|
- delete_ref
+ refs_delete_ref
|
- delete_refs
+ refs_delete_refs
|
- delete_reflog
+ refs_delete_reflog
|
- for_each_reflog_ent
+ refs_for_each_reflog_ent
|
- for_each_reflog_ent_reverse
+ refs_for_each_reflog_ent_reverse
|
- for_each_reflog
+ refs_for_each_reflog
|
- shorten_unambiguous_ref
+ refs_shorten_unambiguous_ref
|
- rename_ref
+ refs_rename_ref
|
- copy_existing_ref
+ refs_copy_existing_ref
|
- create_symref
+ refs_create_symref
|
- ref_transaction_begin
+ ref_store_transaction_begin
|
- update_ref
+ refs_update_ref
|
- reflog_expire
+ refs_reflog_expire
)
  (
+ get_main_ref_store(the_repository),
  ...)
