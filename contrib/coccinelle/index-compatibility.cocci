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
)
  (
+ &the_index,
  ...)
