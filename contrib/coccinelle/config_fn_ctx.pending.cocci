@ get_fn @
identifier fn, R;
@@
(
(
git_config_from_file
|
git_config_from_file_with_options
|
git_config_from_mem
|
git_config_from_blob_oid
|
read_early_config
|
read_very_early_config
|
config_with_options
|
git_config
|
git_protected_config
|
config_from_gitmodules
)
  (fn, ...)
|
repo_config(R, fn, ...)
)

@ extends get_fn @
identifier C1, C2, D;
@@
int fn(const char *C1, const char *C2,
+ const struct config_context *ctx,
  void *D);

@ extends get_fn @
@@
int fn(const char *, const char *,
+ const struct config_context *,
  void *);

@ extends get_fn @
// Don't change fns that look like callback fns but aren't
identifier fn2 != tar_filter_config && != git_diff_heuristic_config &&
  != git_default_submodule_config && != git_color_config &&
  != bundle_list_update && != parse_object_filter_config;
identifier C1, C2, D1, D2, S;
attribute name UNUSED;
@@
int fn(const char *C1, const char *C2,
+ const struct config_context *ctx,
  void *D1) {
<+...
(
fn2(C1, C2
+ , ctx
, D2);
|
if(fn2(C1, C2
+ , ctx
, D2) < 0) { ... }
|
return fn2(C1, C2
+ , ctx
, D2);
|
S = fn2(C1, C2
+ , ctx
, D2);
)
...+>
  }

@ extends get_fn@
identifier C1, C2, D;
attribute name UNUSED;
@@
int fn(const char *C1, const char *C2,
+ const struct config_context *ctx UNUSED,
  void *D) {...}


// The previous rules don't catch all callbacks, especially if they're defined
// in a separate file from the git_config() call. Fix these manually.
@@
identifier C1, C2, D;
attribute name UNUSED;
@@
int
(
git_ident_config
|
urlmatch_collect_fn
|
write_one_config
|
forbid_remote_url
|
credential_config_callback
)
  (const char *C1, const char *C2,
+ const struct config_context *ctx UNUSED,
  void *D) {...}

@@
identifier C1, C2, D, D2, S, fn2;
@@
int
(
http_options
|
git_status_config
|
git_commit_config
|
git_default_core_config
|
grep_config
)
  (const char *C1, const char *C2,
+ const struct config_context *ctx,
  void *D) {
<+...
(
fn2(C1, C2
+ , ctx
, D2);
|
if(fn2(C1, C2
+ , ctx
, D2) < 0) { ... }
|
return fn2(C1, C2
+ , ctx
, D2);
|
S = fn2(C1, C2
+ , ctx
, D2);
)
...+>
  }
