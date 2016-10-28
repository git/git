@@
expression E;
expression V;
@@
- if (E)
-    V = xstrdup(E);
+ V = xstrdup_or_null(E);
