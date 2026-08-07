@@
identifier f != git_hash_init;
expression ALGO;
struct git_hash_ctx *CTX;
@@
  f(...) {<...
- ALGO->init_fn(CTX);
+ git_hash_init(CTX, ALGO);
  ...>}

@@
identifier f != git_hash_clone;
expression ALGO;
struct git_hash_ctx *SRC;
struct git_hash_ctx *DST;
@@
  f(...) {<...
- ALGO->clone_fn(DST, SRC);
+ git_hash_clone(DST, SRC);
  ...>}

@@
identifier f != git_hash_update;
expression ALGO;
struct git_hash_ctx *CTX;
expression list ARGS;
@@
  f(...) {<...
- ALGO->update_fn(CTX, ARGS);
+ git_hash_update(CTX, ARGS);
  ...>}

@@
identifier f != git_hash_final;
expression ALGO;
struct git_hash_ctx *CTX;
expression list ARGS;
@@
  f(...) {<...
- ALGO->final_fn(ARGS, CTX);
+ git_hash_final(ARGS, CTX);
  ...>}

@@
identifier f != git_hash_final_oid;
expression ALGO;
struct git_hash_ctx *CTX;
expression list ARGS;
@@
  f(...) {<...
- ALGO->final_oid_fn(ARGS, CTX);
+ git_hash_final_oid(ARGS, CTX);
  ...>}

@@
identifier f != git_hash_discard;
expression ALGO;
struct git_hash_ctx *CTX;
@@
  f(...) {<...
- ALGO->discard_fn(CTX);
+ git_hash_discard(CTX);
  ...>}
