@@
type T;
T *ptr;
expression n;
@@
  xcalloc(
+ n,
  \( sizeof(T) \| sizeof(*ptr) \)
- , n
  )
