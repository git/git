@@
identifier f != { finddata2dirent, precompose_utf8_readdir };
struct dirent *E;
@@
  f(...) {<...
- E->d_type
+ DTYPE(E)
  ...>}
