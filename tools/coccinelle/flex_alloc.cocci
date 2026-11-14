@@
expression str;
identifier x, flexname;
@@
- FLEX_ALLOC_MEM(x, flexname, str, strlen(str));
+ FLEX_ALLOC_STR(x, flexname, str);

@@
expression str;
identifier x, ptrname;
@@
- FLEXPTR_ALLOC_MEM(x, ptrname, str, strlen(str));
+ FLEXPTR_ALLOC_STR(x, ptrname, str);
