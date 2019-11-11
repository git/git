@@
struct object_id OID;
@@
- is_null_sha1(OID.hash)
+ is_null_oid(&OID)

@@
struct object_id *OIDPTR;
@@
- is_null_sha1(OIDPTR->hash)
+ is_null_oid(OIDPTR)

@@
struct object_id OID;
@@
- hashclr(OID.hash)
+ oidclr(&OID)

@@
identifier f != oidclr;
struct object_id *OIDPTR;
@@
  f(...) {<...
- hashclr(OIDPTR->hash)
+ oidclr(OIDPTR)
  ...>}

@@
struct object_id OID1, OID2;
@@
- hashcmp(OID1.hash, OID2.hash)
+ oidcmp(&OID1, &OID2)

@@
identifier f != oidcmp;
struct object_id *OIDPTR1, OIDPTR2;
@@
  f(...) {<...
- hashcmp(OIDPTR1->hash, OIDPTR2->hash)
+ oidcmp(OIDPTR1, OIDPTR2)
  ...>}

@@
struct object_id *OIDPTR;
struct object_id OID;
@@
- hashcmp(OIDPTR->hash, OID.hash)
+ oidcmp(OIDPTR, &OID)

@@
struct object_id *OIDPTR;
struct object_id OID;
@@
- hashcmp(OID.hash, OIDPTR->hash)
+ oidcmp(&OID, OIDPTR)

@@
struct object_id *OIDPTR1;
struct object_id *OIDPTR2;
@@
- oidcmp(OIDPTR1, OIDPTR2) == 0
+ oideq(OIDPTR1, OIDPTR2)

@@
identifier f != hasheq;
expression E1, E2;
@@
  f(...) {<...
- hashcmp(E1, E2) == 0
+ hasheq(E1, E2)
  ...>}

@@
struct object_id *OIDPTR1;
struct object_id *OIDPTR2;
@@
- oidcmp(OIDPTR1, OIDPTR2) != 0
+ !oideq(OIDPTR1, OIDPTR2)

@@
identifier f != hasheq;
expression E1, E2;
@@
  f(...) {<...
- hashcmp(E1, E2) != 0
+ !hasheq(E1, E2)
  ...>}
