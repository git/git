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
expression E;
@@
- free(E);
+ FREE_AND_NULL(E);
- E = NULL;
