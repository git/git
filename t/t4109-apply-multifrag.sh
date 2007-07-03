#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
# Copyright (c) 2005 Robert Fitzsimons
#

test_description='git apply test patches with multiple fragments.

'
. ./test-lib.sh

# setup

cat > patch1.patch <<\EOF
diff --git a/main.c b/main.c
new file mode 100644
--- /dev/null
+++ b/main.c
@@ -0,0 +1,23 @@
+#include <stdio.h>
+
+int func(int num);
+void print_int(int num);
+
+int main() {
+	int i;
+
+	for (i = 0; i < 10; i++) {
+		print_int(func(i));
+	}
+
+	return 0;
+}
+
+int func(int num) {
+	return num * num;
+}
+
+void print_int(int num) {
+	printf("%d", num);
+}
+
EOF
cat > patch2.patch <<\EOF
diff --git a/main.c b/main.c
--- a/main.c
+++ b/main.c
@@ -1,7 +1,9 @@
+#include <stdlib.h>
 #include <stdio.h>
 
 int func(int num);
 void print_int(int num);
+void print_ln();
 
 int main() {
 	int i;
@@ -10,6 +12,8 @@
 		print_int(func(i));
 	}
 
+	print_ln();
+
 	return 0;
 }
 
@@ -21,3 +25,7 @@
 	printf("%d", num);
 }
 
+void print_ln() {
+	printf("\n");
+}
+
EOF
cat > patch3.patch <<\EOF
diff --git a/main.c b/main.c
--- a/main.c
+++ b/main.c
@@ -1,9 +1,7 @@
-#include <stdlib.h>
 #include <stdio.h>
 
 int func(int num);
 void print_int(int num);
-void print_ln();
 
 int main() {
 	int i;
@@ -12,8 +10,6 @@
 		print_int(func(i));
 	}
 
-	print_ln();
-
 	return 0;
 }
 
@@ -25,7 +21,3 @@
 	printf("%d", num);
 }
 
-void print_ln() {
-	printf("\n");
-}
-
EOF
cat > patch4.patch <<\EOF
diff --git a/main.c b/main.c
--- a/main.c
+++ b/main.c
@@ -1,13 +1,14 @@
 #include <stdio.h>
 
 int func(int num);
-void print_int(int num);
+int func2(int num);
 
 int main() {
 	int i;
 
 	for (i = 0; i < 10; i++) {
-		print_int(func(i));
+		printf("%d", func(i));
+		printf("%d", func3(i));
 	}
 
 	return 0;
@@ -17,7 +18,7 @@
 	return num * num;
 }
 
-void print_int(int num) {
-	printf("%d", num);
+int func2(int num) {
+	return num * num * num;
 }
 
EOF

test_expect_success "S = git apply (1)" \
    'git apply patch1.patch patch2.patch'
mv main.c main.c.git

test_expect_success "S = patch (1)" \
    'cat patch1.patch patch2.patch | patch -p1'

test_expect_success "S = cmp (1)" \
    'cmp main.c.git main.c'

rm -f main.c main.c.git

test_expect_success "S = git apply (2)" \
    'git apply patch1.patch patch2.patch patch3.patch'
mv main.c main.c.git

test_expect_success "S = patch (2)" \
    'cat patch1.patch patch2.patch patch3.patch | patch -p1'

test_expect_success "S = cmp (2)" \
    'cmp main.c.git main.c'

rm -f main.c main.c.git

test_expect_success "S = git apply (3)" \
    'git apply patch1.patch patch4.patch'
mv main.c main.c.git

test_expect_success "S = patch (3)" \
    'cat patch1.patch patch4.patch | patch -p1'

test_expect_success "S = cmp (3)" \
    'cmp main.c.git main.c'

test_done

