@@
type T;
T *dst_ptr;
T *src_ptr;
expression n;
@@
- memcpy(dst_ptr, src_ptr, (n) * \( sizeof(T)
-                                \| sizeof(*(dst_ptr))
-                                \| sizeof(*(src_ptr))
-                                \| sizeof(dst_ptr[...])
-                                \| sizeof(src_ptr[...])
-                                \) )
+ COPY_ARRAY(dst_ptr, src_ptr, n)

@@
type T;
T *dst_ptr;
T[] src_arr;
expression n;
@@
- memcpy(dst_ptr, src_arr, (n) * \( sizeof(T)
-                                \| sizeof(*(dst_ptr))
-                                \| sizeof(*(src_arr))
-                                \| sizeof(dst_ptr[...])
-                                \| sizeof(src_arr[...])
-                                \) )
+ COPY_ARRAY(dst_ptr, src_arr, n)

@@
type T;
T[] dst_arr;
T *src_ptr;
expression n;
@@
- memcpy(dst_arr, src_ptr, (n) * \( sizeof(T)
-                                \| sizeof(*(dst_arr))
-                                \| sizeof(*(src_ptr))
-                                \| sizeof(dst_arr[...])
-                                \| sizeof(src_ptr[...])
-                                \) )
+ COPY_ARRAY(dst_arr, src_ptr, n)

@@
type T;
T[] dst_arr;
T[] src_arr;
expression n;
@@
- memcpy(dst_arr, src_arr, (n) * \( sizeof(T)
-                                \| sizeof(*(dst_arr))
-                                \| sizeof(*(src_arr))
-                                \| sizeof(dst_arr[...])
-                                \| sizeof(src_arr[...])
-                                \) )
+ COPY_ARRAY(dst_arr, src_arr, n)

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

@@
type T;
T *ptr;
expression n != 1;
@@
- ptr = xcalloc(n, \( sizeof(*ptr) \| sizeof(T) \) )
+ CALLOC_ARRAY(ptr, n)

@@
expression dst, src, n;
@@
-ALLOC_ARRAY(dst, n);
-COPY_ARRAY(dst, src, n);
+DUP_ARRAY(dst, src, n);
