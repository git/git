#!/bin/sh

test_description='common tail optimization'

. ./test-lib.sh

z=zzzzzzzz ;# 8
z="$z$z$z$z$z$z$z$z" ;# 64
z="$z$z$z$z$z$z$z$z" ;# 512
z="$z$z$z$z" ;# 2048
z2047=$(expr "$z" : '.\(.*\)') ; #2047

test_expect_success setup '

	echo "a$z2047" >file-a &&
	echo "b" >file-b &&
	echo "$z2047" >>file-b &&
	echo "c$z2047" | tr -d "\012" >file-c &&
	echo "d" >file-d &&
	echo "$z2047" | tr -d "\012" >>file-d &&

	git add file-a file-b file-c file-d &&

	echo "A$z2047" >file-a &&
	echo "B" >file-b &&
	echo "$z2047" >>file-b &&
	echo "C$z2047" | tr -d "\012" >file-c &&
	echo "D" >file-d &&
	echo "$z2047" | tr -d "\012" >>file-d

'

cat >expect <<\EOF
diff --git a/file-a b/file-a
--- a/file-a
+++ b/file-a
@@ -1 +1 @@
-aZ
+AZ
diff --git a/file-b b/file-b
--- a/file-b
+++ b/file-b
@@ -1 +1 @@
-b
+B
diff --git a/file-c b/file-c
--- a/file-c
+++ b/file-c
@@ -1 +1 @@
-cZ
\ No newline at end of file
+CZ
\ No newline at end of file
diff --git a/file-d b/file-d
--- a/file-d
+++ b/file-d
@@ -1 +1 @@
-d
+D
EOF

test_expect_success 'diff -U0' '

	git diff -U0 | sed -e "/^index/d" -e "s/$z2047/Z/g" >actual &&
	diff -u expect actual

'

test_done
