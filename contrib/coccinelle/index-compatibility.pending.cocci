// "the_repository" simple cases
@@
@@
(
- read_cache
+ repo_read_index
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
)
  (
+ &the_index,
  ...)
