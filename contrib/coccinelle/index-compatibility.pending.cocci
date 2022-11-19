// the_index.* variables
@@
@@
(
- active_cache
+ the_index.cache
|
- active_nr
+ the_index.cache_nr
|
- active_cache_changed
+ the_index.cache_changed
|
- active_cache_tree
+ the_index.cache_tree
)

// "the_repository" simple cases
@@
@@
(
- read_cache
+ repo_read_index
|
- hold_locked_index
+ repo_hold_locked_index
)
  (
+ the_repository,
  ...)

// "the_index" simple cases
@@
@@
(
- discard_cache
+ discard_index
|
- cache_name_pos
+ index_name_pos
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
