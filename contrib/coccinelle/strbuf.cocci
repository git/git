@ strbuf_addf_with_format_only @
expression E;
constant fmt !~ "%";
@@
- strbuf_addf
+ strbuf_addstr
  (E,
(
  fmt
|
  _(fmt)
)
  );

@@
expression E;
struct strbuf SB;
format F =~ "s";
@@
- strbuf_addf(E, "%@F@", SB.buf);
+ strbuf_addbuf(E, &SB);

@@
expression E;
struct strbuf *SBP;
format F =~ "s";
@@
- strbuf_addf(E, "%@F@", SBP->buf);
+ strbuf_addbuf(E, SBP);

@@
expression E;
struct strbuf SB;
@@
- strbuf_addstr(E, SB.buf);
+ strbuf_addbuf(E, &SB);

@@
expression E;
struct strbuf *SBP;
@@
- strbuf_addstr(E, SBP->buf);
+ strbuf_addbuf(E, SBP);

@@
expression E1, E2;
format F =~ "s";
@@
- strbuf_addf(E1, "%@F@", E2);
+ strbuf_addstr(E1, E2);

@@
expression E1, E2, E3;
@@
- strbuf_addstr(E1, find_unique_abbrev(E2, E3));
+ strbuf_add_unique_abbrev(E1, E2, E3);

@@
expression E1, E2;
@@
- strbuf_addstr(E1, real_path(E2));
+ strbuf_add_real_path(E1, E2);
