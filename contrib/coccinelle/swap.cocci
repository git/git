@ swap_with_declaration @
type T;
identifier tmp;
T a, b;
@@
- T tmp = a;
+ T tmp;
+ tmp = a;
  a = b;
  b = tmp;

@ swap @
type T;
T tmp, a, b;
@@
- tmp = a;
- a = b;
- b = tmp;
+ SWAP(a, b);

@ extends swap @
identifier unused;
@@
  {
  ...
- T unused;
  ... when != unused
  }
