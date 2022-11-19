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
