@@
expression E;
struct hashmap_entry HME;
@@
- HME.hash = E;
+ hashmap_entry_init(&HME, E);

@@
identifier f !~ "^hashmap_entry_init$";
expression E;
struct hashmap_entry *HMEP;
@@
  f(...) {<...
- HMEP->hash = E;
+ hashmap_entry_init(HMEP, E);
  ...>}
