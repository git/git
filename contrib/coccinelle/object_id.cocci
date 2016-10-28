@@
expression E1;
@@
- is_null_sha1(E1.hash)
+ is_null_oid(&E1)

@@
expression E1;
@@
- is_null_sha1(E1->hash)
+ is_null_oid(E1)

@@
expression E1;
@@
- sha1_to_hex(E1.hash)
+ oid_to_hex(&E1)

@@
expression E1;
@@
- sha1_to_hex(E1->hash)
+ oid_to_hex(E1)

@@
expression E1, E2;
@@
- sha1_to_hex_r(E1, E2.hash)
+ oid_to_hex_r(E1, &E2)

@@
expression E1, E2;
@@
- sha1_to_hex_r(E1, E2->hash)
+ oid_to_hex_r(E1, E2)

@@
expression E1;
@@
- hashclr(E1.hash)
+ oidclr(&E1)

@@
expression E1;
@@
- hashclr(E1->hash)
+ oidclr(E1)

@@
expression E1, E2;
@@
- hashcmp(E1.hash, E2.hash)
+ oidcmp(&E1, &E2)

@@
expression E1, E2;
@@
- hashcmp(E1->hash, E2->hash)
+ oidcmp(E1, E2)

@@
expression E1, E2;
@@
- hashcmp(E1->hash, E2.hash)
+ oidcmp(E1, &E2)

@@
expression E1, E2;
@@
- hashcmp(E1.hash, E2->hash)
+ oidcmp(&E1, E2)

@@
expression E1, E2;
@@
- hashcpy(E1.hash, E2.hash)
+ oidcpy(&E1, &E2)

@@
expression E1, E2;
@@
- hashcpy(E1->hash, E2->hash)
+ oidcpy(E1, E2)

@@
expression E1, E2;
@@
- hashcpy(E1->hash, E2.hash)
+ oidcpy(E1, &E2)

@@
expression E1, E2;
@@
- hashcpy(E1.hash, E2->hash)
+ oidcpy(&E1, E2)
