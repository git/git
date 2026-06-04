@@
type T;
identifier i;
expression dst;
struct strvec *src_ptr;
struct strvec src_arr;
@@
(
- for (T i = 0; i < src_ptr->nr; i++) { strvec_push(dst, src_ptr->v[i]); }
+ strvec_pushv(dst, src_ptr->v);
|
- for (T i = 0; i < src_arr.nr; i++) { strvec_push(dst, src_arr.v[i]); }
+ strvec_pushv(dst, src_arr.v);
)

@ separate_loop_index @
type T;
identifier i;
expression dst;
struct strvec *src_ptr;
struct strvec src_arr;
@@
  T i;
  ...
(
- for (i = 0; i < src_ptr->nr; i++) { strvec_push(dst, src_ptr->v[i]); }
+ strvec_pushv(dst, src_ptr->v);
|
- for (i = 0; i < src_arr.nr; i++) { strvec_push(dst, src_arr.v[i]); }
+ strvec_pushv(dst, src_arr.v);
)

@ unused_loop_index extends separate_loop_index @
@@
  {
  ...
- T i;
  ... when != i
  }

@ depends on unused_loop_index @
@@
  if (...)
- {
  strvec_pushv(...);
- }
