// This rule finds sequences of "unused" declerations and uses of a
// variable, where "unused" is defined to include only calling the
// equivalent of alloc, init & free functions on the variable.
@@
type T;
identifier I;
constant INIT_MACRO =~ "^STRBUF_INIT$";
identifier MALLOC1 =~ "^x?[mc]alloc$";
identifier INIT_CALL1 =~ "^strbuf_init$";
identifier REL1 =~ "^strbuf_(release|reset)$";
@@

(
- T I;
|
- T I = { 0 };
|
- T I = INIT_MACRO;
|
- T I = MALLOC1(...);
)

<... when != \( I \| &I \)
(
- \( INIT_CALL1 \)( \( I \| &I \), ...);
|
- I = MALLOC1(...);
)
...>

- \( REL1 \)( \( &I \| I \) );
  ... when != \( I \| &I \)
