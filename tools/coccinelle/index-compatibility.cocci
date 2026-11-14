// the_index.* variables
@@
identifier AC = active_cache;
identifier AN = active_nr;
identifier ACC = active_cache_changed;
identifier ACT = active_cache_tree;
@@
(
- AC
+ the_index.cache
|
- AN
+ the_index.cache_nr
|
- ACC
+ the_index.cache_changed
|
- ACT
+ the_index.cache_tree
)

// "the_repository" simple cases
@@
@@
(
- read_cache
+ repo_read_index
|
- read_cache_unmerged
+ repo_read_index_unmerged
|
- hold_locked_index
+ repo_hold_locked_index
)
  (
+ the_repository,
  ...)

// "the_repository" special-cases
@@
@@
(
- read_cache_preload
+ repo_read_index_preload
)
  (
+ the_repository,
  ...
+ , 0
  )

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
|
- cache_name_pos
+ index_name_pos
|
- update_main_cache_tree
+ cache_tree_update
|
- discard_cache
+ discard_index
)
  (
+ &the_index,
  ...)

@@
@@
(
- refresh_and_write_cache
+ repo_refresh_and_write_index
)
  (
+ the_repository,
  ...
+ , NULL, NULL, NULL
  )

// "the_index" special-cases
@@
@@
(
- read_cache_from
+ read_index_from
)
  (
+ &the_index,
  ...
+ , get_git_dir()
  )

@@
@@
(
- refresh_cache
+ refresh_index
)
  (
+ &the_index,
  ...
+ , NULL, NULL, NULL
  )

@@
expression O;
@@
- write_cache_as_tree
+ write_index_as_tree
  (
- O,
+ O, &the_index, get_index_file(),
  ...
  )
