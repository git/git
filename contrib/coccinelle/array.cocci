@@
type T;
T *dst;
T *src;
expression n;
@@
- memcpy(dst, src, n * sizeof(*dst));
+ COPY_ARRAY(dst, src, n);

@@
type T;
T *dst;
T *src;
expression n;
@@
- memcpy(dst, src, n * sizeof(*src));
+ COPY_ARRAY(dst, src, n);

@@
type T;
T *dst;
T *src;
expression n;
@@
- memcpy(dst, src, n * sizeof(T));
+ COPY_ARRAY(dst, src, n);

@@
type T;
T *ptr;
expression n;
@@
- ptr = xmalloc(n * sizeof(*ptr));
+ ALLOC_ARRAY(ptr, n);

@@
type T;
T *ptr;
expression n;
@@
- ptr = xmalloc(n * sizeof(T));
+ ALLOC_ARRAY(ptr, n);
