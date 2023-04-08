// These need to be applied more judiciously because of the risk of
// false positives.
@@
identifier fn;
identifier C1, C2, D;
@@
int fn(const char *C1, const char *C2,
+  struct key_value_info *kvi,
  void *D);

@@
identifier fn;
@@
int fn(const char *, const char *,
+  struct key_value_info *,
  void *);

@@
identifier fn, fn2;
identifier C1, C2, D;
attribute name UNUSED;
@@
int fn(const char *C1, const char *C2,
+  struct key_value_info *kvi,
  void *D) {
<+...
(
fn2(C1, C2,
+ kvi,
...);
|
if(fn2(C1, C2,
+ kvi,
...) < 0) { ... }
|
return fn2(C1, C2,
+ kvi,
...);
)
...+>
  }

@@
identifier fn, fn2;
identifier C1, C2, D;
attribute name UNUSED;
@@
int fn(const char *C1, const char *C2,
+  struct key_value_info *kvi UNUSED,
  void *D) {...}
