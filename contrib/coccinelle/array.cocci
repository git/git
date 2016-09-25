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
