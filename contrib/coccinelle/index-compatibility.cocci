// the_index.* variables
@@
@@
(
- active_cache
+ the_index.cache
|
- active_cache_changed
+ the_index.cache_changed
|
- active_cache_tree
+ the_index.cache_tree
)

@@
identifier f != prepare_to_commit;
@@
  f(...) {<...
- active_nr
+ the_index.cache_nr
  ...>}

// "the_repository" simple cases
@@
@@
(
- read_cache_unmerged
+ repo_read_index_unmerged
)
  (
+ the_repository,
  ...)

// "the_index" simple cases
@@
@@
(
- is_cache_unborn
+ is_index_unborn
|
- unmerged_cache
+ unmerged_index
|
- rename_cache_entry_at
+ rename_index_entry_at
|
- chmod_cache_entry
+ chmod_index_entry
|
- cache_file_exists
+ index_file_exists
|
- cache_name_is_other
+ index_name_is_other
|
- unmerge_cache_entry_at
+ unmerge_index_entry_at
|
- add_to_cache
+ add_to_index
|
- add_file_to_cache
+ add_file_to_index
|
- add_cache_entry
+ add_index_entry
|
- remove_file_from_cache
+ remove_file_from_index
|
- ce_match_stat
+ ie_match_stat
|
- ce_modified
+ ie_modified
|
- resolve_undo_clear
+ resolve_undo_clear_index
)
  (
+ &the_index,
  ...)
