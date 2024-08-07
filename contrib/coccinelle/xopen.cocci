@@
identifier fd;
identifier die_fn =~ "^(die|die_errno)$";
@@
  int fd =
- open
+ xopen
  (...);
- if ( \( fd < 0 \| fd == -1 \) ) { die_fn(...); }

@@
expression fd;
identifier die_fn =~ "^(die|die_errno)$";
@@
  fd =
- open
+ xopen
  (...);
- if ( \( fd < 0 \| fd == -1 \) ) { die_fn(...); }
