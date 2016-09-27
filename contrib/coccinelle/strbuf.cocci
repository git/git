@@
expression E1, E2;
@@
- strbuf_addf(E1, E2);
+ strbuf_addstr(E1, E2);

@@
expression E1, E2;
@@
- strbuf_addf(E1, "%s", E2);
+ strbuf_addstr(E1, E2);

@@
expression E1, E2, E3;
@@
- strbuf_addstr(E1, find_unique_abbrev(E2, E3));
+ strbuf_add_unique_abbrev(E1, E2, E3);
