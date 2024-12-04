#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply should not get confused with rename/copy.

'


. ./test-lib.sh

# setup

mkdir -p klibc/arch/x86_64/include/klibc

cat >klibc/arch/x86_64/include/klibc/archsetjmp.h <<\EOF
/*
 * arch/x86_64/include/klibc/archsetjmp.h
 */

#ifndef _KLIBC_ARCHSETJMP_H
#define _KLIBC_ARCHSETJMP_H

struct __jmp_buf {
  unsigned long __rbx;
  unsigned long __rsp;
  unsigned long __rbp;
  unsigned long __r12;
  unsigned long __r13;
  unsigned long __r14;
  unsigned long __r15;
  unsigned long __rip;
};

typedef struct __jmp_buf jmp_buf[1];

#endif /* _SETJMP_H */
EOF
cat >klibc/README <<\EOF
This is a simple readme file.
EOF

cat >patch <<\EOF
diff --git a/klibc/arch/x86_64/include/klibc/archsetjmp.h b/include/arch/cris/klibc/archsetjmp.h
similarity index 76%
copy from klibc/arch/x86_64/include/klibc/archsetjmp.h
copy to include/arch/cris/klibc/archsetjmp.h
--- a/klibc/arch/x86_64/include/klibc/archsetjmp.h
+++ b/include/arch/cris/klibc/archsetjmp.h
@@ -1,21 +1,24 @@
 /*
- * arch/x86_64/include/klibc/archsetjmp.h
+ * arch/cris/include/klibc/archsetjmp.h
  */

 #ifndef _KLIBC_ARCHSETJMP_H
 #define _KLIBC_ARCHSETJMP_H

 struct __jmp_buf {
-  unsigned long __rbx;
-  unsigned long __rsp;
-  unsigned long __rbp;
-  unsigned long __r12;
-  unsigned long __r13;
-  unsigned long __r14;
-  unsigned long __r15;
-  unsigned long __rip;
+  unsigned long __r0;
+  unsigned long __r1;
+  unsigned long __r2;
+  unsigned long __r3;
+  unsigned long __r4;
+  unsigned long __r5;
+  unsigned long __r6;
+  unsigned long __r7;
+  unsigned long __r8;
+  unsigned long __sp;
+  unsigned long __srp;
 };

 typedef struct __jmp_buf jmp_buf[1];

-#endif /* _SETJMP_H */
+#endif /* _KLIBC_ARCHSETJMP_H */
diff --git a/klibc/arch/x86_64/include/klibc/archsetjmp.h b/include/arch/m32r/klibc/archsetjmp.h
similarity index 66%
rename from klibc/arch/x86_64/include/klibc/archsetjmp.h
rename to include/arch/m32r/klibc/archsetjmp.h
--- a/klibc/arch/x86_64/include/klibc/archsetjmp.h
+++ b/include/arch/m32r/klibc/archsetjmp.h
@@ -1,21 +1,21 @@
 /*
- * arch/x86_64/include/klibc/archsetjmp.h
+ * arch/m32r/include/klibc/archsetjmp.h
  */

 #ifndef _KLIBC_ARCHSETJMP_H
 #define _KLIBC_ARCHSETJMP_H

 struct __jmp_buf {
-  unsigned long __rbx;
-  unsigned long __rsp;
-  unsigned long __rbp;
+  unsigned long __r8;
+  unsigned long __r9;
+  unsigned long __r10;
+  unsigned long __r11;
   unsigned long __r12;
   unsigned long __r13;
   unsigned long __r14;
   unsigned long __r15;
-  unsigned long __rip;
 };

 typedef struct __jmp_buf jmp_buf[1];

-#endif /* _SETJMP_H */
+#endif /* _KLIBC_ARCHSETJMP_H */
diff --git a/klibc/README b/klibc/README
--- a/klibc/README
+++ b/klibc/README
@@ -1,1 +1,4 @@
 This is a simple readme file.
+And we add a few
+lines at the
+end of it.
diff --git a/klibc/README b/klibc/arch/README
copy from klibc/README
copy to klibc/arch/README
--- a/klibc/README
+++ b/klibc/arch/README
@@ -1,1 +1,3 @@
 This is a simple readme file.
+And we copy it to one level down, and
+add a few lines at the end of it.
EOF

find klibc -type f -print | xargs git update-index --add --

test_expect_success 'check rename/copy patch' 'git apply --check patch'

test_expect_success 'apply rename/copy patch' 'git apply --index patch'

test_done
