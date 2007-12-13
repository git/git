#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin
#

test_description='Test special whitespace in diff engine.

'
. ./test-lib.sh
. ../diff-lib.sh

# Ray Lehtiniemi's example

cat << EOF > x
do {
   nothing;
} while (0);
EOF

git update-index --add x

cat << EOF > x
do
{
   nothing;
}
while (0);
EOF

cat << EOF > expect
diff --git a/x b/x
index adf3937..6edc172 100644
--- a/x
+++ b/x
@@ -1,3 +1,5 @@
-do {
+do
+{
    nothing;
-} while (0);
+}
+while (0);
EOF

git diff > out
test_expect_success "Ray's example without options" 'git diff expect out'

git diff -w > out
test_expect_success "Ray's example with -w" 'git diff expect out'

git diff -b > out
test_expect_success "Ray's example with -b" 'git diff expect out'

tr 'Q' '\015' << EOF > x
whitespace at beginning
whitespace change
whitespace in the middle
whitespace at end
unchanged line
CR at endQ
EOF

git update-index x

cat << EOF > x
	whitespace at beginning
whitespace 	 change
white space in the middle
whitespace at end  
unchanged line
CR at end
EOF

tr 'Q' '\015' << EOF > expect
diff --git a/x b/x
index d99af23..8b32fb5 100644
--- a/x
+++ b/x
@@ -1,6 +1,6 @@
-whitespace at beginning
-whitespace change
-whitespace in the middle
-whitespace at end
+	whitespace at beginning
+whitespace 	 change
+white space in the middle
+whitespace at end  
 unchanged line
-CR at endQ
+CR at end
EOF
git diff > out
test_expect_success 'another test, without options' 'git diff expect out'

cat << EOF > expect
diff --git a/x b/x
index d99af23..8b32fb5 100644
EOF
git diff -w > out
test_expect_success 'another test, with -w' 'git diff expect out'

tr 'Q' '\015' << EOF > expect
diff --git a/x b/x
index d99af23..8b32fb5 100644
--- a/x
+++ b/x
@@ -1,6 +1,6 @@
-whitespace at beginning
+	whitespace at beginning
 whitespace change
-whitespace in the middle
+white space in the middle
 whitespace at end
 unchanged line
 CR at endQ
EOF
git diff -b > out
test_expect_success 'another test, with -b' 'git diff expect out'

test_expect_success 'check mixed spaces and tabs in indent' '

	# This is indented with SP HT SP.
	echo " 	 foo();" > x &&
	git diff --check | grep "Space in indent is followed by a tab"

'

test_expect_success 'check with no whitespace errors' '

	git commit -m "snapshot" &&
	echo "foo();" > x &&
	git diff --check

'

test_expect_failure 'check with trailing whitespace' '

	echo "foo(); " > x &&
	git diff --check

'

test_expect_failure 'check with space before tab in indent' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git diff --check

'

test_expect_success '--check and --exit-code are not exclusive' '

	git checkout x &&
	git diff --check --exit-code

'

test_expect_success '--check and --quiet are not exclusive' '

	git diff --check --quiet

'

test_expect_success 'check staged with no whitespace errors' '

	echo "foo();" > x &&
	git add x &&
	git diff --cached --check

'

test_expect_failure 'check staged with trailing whitespace' '

	echo "foo(); " > x &&
	git add x &&
	git diff --cached --check

'

test_expect_failure 'check staged with space before tab in indent' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git add x &&
	git diff --cached --check

'

test_expect_success 'check with no whitespace errors (diff-index)' '

	echo "foo();" > x &&
	git add x &&
	git diff-index --check HEAD

'

test_expect_failure 'check with trailing whitespace (diff-index)' '

	echo "foo(); " > x &&
	git add x &&
	git diff-index --check HEAD

'

test_expect_failure 'check with space before tab in indent (diff-index)' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git add x &&
	git diff-index --check HEAD

'

test_expect_success 'check staged with no whitespace errors (diff-index)' '

	echo "foo();" > x &&
	git add x &&
	git diff-index --cached --check HEAD

'

test_expect_failure 'check staged with trailing whitespace (diff-index)' '

	echo "foo(); " > x &&
	git add x &&
	git diff-index --cached --check HEAD

'

test_expect_failure 'check staged with space before tab in indent (diff-index)' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git add x &&
	git diff-index --cached --check HEAD

'

test_expect_success 'check with no whitespace errors (diff-tree)' '

	echo "foo();" > x &&
	git commit -m "new commit" x &&
	git diff-tree --check HEAD^ HEAD

'

test_expect_failure 'check with trailing whitespace (diff-tree)' '

	echo "foo(); " > x &&
	git commit -m "another commit" x &&
	git diff-tree --check HEAD^ HEAD

'

test_expect_failure 'check with space before tab in indent (diff-tree)' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git commit -m "yet another" x &&
	git diff-tree --check HEAD^ HEAD

'

test_done
