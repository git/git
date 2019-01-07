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
- sha1_to_hex(OID.hash)
+ oid_to_hex(&OID)

@@
identifier f != oid_to_hex;
struct object_id *OIDPTR;
@@
  f(...) {<...
- sha1_to_hex(OIDPTR->hash)
+ oid_to_hex(OIDPTR)
  ...>}

@@
expression E;
struct object_id OID;
@@
- sha1_to_hex_r(E, OID.hash)
+ oid_to_hex_r(E, &OID)

@@
identifier f != oid_to_hex_r;
expression E;
struct object_id *OIDPTR;
@@
   f(...) {<...
- sha1_to_hex_r(E, OIDPTR->hash)
+ oid_to_hex_r(E, OIDPTR)
  ...>}

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
struct object_id OID1, OID2;
@@
- hashcpy(OID1.hash, OID2.hash)
+ oidcpy(&OID1, &OID2)

@@
identifier f != oidcpy;
struct object_id *OIDPTR1;
struct object_id *OIDPTR2;
@@
  f(...) {<...
- hashcpy(OIDPTR1->hash, OIDPTR2->hash)
+ oidcpy(OIDPTR1, OIDPTR2)
  ...>}

@@
struct object_id *OIDPTR;
struct object_id OID;
@@
- hashcpy(OIDPTR->hash, OID.hash)
+ oidcpy(OIDPTR, &OID)

@@
struct object_id *OIDPTR;
struct object_id OID;
@@
- hashcpy(OID.hash, OIDPTR->hash)
+ oidcpy(&OID, OIDPTR)

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
