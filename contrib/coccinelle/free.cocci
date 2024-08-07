@@
expression E;
@@
- if (E)
(
  free(E);
|
  free_commit_list(E);
)

@@
expression E;
@@
- if (!E)
(
  free(E);
|
  free_commit_list(E);
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
  free_commit_list(E);
  E = NULL;
- }

@@
expression E;
statement S;
@@
- if (E) {
+ if (E)
  S
  free_commit_list(E);
- }
