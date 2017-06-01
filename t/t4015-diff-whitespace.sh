#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin
#

test_description='Test special whitespace in diff engine.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

test_expect_success "Ray Lehtiniemi's example" '
	cat <<-\EOF >x &&
	do {
	   nothing;
	} while (0);
	EOF
	git update-index --add x &&

	cat <<-\EOF >x &&
	do
	{
	   nothing;
	}
	while (0);
	EOF

	cat <<-\EOF >expect &&
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

	git diff >out &&
	test_cmp expect out &&

	git diff -w >out &&
	test_cmp expect out &&

	git diff -b >out &&
	test_cmp expect out
'

test_expect_success 'another test, without options' '
	tr Q "\015" <<-\EOF >x &&
	whitespace at beginning
	whitespace change
	whitespace in the middle
	whitespace at end
	unchanged line
	CR at endQ
	EOF

	git update-index x &&

	tr "_" " " <<-\EOF >x &&
	_	whitespace at beginning
	whitespace 	 change
	white space in the middle
	whitespace at end__
	unchanged line
	CR at end
	EOF

	tr "Q_" "\015 " <<-\EOF >expect &&
	diff --git a/x b/x
	index d99af23..22d9f73 100644
	--- a/x
	+++ b/x
	@@ -1,6 +1,6 @@
	-whitespace at beginning
	-whitespace change
	-whitespace in the middle
	-whitespace at end
	+ 	whitespace at beginning
	+whitespace 	 change
	+white space in the middle
	+whitespace at end__
	 unchanged line
	-CR at endQ
	+CR at end
	EOF

	git diff >out &&
	test_cmp expect out &&

	>expect &&
	git diff -w >out &&
	test_cmp expect out &&

	git diff -w -b >out &&
	test_cmp expect out &&

	git diff -w --ignore-space-at-eol >out &&
	test_cmp expect out &&

	git diff -w -b --ignore-space-at-eol >out &&
	test_cmp expect out &&


	tr "Q_" "\015 " <<-\EOF >expect &&
	diff --git a/x b/x
	index d99af23..22d9f73 100644
	--- a/x
	+++ b/x
	@@ -1,6 +1,6 @@
	-whitespace at beginning
	+_	whitespace at beginning
	 whitespace 	 change
	-whitespace in the middle
	+white space in the middle
	 whitespace at end__
	 unchanged line
	 CR at end
	EOF
	git diff -b >out &&
	test_cmp expect out &&

	git diff -b --ignore-space-at-eol >out &&
	test_cmp expect out &&

	tr "Q_" "\015 " <<-\EOF >expect &&
	diff --git a/x b/x
	index d99af23..22d9f73 100644
	--- a/x
	+++ b/x
	@@ -1,6 +1,6 @@
	-whitespace at beginning
	-whitespace change
	-whitespace in the middle
	+_	whitespace at beginning
	+whitespace 	 change
	+white space in the middle
	 whitespace at end__
	 unchanged line
	 CR at end
	EOF
	git diff --ignore-space-at-eol >out &&
	test_cmp expect out
'

test_expect_success 'ignore-blank-lines: only new lines' '
	test_seq 5 >x &&
	git update-index x &&
	test_seq 5 | sed "/3/i\\
" >x &&
	git diff --ignore-blank-lines >out &&
	>expect &&
	test_cmp out expect
'

test_expect_success 'ignore-blank-lines: only new lines with space' '
	test_seq 5 >x &&
	git update-index x &&
	test_seq 5 | sed "/3/i\\
 " >x &&
	git diff -w --ignore-blank-lines >out &&
	>expect &&
	test_cmp out expect
'

test_expect_success 'ignore-blank-lines: after change' '
	cat <<-\EOF >x &&
	1
	2

	3
	4
	5

	6
	7
	EOF
	git update-index x &&
	cat <<-\EOF >x &&
	change

	1
	2
	3
	4
	5
	6

	7
	EOF
	git diff --inter-hunk-context=100 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --git a/x b/x
	--- a/x
	+++ b/x
	@@ -1,6 +1,7 @@
	+change
	+
	 1
	 2
	-
	 3
	 4
	 5
	EOF
	compare_diff_patch expected out.tmp
'

test_expect_success 'ignore-blank-lines: before change' '
	cat <<-\EOF >x &&
	1
	2

	3
	4
	5
	6
	7
	EOF
	git update-index x &&
	cat <<-\EOF >x &&

	1
	2
	3
	4
	5

	6
	7
	change
	EOF
	git diff --inter-hunk-context=100 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --git a/x b/x
	--- a/x
	+++ b/x
	@@ -4,5 +4,7 @@
	 3
	 4
	 5
	+
	 6
	 7
	+change
	EOF
	compare_diff_patch expected out.tmp
'

test_expect_success 'ignore-blank-lines: between changes' '
	cat <<-\EOF >x &&
	1
	2
	3
	4
	5


	6
	7
	8
	9
	10
	EOF
	git update-index x &&
	cat <<-\EOF >x &&
	change
	1
	2

	3
	4
	5
	6
	7
	8

	9
	10
	change
	EOF
	git diff --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --git a/x b/x
	--- a/x
	+++ b/x
	@@ -1,5 +1,7 @@
	+change
	 1
	 2
	+
	 3
	 4
	 5
	@@ -8,5 +8,7 @@
	 6
	 7
	 8
	+
	 9
	 10
	+change
	EOF
	compare_diff_patch expected out.tmp
'

test_expect_success 'ignore-blank-lines: between changes (with interhunkctx)' '
	test_seq 10 >x &&
	git update-index x &&
	cat <<-\EOF >x &&
	change
	1
	2

	3
	4
	5

	6
	7
	8
	9

	10
	change
	EOF
	git diff --inter-hunk-context=2 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --git a/x b/x
	--- a/x
	+++ b/x
	@@ -1,10 +1,15 @@
	+change
	 1
	 2
	+
	 3
	 4
	 5
	+
	 6
	 7
	 8
	 9
	+
	 10
	+change
	EOF
	compare_diff_patch expected out.tmp
'

test_expect_success 'ignore-blank-lines: scattered spaces' '
	test_seq 10 >x &&
	git update-index x &&
	cat <<-\EOF >x &&
	change
	1
	2
	3

	4

	5

	6


	7

	8
	9
	10
	change
	EOF
	git diff --inter-hunk-context=4 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --git a/x b/x
	--- a/x
	+++ b/x
	@@ -1,3 +1,4 @@
	+change
	 1
	 2
	 3
	@@ -8,3 +15,4 @@
	 8
	 9
	 10
	+change
	EOF
	compare_diff_patch expected out.tmp
'

test_expect_success 'ignore-blank-lines: spaces coalesce' '
	test_seq 6 >x &&
	git update-index x &&
	cat <<-\EOF >x &&
	change
	1
	2
	3

	4

	5

	6
	change
	EOF
	git diff --inter-hunk-context=4 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --git a/x b/x
	--- a/x
	+++ b/x
	@@ -1,6 +1,11 @@
	+change
	 1
	 2
	 3
	+
	 4
	+
	 5
	+
	 6
	+change
	EOF
	compare_diff_patch expected out.tmp
'

test_expect_success 'ignore-blank-lines: mix changes and blank lines' '
	test_seq 16 >x &&
	git update-index x &&
	cat <<-\EOF >x &&
	change
	1
	2

	3
	4
	5
	change
	6
	7
	8

	9
	10
	11
	change
	12
	13
	14

	15
	16
	change
	EOF
	git diff --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --git a/x b/x
	--- a/x
	+++ b/x
	@@ -1,8 +1,11 @@
	+change
	 1
	 2
	+
	 3
	 4
	 5
	+change
	 6
	 7
	 8
	@@ -9,8 +13,11 @@
	 9
	 10
	 11
	+change
	 12
	 13
	 14
	+
	 15
	 16
	+change
	EOF
	compare_diff_patch expected out.tmp
'

test_expect_success 'check mixed spaces and tabs in indent' '
	# This is indented with SP HT SP.
	echo " 	 foo();" >x &&
	git diff --check | grep "space before tab in indent"
'

test_expect_success 'check mixed tabs and spaces in indent' '
	# This is indented with HT SP HT.
	echo "	 	foo();" >x &&
	git diff --check | grep "space before tab in indent"
'

test_expect_success 'check with no whitespace errors' '
	git commit -m "snapshot" &&
	echo "foo();" >x &&
	git diff --check
'

test_expect_success 'check with trailing whitespace' '
	echo "foo(); " >x &&
	test_must_fail git diff --check
'

test_expect_success 'check with space before tab in indent' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
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
	echo "foo();" >x &&
	git add x &&
	git diff --cached --check
'

test_expect_success 'check staged with trailing whitespace' '
	echo "foo(); " >x &&
	git add x &&
	test_must_fail git diff --cached --check
'

test_expect_success 'check staged with space before tab in indent' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	git add x &&
	test_must_fail git diff --cached --check
'

test_expect_success 'check with no whitespace errors (diff-index)' '
	echo "foo();" >x &&
	git add x &&
	git diff-index --check HEAD
'

test_expect_success 'check with trailing whitespace (diff-index)' '
	echo "foo(); " >x &&
	git add x &&
	test_must_fail git diff-index --check HEAD
'

test_expect_success 'check with space before tab in indent (diff-index)' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	git add x &&
	test_must_fail git diff-index --check HEAD
'

test_expect_success 'check staged with no whitespace errors (diff-index)' '
	echo "foo();" >x &&
	git add x &&
	git diff-index --cached --check HEAD
'

test_expect_success 'check staged with trailing whitespace (diff-index)' '
	echo "foo(); " >x &&
	git add x &&
	test_must_fail git diff-index --cached --check HEAD
'

test_expect_success 'check staged with space before tab in indent (diff-index)' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	git add x &&
	test_must_fail git diff-index --cached --check HEAD
'

test_expect_success 'check with no whitespace errors (diff-tree)' '
	echo "foo();" >x &&
	git commit -m "new commit" x &&
	git diff-tree --check HEAD^ HEAD
'

test_expect_success 'check with trailing whitespace (diff-tree)' '
	echo "foo(); " >x &&
	git commit -m "another commit" x &&
	test_must_fail git diff-tree --check HEAD^ HEAD
'

test_expect_success 'check with space before tab in indent (diff-tree)' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	git commit -m "yet another" x &&
	test_must_fail git diff-tree --check HEAD^ HEAD
'

test_expect_success 'check trailing whitespace (trailing-space: off)' '
	git config core.whitespace "-trailing-space" &&
	echo "foo ();   " >x &&
	git diff --check
'

test_expect_success 'check trailing whitespace (trailing-space: on)' '
	git config core.whitespace "trailing-space" &&
	echo "foo ();   " >x &&
	test_must_fail git diff --check
'

test_expect_success 'check space before tab in indent (space-before-tab: off)' '
	# indent contains space followed by HT
	git config core.whitespace "-space-before-tab" &&
	echo " 	foo ();" >x &&
	git diff --check
'

test_expect_success 'check space before tab in indent (space-before-tab: on)' '
	# indent contains space followed by HT
	git config core.whitespace "space-before-tab" &&
	echo " 	foo ();   " >x &&
	test_must_fail git diff --check
'

test_expect_success 'check spaces as indentation (indent-with-non-tab: off)' '
	git config core.whitespace "-indent-with-non-tab" &&
	echo "        foo ();" >x &&
	git diff --check
'

test_expect_success 'check spaces as indentation (indent-with-non-tab: on)' '
	git config core.whitespace "indent-with-non-tab" &&
	echo "        foo ();" >x &&
	test_must_fail git diff --check
'

test_expect_success 'ditto, but tabwidth=9' '
	git config core.whitespace "indent-with-non-tab,tabwidth=9" &&
	git diff --check
'

test_expect_success 'check tabs and spaces as indentation (indent-with-non-tab: on)' '
	git config core.whitespace "indent-with-non-tab" &&
	echo "	                foo ();" >x &&
	test_must_fail git diff --check
'

test_expect_success 'ditto, but tabwidth=10' '
	git config core.whitespace "indent-with-non-tab,tabwidth=10" &&
	test_must_fail git diff --check
'

test_expect_success 'ditto, but tabwidth=20' '
	git config core.whitespace "indent-with-non-tab,tabwidth=20" &&
	git diff --check
'

test_expect_success 'check tabs as indentation (tab-in-indent: off)' '
	git config core.whitespace "-tab-in-indent" &&
	echo "	foo ();" >x &&
	git diff --check
'

test_expect_success 'check tabs as indentation (tab-in-indent: on)' '
	git config core.whitespace "tab-in-indent" &&
	echo "	foo ();" >x &&
	test_must_fail git diff --check
'

test_expect_success 'check tabs and spaces as indentation (tab-in-indent: on)' '
	git config core.whitespace "tab-in-indent" &&
	echo "	                foo ();" >x &&
	test_must_fail git diff --check
'

test_expect_success 'ditto, but tabwidth=1 (must be irrelevant)' '
	git config core.whitespace "tab-in-indent,tabwidth=1" &&
	test_must_fail git diff --check
'

test_expect_success 'check tab-in-indent and indent-with-non-tab conflict' '
	git config core.whitespace "tab-in-indent,indent-with-non-tab" &&
	echo "foo ();" >x &&
	test_must_fail git diff --check
'

test_expect_success 'check tab-in-indent excluded from wildcard whitespace attribute' '
	git config --unset core.whitespace &&
	echo "x whitespace" >.gitattributes &&
	echo "	  foo ();" >x &&
	git diff --check &&
	rm -f .gitattributes
'

test_expect_success 'line numbers in --check output are correct' '
	echo "" >x &&
	echo "foo(); " >>x &&
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

# Start testing the colored format for whitespace checks

test_expect_success 'setup diff colors' '
	git config color.diff always &&
	git config color.diff.plain normal &&
	git config color.diff.meta bold &&
	git config color.diff.frag cyan &&
	git config color.diff.func normal &&
	git config color.diff.old red &&
	git config color.diff.new green &&
	git config color.diff.commit yellow &&
	git config color.diff.whitespace blue &&

	git config core.autocrlf false
'

test_expect_success 'diff that introduces a line with only tabs' '
	git config core.whitespace blank-at-eol &&
	git reset --hard &&
	echo "test" >x &&
	git commit -m "initial" x &&
	echo "{NTN}" | tr "NT" "\n\t" >>x &&
	git -c color.diff=always diff | test_decode_color >current &&

	cat >expected <<-\EOF &&
	<BOLD>diff --git a/x b/x<RESET>
	<BOLD>index 9daeafb..2874b91 100644<RESET>
	<BOLD>--- a/x<RESET>
	<BOLD>+++ b/x<RESET>
	<CYAN>@@ -1 +1,4 @@<RESET>
	 test<RESET>
	<GREEN>+<RESET><GREEN>{<RESET>
	<GREEN>+<RESET><BLUE>	<RESET>
	<GREEN>+<RESET><GREEN>}<RESET>
	EOF

	test_cmp expected current
'

test_expect_success 'diff that introduces and removes ws breakages' '
	git reset --hard &&
	{
		echo "0. blank-at-eol " &&
		echo "1. blank-at-eol "
	} >x &&
	git commit -a --allow-empty -m preimage &&
	{
		echo "0. blank-at-eol " &&
		echo "1. still-blank-at-eol " &&
		echo "2. and a new line "
	} >x &&

	git -c color.diff=always diff |
	test_decode_color >current &&

	cat >expected <<-\EOF &&
	<BOLD>diff --git a/x b/x<RESET>
	<BOLD>index d0233a2..700886e 100644<RESET>
	<BOLD>--- a/x<RESET>
	<BOLD>+++ b/x<RESET>
	<CYAN>@@ -1,2 +1,3 @@<RESET>
	 0. blank-at-eol <RESET>
	<RED>-1. blank-at-eol <RESET>
	<GREEN>+<RESET><GREEN>1. still-blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>2. and a new line<RESET><BLUE> <RESET>
	EOF

	test_cmp expected current
'

test_expect_success 'ws-error-highlight test setup' '

	git reset --hard &&
	{
		echo "0. blank-at-eol " &&
		echo "1. blank-at-eol "
	} >x &&
	git commit -a --allow-empty -m preimage &&
	{
		echo "0. blank-at-eol " &&
		echo "1. still-blank-at-eol " &&
		echo "2. and a new line "
	} >x &&

	cat >expect.default-old <<-\EOF &&
	<BOLD>diff --git a/x b/x<RESET>
	<BOLD>index d0233a2..700886e 100644<RESET>
	<BOLD>--- a/x<RESET>
	<BOLD>+++ b/x<RESET>
	<CYAN>@@ -1,2 +1,3 @@<RESET>
	 0. blank-at-eol <RESET>
	<RED>-<RESET><RED>1. blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>1. still-blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>2. and a new line<RESET><BLUE> <RESET>
	EOF

	cat >expect.all <<-\EOF &&
	<BOLD>diff --git a/x b/x<RESET>
	<BOLD>index d0233a2..700886e 100644<RESET>
	<BOLD>--- a/x<RESET>
	<BOLD>+++ b/x<RESET>
	<CYAN>@@ -1,2 +1,3 @@<RESET>
	 <RESET>0. blank-at-eol<RESET><BLUE> <RESET>
	<RED>-<RESET><RED>1. blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>1. still-blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>2. and a new line<RESET><BLUE> <RESET>
	EOF

	cat >expect.none <<-\EOF
	<BOLD>diff --git a/x b/x<RESET>
	<BOLD>index d0233a2..700886e 100644<RESET>
	<BOLD>--- a/x<RESET>
	<BOLD>+++ b/x<RESET>
	<CYAN>@@ -1,2 +1,3 @@<RESET>
	 0. blank-at-eol <RESET>
	<RED>-1. blank-at-eol <RESET>
	<GREEN>+1. still-blank-at-eol <RESET>
	<GREEN>+2. and a new line <RESET>
	EOF

'

test_expect_success 'test --ws-error-highlight option' '

	git -c color.diff=always diff --ws-error-highlight=default,old |
	test_decode_color >current &&
	test_cmp expect.default-old current &&

	git -c color.diff=always diff --ws-error-highlight=all |
	test_decode_color >current &&
	test_cmp expect.all current &&

	git -c color.diff=always diff --ws-error-highlight=none |
	test_decode_color >current &&
	test_cmp expect.none current

'

test_expect_success 'test diff.wsErrorHighlight config' '

	git -c color.diff=always -c diff.wsErrorHighlight=default,old diff |
	test_decode_color >current &&
	test_cmp expect.default-old current &&

	git -c color.diff=always -c diff.wsErrorHighlight=all diff |
	test_decode_color >current &&
	test_cmp expect.all current &&

	git -c color.diff=always -c diff.wsErrorHighlight=none diff |
	test_decode_color >current &&
	test_cmp expect.none current

'

test_expect_success 'option overrides diff.wsErrorHighlight' '

	git -c color.diff=always -c diff.wsErrorHighlight=none \
		diff --ws-error-highlight=default,old |
	test_decode_color >current &&
	test_cmp expect.default-old current &&

	git -c color.diff=always -c diff.wsErrorHighlight=default \
		diff --ws-error-highlight=all |
	test_decode_color >current &&
	test_cmp expect.all current &&

	git -c color.diff=always -c diff.wsErrorHighlight=all \
		diff --ws-error-highlight=none |
	test_decode_color >current &&
	test_cmp expect.none current

'

test_expect_success 'detect moved code, complete file' '
	git reset --hard &&
	cat <<-\EOF >test.c &&
	#include<stdio.h>
	main()
	{
	printf("Hello World");
	}
	EOF
	git add test.c &&
	git commit -m "add main function" &&
	git mv test.c main.c &&
	test_config color.diff.oldMoved "normal red" &&
	test_config color.diff.newMoved "normal green" &&
	git diff HEAD --color-moved --no-renames | test_decode_color >actual &&
	cat >expected <<-\EOF &&
	<BOLD>diff --git a/main.c b/main.c<RESET>
	<BOLD>new file mode 100644<RESET>
	<BOLD>index 0000000..a986c57<RESET>
	<BOLD>--- /dev/null<RESET>
	<BOLD>+++ b/main.c<RESET>
	<CYAN>@@ -0,0 +1,5 @@<RESET>
	<BGREEN>+<RESET><BGREEN>#include<stdio.h><RESET>
	<BGREEN>+<RESET><BGREEN>main()<RESET>
	<BGREEN>+<RESET><BGREEN>{<RESET>
	<BGREEN>+<RESET><BGREEN>printf("Hello World");<RESET>
	<BGREEN>+<RESET><BGREEN>}<RESET>
	<BOLD>diff --git a/test.c b/test.c<RESET>
	<BOLD>deleted file mode 100644<RESET>
	<BOLD>index a986c57..0000000<RESET>
	<BOLD>--- a/test.c<RESET>
	<BOLD>+++ /dev/null<RESET>
	<CYAN>@@ -1,5 +0,0 @@<RESET>
	<BRED>-#include<stdio.h><RESET>
	<BRED>-main()<RESET>
	<BRED>-{<RESET>
	<BRED>-printf("Hello World");<RESET>
	<BRED>-}<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success 'detect moved code, inside file' '
	git reset --hard &&
	cat <<-\EOF >main.c &&
		#include<stdio.h>
		int stuff()
		{
			printf("Hello ");
			printf("World\n");
		}

		int secure_foo(struct user *u)
		{
			if (!u->is_allowed_foo)
				return;
			foo(u);
		}

		int main()
		{
			foo();
		}
	EOF
	cat <<-\EOF >test.c &&
		#include<stdio.h>
		int bar()
		{
			printf("Hello World, but different\n");
		}

		int another_function()
		{
			bar();
		}
	EOF
	git add main.c test.c &&
	git commit -m "add main and test file" &&
	cat <<-\EOF >main.c &&
		#include<stdio.h>
		int stuff()
		{
			printf("Hello ");
			printf("World\n");
		}

		int main()
		{
			foo();
		}
	EOF
	cat <<-\EOF >test.c &&
		#include<stdio.h>
		int bar()
		{
			printf("Hello World, but different\n");
		}

		int secure_foo(struct user *u)
		{
			if (!u->is_allowed_foo)
				return;
			foo(u);
		}

		int another_function()
		{
			bar();
		}
	EOF
	test_config color.diff.oldMoved "normal red" &&
	test_config color.diff.newMoved "normal green" &&
	test_config color.diff.oldMovedAlternative "bold red" &&
	test_config color.diff.newMovedAlternative "bold green" &&
	git diff HEAD --no-renames --color-moved| test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --git a/main.c b/main.c<RESET>
	<BOLD>index 27a619c..7cf9336 100644<RESET>
	<BOLD>--- a/main.c<RESET>
	<BOLD>+++ b/main.c<RESET>
	<CYAN>@@ -5,13 +5,6 @@<RESET> <RESET>printf("Hello ");<RESET>
	 printf("World\n");<RESET>
	 }<RESET>
	 <RESET>
	<BRED>-int secure_foo(struct user *u)<RESET>
	<BRED>-{<RESET>
	<BRED>-if (!u->is_allowed_foo)<RESET>
	<BRED>-return;<RESET>
	<BRED>-foo(u);<RESET>
	<BRED>-}<RESET>
	<BRED>-<RESET>
	 int main()<RESET>
	 {<RESET>
	 foo();<RESET>
	<BOLD>diff --git a/test.c b/test.c<RESET>
	<BOLD>index 1dc1d85..e34eb69 100644<RESET>
	<BOLD>--- a/test.c<RESET>
	<BOLD>+++ b/test.c<RESET>
	<CYAN>@@ -4,6 +4,13 @@<RESET> <RESET>int bar()<RESET>
	 printf("Hello World, but different\n");<RESET>
	 }<RESET>
	 <RESET>
	<BGREEN>+<RESET><BGREEN>int secure_foo(struct user *u)<RESET>
	<BGREEN>+<RESET><BGREEN>{<RESET>
	<BGREEN>+<RESET><BGREEN>if (!u->is_allowed_foo)<RESET>
	<BGREEN>+<RESET><BGREEN>return;<RESET>
	<BGREEN>+<RESET><BGREEN>foo(u);<RESET>
	<BGREEN>+<RESET><BGREEN>}<RESET>
	<BGREEN>+<RESET>
	 int another_function()<RESET>
	 {<RESET>
	 bar();<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success 'detect permutations inside moved code' '
	git reset --hard &&
	cat <<-\EOF >lines.txt &&
		line 1
		line 2
		line 3
		line 4
		line 5
		line 6
		line 7
		line 8
		line 9
		line 10
		line 11
		line 12
		line 13
		line 14
		line 15
		line 16
	EOF
	git add lines.txt &&
	git commit -m "add poetry" &&
	cat <<-\EOF >lines.txt &&
		line 4
		line 5
		line 6
		line 7
		line 8
		line 9
		line 1
		line 2
		line 3
		line 14
		line 15
		line 16
		line 10
		line 11
		line 12
		line 13
	EOF
	test_config color.diff.oldMoved "magenta" &&
	test_config color.diff.newMoved "cyan" &&
	test_config color.diff.oldMovedAlternative "blue" &&
	test_config color.diff.newMovedAlternative "yellow" &&


	git diff HEAD --no-renames --color-moved=nobounds| test_decode_color >actual &&
	cat <<-\EOF >expected &&
		<BOLD>diff --git a/lines.txt b/lines.txt<RESET>
		<BOLD>index 47ea9c3..ba96a38 100644<RESET>
		<BOLD>--- a/lines.txt<RESET>
		<BOLD>+++ b/lines.txt<RESET>
		<CYAN>@@ -1,16 +1,16 @@<RESET>
		<MAGENTA>-line 1<RESET>
		<MAGENTA>-line 2<RESET>
		<MAGENTA>-line 3<RESET>
		 line 4<RESET>
		 line 5<RESET>
		 line 6<RESET>
		 line 7<RESET>
		 line 8<RESET>
		 line 9<RESET>
		<CYAN>+<RESET><CYAN>line 1<RESET>
		<CYAN>+<RESET><CYAN>line 2<RESET>
		<CYAN>+<RESET><CYAN>line 3<RESET>
		<CYAN>+<RESET><CYAN>line 14<RESET>
		<CYAN>+<RESET><CYAN>line 15<RESET>
		<CYAN>+<RESET><CYAN>line 16<RESET>
		 line 10<RESET>
		 line 11<RESET>
		 line 12<RESET>
		 line 13<RESET>
		<MAGENTA>-line 14<RESET>
		<MAGENTA>-line 15<RESET>
		<MAGENTA>-line 16<RESET>
	EOF
	test_cmp expected actual &&

	git diff HEAD --no-renames --color-moved=adjacentbounds| test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --git a/lines.txt b/lines.txt<RESET>
	<BOLD>index 47ea9c3..ba96a38 100644<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,16 +1,16 @@<RESET>
	<MAGENTA>-line 1<RESET>
	<MAGENTA>-line 2<RESET>
	<MAGENTA>-line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	 line 6<RESET>
	 line 7<RESET>
	 line 8<RESET>
	 line 9<RESET>
	<CYAN>+<RESET><CYAN>line 1<RESET>
	<CYAN>+<RESET><CYAN>line 2<RESET>
	<YELLOW>+<RESET><YELLOW>line 3<RESET>
	<YELLOW>+<RESET><YELLOW>line 14<RESET>
	<CYAN>+<RESET><CYAN>line 15<RESET>
	<CYAN>+<RESET><CYAN>line 16<RESET>
	 line 10<RESET>
	 line 11<RESET>
	 line 12<RESET>
	 line 13<RESET>
	<MAGENTA>-line 14<RESET>
	<MAGENTA>-line 15<RESET>
	<MAGENTA>-line 16<RESET>
	EOF
	test_cmp expected actual &&

	test_config diff.colorMoved alternate &&
	git diff HEAD --no-renames --color-moved| test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --git a/lines.txt b/lines.txt<RESET>
	<BOLD>index 47ea9c3..ba96a38 100644<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,16 +1,16 @@<RESET>
	<MAGENTA>-line 1<RESET>
	<MAGENTA>-line 2<RESET>
	<MAGENTA>-line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	 line 6<RESET>
	 line 7<RESET>
	 line 8<RESET>
	 line 9<RESET>
	<CYAN>+<RESET><CYAN>line 1<RESET>
	<CYAN>+<RESET><CYAN>line 2<RESET>
	<CYAN>+<RESET><CYAN>line 3<RESET>
	<YELLOW>+<RESET><YELLOW>line 14<RESET>
	<YELLOW>+<RESET><YELLOW>line 15<RESET>
	<YELLOW>+<RESET><YELLOW>line 16<RESET>
	 line 10<RESET>
	 line 11<RESET>
	 line 12<RESET>
	 line 13<RESET>
	<MAGENTA>-line 14<RESET>
	<MAGENTA>-line 15<RESET>
	<MAGENTA>-line 16<RESET>
	EOF
	test_cmp expected actual &&

	test_config diff.colorMoved allbounds &&
	git diff HEAD --no-renames --color-moved| test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --git a/lines.txt b/lines.txt<RESET>
	<BOLD>index 47ea9c3..ba96a38 100644<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,16 +1,16 @@<RESET>
	<BLUE>-line 1<RESET>
	<MAGENTA>-line 2<RESET>
	<BLUE>-line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	 line 6<RESET>
	 line 7<RESET>
	 line 8<RESET>
	 line 9<RESET>
	<YELLOW>+<RESET><YELLOW>line 1<RESET>
	<CYAN>+<RESET><CYAN>line 2<RESET>
	<YELLOW>+<RESET><YELLOW>line 3<RESET>
	<YELLOW>+<RESET><YELLOW>line 14<RESET>
	<CYAN>+<RESET><CYAN>line 15<RESET>
	<YELLOW>+<RESET><YELLOW>line 16<RESET>
	 line 10<RESET>
	 line 11<RESET>
	 line 12<RESET>
	 line 13<RESET>
	<BLUE>-line 14<RESET>
	<MAGENTA>-line 15<RESET>
	<BLUE>-line 16<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'move detection does not mess up colored words' '
	cat <<-\EOF >text.txt &&
	Lorem Ipsum is simply dummy text of the printing and typesetting industry.
	EOF
	git add text.txt &&
	git commit -a -m "clean state" &&
	cat <<-\EOF >text.txt &&
	simply Lorem Ipsum dummy is text of the typesetting and printing industry.
	EOF
	git diff --color-moved --word-diff >actual &&
	git diff --word-diff >expect &&
	test_cmp expect actual
'

test_expect_success 'move detection with submodules' '
	test_create_repo bananas &&
	echo ripe >bananas/recipe &&
	git -C bananas add recipe &&
	test_commit fruit &&
	test_commit -C bananas recipe &&
	git submodule add ./bananas &&
	git add bananas &&
	git commit -a -m "bananas are like a heavy library?" &&
	echo foul >bananas/recipe &&
	echo ripe >fruit.t &&

	git diff --submodule=diff --color-moved >actual &&

	# no move detection as the moved line is across repository boundaries.
	test_decode_color <actual >decoded_actual &&
	! grep BGREEN decoded_actual &&
	! grep BRED decoded_actual &&

	# nor did we mess with it another way
	git diff --submodule=diff | test_decode_color >expect &&
	test_cmp expect decoded_actual
'

test_done
