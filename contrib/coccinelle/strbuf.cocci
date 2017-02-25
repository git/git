@ strbuf_addf_with_format_only @
expression E;
constant fmt;
@@
  strbuf_addf(E,
(
  fmt
|
  _(fmt)
)
  );

@ script:python @
fmt << strbuf_addf_with_format_only.fmt;
@@
cocci.include_match("%" not in fmt)

@ extends strbuf_addf_with_format_only @
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
expression E1, E2;
@@
- strbuf_addf(E1, "%s", E2);
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
