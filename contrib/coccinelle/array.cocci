@@
expression dst, src, n, E;
@@
  memcpy(dst, src, n * sizeof(
- E[...]
+ *(E)
  ))

@@
type T;
T *ptr;
T[] arr;
expression E, n;
@@
(
  memcpy(ptr, E,
- n * sizeof(*(ptr))
+ n * sizeof(T)
  )
|
  memcpy(arr, E,
- n * sizeof(*(arr))
+ n * sizeof(T)
  )
|
  memcpy(E, ptr,
- n * sizeof(*(ptr))
+ n * sizeof(T)
  )
|
  memcpy(E, arr,
- n * sizeof(*(arr))
+ n * sizeof(T)
  )
)

@@
type T;
T *dst_ptr;
T *src_ptr;
T[] dst_arr;
T[] src_arr;
expression n;
@@
(
- memcpy(dst_ptr, src_ptr, (n) * sizeof(T))
+ COPY_ARRAY(dst_ptr, src_ptr, n)
|
- memcpy(dst_ptr, src_arr, (n) * sizeof(T))
+ COPY_ARRAY(dst_ptr, src_arr, n)
|
- memcpy(dst_arr, src_ptr, (n) * sizeof(T))
+ COPY_ARRAY(dst_arr, src_ptr, n)
|
- memcpy(dst_arr, src_arr, (n) * sizeof(T))
+ COPY_ARRAY(dst_arr, src_arr, n)
)

@@
type T;
T *dst;
T *src;
expression n;
@@
(
- memmove(dst, src, (n) * sizeof(*dst));
+ MOVE_ARRAY(dst, src, n);
|
- memmove(dst, src, (n) * sizeof(*src));
+ MOVE_ARRAY(dst, src, n);
|
- memmove(dst, src, (n) * sizeof(T));
+ MOVE_ARRAY(dst, src, n);
)

@@
type T;
T *ptr;
expression n;
@@
- ptr = xmalloc((n) * sizeof(*ptr));
+ ALLOC_ARRAY(ptr, n);

@@
type T;
T *ptr;
expression n;
@@
- ptr = xmalloc((n) * sizeof(T));
+ ALLOC_ARRAY(ptr, n);
