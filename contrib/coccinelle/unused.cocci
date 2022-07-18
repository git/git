// This rule finds sequences of "unused" declerations and uses of a
// variable, where "unused" is defined to include only calling the
// equivalent of alloc, init & free functions on the variable.
@@
type T;
identifier I;
// STRBUF_INIT, but also e.g. STRING_LIST_INIT_DUP (so no anchoring)
constant INIT_MACRO =~ "_INIT";
identifier MALLOC1 =~ "^x?[mc]alloc$";
identifier INIT_ASSIGN1 =~ "^get_worktrees$";
identifier INIT_CALL1 =~ "^[a-z_]*_init$";
identifier REL1 =~ "^[a-z_]*_(release|reset|clear|free)$";
identifier REL2 =~ "^(release|clear|free)_[a-z_]*$";
@@

(
- T I;
|
- T I = { 0 };
|
- T I = INIT_MACRO;
|
- T I = MALLOC1(...);
|
- T I = INIT_ASSIGN1(...);
)

<... when != \( I \| &I \)
(
- \( INIT_CALL1 \)( \( I \| &I \), ...);
|
- I = \( INIT_ASSIGN1 \)(...);
|
- I = MALLOC1(...);
)
...>

(
- \( REL1 \| REL2 \)( \( I \| &I \), ...);
|
- \( REL1 \| REL2 \)( \( &I \| I \) );
)
  ... when != \( I \| &I \)
