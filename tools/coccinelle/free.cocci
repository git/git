@@
expression E;
@@
- if (E)
(
  free(E);
|
  commit_list_free(E);
)

@@
expression E;
@@
- if (!E)
(
  free(E);
|
  commit_list_free(E);
)

@@
expression E;
@@
- free(E);
+ FREE_AND_NULL(E);
- E = NULL;

@@
expression E;
@@
- if (E)
- {
  commit_list_free(E);
  E = NULL;
- }

@@
expression E;
statement S;
@@
- if (E) {
+ if (E)
  S
  commit_list_free(E);
- }
