#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
# Copyright (c) 2005 Robert Fitzsimons
#

test_description='git apply test for patches which require scanning forwards and backwards.

'
. ./test-lib.sh

# setup

cat > patch1.patch <<\EOF
diff --git a/new.txt b/new.txt
new file mode 100644
--- /dev/null
+++ b/new.txt
@@ -0,0 +1,12 @@
+a1
+a11
+a111
+a1111
+b1
+b11
+b111
+b1111
+c1
+c11
+c111
+c1111
EOF
cat > patch2.patch <<\EOF
diff --git a/new.txt b/new.txt
--- a/new.txt
+++ b/new.txt
@@ -1,7 +1,3 @@
-a1
-a11
-a111
-a1111
 b1
 b11
 b111
EOF
cat > patch3.patch <<\EOF
diff --git a/new.txt b/new.txt
--- a/new.txt
+++ b/new.txt
@@ -6,6 +6,10 @@
 b11
 b111
 b1111
+b2
+b22
+b222
+b2222
 c1
 c11
 c111
EOF
cat > patch4.patch <<\EOF
diff --git a/new.txt b/new.txt
--- a/new.txt
+++ b/new.txt
@@ -1,3 +1,7 @@
+a1
+a11
+a111
+a1111
 b1
 b11
 b111
EOF
cat > patch5.patch <<\EOF
diff --git a/new.txt b/new.txt
--- a/new.txt
+++ b/new.txt
@@ -10,3 +10,7 @@
 c11
 c111
 c1111
+c2
+c22
+c222
+c2222
EOF

test_expect_success "S = git apply scan" \
    'git apply patch1.patch patch2.patch patch3.patch patch4.patch patch5.patch'
mv new.txt apply.txt

test_expect_success "S = patch scan" \
    'cat patch1.patch patch2.patch patch3.patch patch4.patch patch5.patch | patch'
mv new.txt patch.txt

test_expect_success "S = cmp" \
    'cmp apply.txt patch.txt'

test_done
