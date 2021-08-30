@@
identifier fd;
identifier die_fn =~ "^(die|die_errno)$";
@@
(
  fd =
- open
+ xopen
  (...);
|
  int fd =
- open
+ xopen
  (...);
)
- if ( \( fd < 0 \| fd == -1 \) ) { die_fn(...); }
