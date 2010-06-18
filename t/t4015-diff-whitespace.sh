#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin
#

test_description='Test special whitespace in diff engine.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

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
test_expect_success "Ray's example without options" 'test_cmp expect out'

git diff -w > out
test_expect_success "Ray's example with -w" 'test_cmp expect out'

git diff -b > out
test_expect_success "Ray's example with -b" 'test_cmp expect out'

tr 'Q' '\015' << EOF > x
whitespace at beginning
whitespace change
whitespace in the middle
whitespace at end
unchanged line
CR at endQ
EOF

git update-index x

tr '_' ' ' << EOF > x
	whitespace at beginning
whitespace 	 change
white space in the middle
whitespace at end__
unchanged line
CR at end
EOF

tr 'Q_' '\015 ' << EOF > expect
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
+whitespace at end__
 unchanged line
-CR at endQ
+CR at end
EOF
git diff > out
test_expect_success 'another test, without options' 'test_cmp expect out'

cat << EOF > expect
EOF
git diff -w > out
test_expect_success 'another test, with -w' 'test_cmp expect out'
git diff -w -b > out
test_expect_success 'another test, with -w -b' 'test_cmp expect out'
git diff -w --ignore-space-at-eol > out
test_expect_success 'another test, with -w --ignore-space-at-eol' 'test_cmp expect out'
git diff -w -b --ignore-space-at-eol > out
test_expect_success 'another test, with -w -b --ignore-space-at-eol' 'test_cmp expect out'

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
test_expect_success 'another test, with -b' 'test_cmp expect out'
git diff -b --ignore-space-at-eol > out
test_expect_success 'another test, with -b --ignore-space-at-eol' 'test_cmp expect out'

tr 'Q' '\015' << EOF > expect
diff --git a/x b/x
index d99af23..8b32fb5 100644
--- a/x
+++ b/x
@@ -1,6 +1,6 @@
-whitespace at beginning
-whitespace change
-whitespace in the middle
+	whitespace at beginning
+whitespace 	 change
+white space in the middle
 whitespace at end
 unchanged line
 CR at endQ
EOF
git diff --ignore-space-at-eol > out
test_expect_success 'another test, with --ignore-space-at-eol' 'test_cmp expect out'

test_expect_success 'check mixed spaces and tabs in indent' '

	# This is indented with SP HT SP.
	echo " 	 foo();" > x &&
	git diff --check | grep "space before tab in indent"

'

test_expect_success 'check mixed tabs and spaces in indent' '

	# This is indented with HT SP HT.
	echo "	 	foo();" > x &&
	git diff --check | grep "space before tab in indent"

'

test_expect_success 'check with no whitespace errors' '

	git commit -m "snapshot" &&
	echo "foo();" > x &&
	git diff --check

'

test_expect_success 'check with trailing whitespace' '

	echo "foo(); " > x &&
	test_must_fail git diff --check

'

test_expect_success 'check with space before tab in indent' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	test_must_fail git diff --check

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

test_expect_success 'check staged with trailing whitespace' '

	echo "foo(); " > x &&
	git add x &&
	test_must_fail git diff --cached --check

'

test_expect_success 'check staged with space before tab in indent' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git add x &&
	test_must_fail git diff --cached --check

'

test_expect_success 'check with no whitespace errors (diff-index)' '

	echo "foo();" > x &&
	git add x &&
	git diff-index --check HEAD

'

test_expect_success 'check with trailing whitespace (diff-index)' '

	echo "foo(); " > x &&
	git add x &&
	test_must_fail git diff-index --check HEAD

'

test_expect_success 'check with space before tab in indent (diff-index)' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git add x &&
	test_must_fail git diff-index --check HEAD

'

test_expect_success 'check staged with no whitespace errors (diff-index)' '

	echo "foo();" > x &&
	git add x &&
	git diff-index --cached --check HEAD

'

test_expect_success 'check staged with trailing whitespace (diff-index)' '

	echo "foo(); " > x &&
	git add x &&
	test_must_fail git diff-index --cached --check HEAD

'

test_expect_success 'check staged with space before tab in indent (diff-index)' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git add x &&
	test_must_fail git diff-index --cached --check HEAD

'

test_expect_success 'check with no whitespace errors (diff-tree)' '

	echo "foo();" > x &&
	git commit -m "new commit" x &&
	git diff-tree --check HEAD^ HEAD

'

test_expect_success 'check with trailing whitespace (diff-tree)' '

	echo "foo(); " > x &&
	git commit -m "another commit" x &&
	test_must_fail git diff-tree --check HEAD^ HEAD

'

test_expect_success 'check with space before tab in indent (diff-tree)' '

	# indent has space followed by hard tab
	echo " 	foo();" > x &&
	git commit -m "yet another" x &&
	test_must_fail git diff-tree --check HEAD^ HEAD

'

test_expect_success 'check trailing whitespace (trailing-space: off)' '

	git config core.whitespace "-trailing-space" &&
	echo "foo ();   " > x &&
	git diff --check

'

test_expect_success 'check trailing whitespace (trailing-space: on)' '

	git config core.whitespace "trailing-space" &&
	echo "foo ();   " > x &&
	test_must_fail git diff --check

'

test_expect_success 'check space before tab in indent (space-before-tab: off)' '

	# indent contains space followed by HT
	git config core.whitespace "-space-before-tab" &&
	echo " 	foo ();" > x &&
	git diff --check

'

test_expect_success 'check space before tab in indent (space-before-tab: on)' '

	# indent contains space followed by HT
	git config core.whitespace "space-before-tab" &&
	echo " 	foo ();   " > x &&
	test_must_fail git diff --check

'

test_expect_success 'check spaces as indentation (indent-with-non-tab: off)' '

	git config core.whitespace "-indent-with-non-tab"
	echo "        foo ();" > x &&
	git diff --check

'

test_expect_success 'check spaces as indentation (indent-with-non-tab: on)' '

	git config core.whitespace "indent-with-non-tab" &&
	echo "        foo ();" > x &&
	test_must_fail git diff --check

'

test_expect_success 'check tabs and spaces as indentation (indent-with-non-tab: on)' '

	git config core.whitespace "indent-with-non-tab" &&
	echo "	                foo ();" > x &&
	test_must_fail git diff --check

'

test_expect_success 'check tabs as indentation (tab-in-indent: off)' '

	git config core.whitespace "-tab-in-indent" &&
	echo "	foo ();" > x &&
	git diff --check

'

test_expect_success 'check tabs as indentation (tab-in-indent: on)' '

	git config core.whitespace "tab-in-indent" &&
	echo "	foo ();" > x &&
	test_must_fail git diff --check

'

test_expect_success 'check tabs and spaces as indentation (tab-in-indent: on)' '

	git config core.whitespace "tab-in-indent" &&
	echo "	                foo ();" > x &&
	test_must_fail git diff --check

'

test_expect_success 'check tab-in-indent and indent-with-non-tab conflict' '

	git config core.whitespace "tab-in-indent,indent-with-non-tab" &&
	echo "foo ();" > x &&
	test_must_fail git diff --check

'

test_expect_success 'check tab-in-indent excluded from wildcard whitespace attribute' '

	git config --unset core.whitespace &&
	echo "x whitespace" > .gitattributes &&
	echo "	  foo ();" > x &&
	git diff --check &&
	rm -f .gitattributes

'

test_expect_success 'line numbers in --check output are correct' '

	echo "" > x &&
	echo "foo(); " >> x &&
	git diff --check | grep "x:2:"

'

test_expect_success 'checkdiff detects new trailing blank lines (1)' '
	echo "foo();" >x &&
	echo "" >>x &&
	git diff --check | grep "new blank line"
'

test_expect_success 'checkdiff detects new trailing blank lines (2)' '
	{ echo a; echo b; echo; echo; } >x &&
	git add x &&
	{ echo a; echo; echo; echo; echo; } >x &&
	git diff --check | grep "new blank line"
'

test_expect_success 'checkdiff allows new blank lines' '
	git checkout x &&
	mv x y &&
	(
		echo "/* This is new */" &&
		echo "" &&
		cat y
	) >x &&
	git diff --check
'

cat <<EOF >expect
EOF
test_expect_success 'whitespace-only changes not reported' '
	git reset --hard &&
	echo >x "hello world" &&
	git add x &&
	git commit -m "hello 1" &&
	echo >x "hello  world" &&
	git diff -b >actual &&
	test_cmp expect actual
'

cat <<EOF >expect
diff --git a/x b/z
similarity index NUM%
rename from x
rename to z
index 380c32a..a97b785 100644
EOF
test_expect_success 'whitespace-only changes reported across renames' '
	git reset --hard &&
	for i in 1 2 3 4 5 6 7 8 9; do echo "$i$i$i$i$i$i"; done >x &&
	git add x &&
	git commit -m "base" &&
	sed -e "5s/^/ /" x >z &&
	git rm x &&
	git add z &&
	git diff -w -M --cached |
	sed -e "/^similarity index /s/[0-9][0-9]*/NUM/" >actual &&
	test_cmp expect actual
'

cat >expected <<\EOF
diff --git a/empty b/void
similarity index 100%
rename from empty
rename to void
EOF

test_expect_success 'rename empty' '
	git reset --hard &&
	>empty &&
	git add empty &&
	git commit -m empty &&
	git mv empty void &&
	git diff -w --cached -M >current &&
	test_cmp expected current
'

test_expect_success 'combined diff with autocrlf conversion' '

	git reset --hard &&
	echo >x hello &&
	git commit -m "one side" x &&
	git checkout HEAD^ &&
	echo >x goodbye &&
	git commit -m "the other side" x &&
	git config core.autocrlf true &&
	test_must_fail git merge master &&

	git diff | sed -e "1,/^@@@/d" >actual &&
	! grep "^-" actual

'

test_done
