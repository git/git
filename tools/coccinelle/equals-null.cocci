/* SPDX-License-Identifier: LGPL-2.1-or-later */
@@
expression e;
statement s;
@@
if (
(
!e
|
- e == NULL
+ !e
)
   )
   {...}
else s

@@
expression e;
statement s;
@@
if (
(
e
|
- e != NULL
+ e
)
   )
   {...}
else s
