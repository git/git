@@
expression E;
@@
- if (E)
  free(E);

@@
expression E;
@@
- if (!E)
  free(E);

@@
type T;
T *ptr;
@@
- free(ptr);
- ptr = NULL;
+ FREE_AND_NULL(ptr);
