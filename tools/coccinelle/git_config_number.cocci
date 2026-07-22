@@
identifier C1, C2, C3;
@@
(
(
git_config_int
|
git_config_int64
|
git_config_ulong
|
git_config_ssize_t
)
  (C1, C2
+ , ctx->kvi
  )
|
(
git_configset_get_value
|
git_config_bool_or_int
)
  (C1, C2
+ , ctx->kvi
 , C3
  )
)
