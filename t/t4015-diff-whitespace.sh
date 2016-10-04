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

test_done
