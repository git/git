#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin
#

test_description='Test special whitespace in diff engine.

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

test_expect_success "Ray Lehtiniemi's example" '
	cat <<-\EOF >x &&
	do {
	   nothing;
	} while (0);
	EOF
	but update-index --add x &&
	old_hash_x=$(but hash-object x) &&
	before=$(but rev-parse --short "$old_hash_x") &&

	cat <<-\EOF >x &&
	do
	{
	   nothing;
	}
	while (0);
	EOF
	new_hash_x=$(but hash-object x) &&
	after=$(but rev-parse --short "$new_hash_x") &&

	cat <<-EOF >expect &&
	diff --but a/x b/x
	index $before..$after 100644
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

	but diff >out &&
	test_cmp expect out &&

	but diff -w >out &&
	test_cmp expect out &&

	but diff -b >out &&
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

	but update-index x &&
	old_hash_x=$(but hash-object x) &&
	before=$(but rev-parse --short "$old_hash_x") &&

	tr "_" " " <<-\EOF >x &&
	_	whitespace at beginning
	whitespace 	 change
	white space in the middle
	whitespace at end__
	unchanged line
	CR at end
	EOF
	new_hash_x=$(but hash-object x) &&
	after=$(but rev-parse --short "$new_hash_x") &&

	tr "Q_" "\015 " <<-EOF >expect &&
	diff --but a/x b/x
	index $before..$after 100644
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

	but diff >out &&
	test_cmp expect out &&

	but diff -w >out &&
	test_must_be_empty out &&

	but diff -w -b >out &&
	test_must_be_empty out &&

	but diff -w --ignore-space-at-eol >out &&
	test_must_be_empty out &&

	but diff -w -b --ignore-space-at-eol >out &&
	test_must_be_empty out &&

	but diff -w --ignore-cr-at-eol >out &&
	test_must_be_empty out &&

	tr "Q_" "\015 " <<-EOF >expect &&
	diff --but a/x b/x
	index $before..$after 100644
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
	but diff -b >out &&
	test_cmp expect out &&

	but diff -b --ignore-space-at-eol >out &&
	test_cmp expect out &&

	but diff -b --ignore-cr-at-eol >out &&
	test_cmp expect out &&

	tr "Q_" "\015 " <<-EOF >expect &&
	diff --but a/x b/x
	index $before..$after 100644
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
	but diff --ignore-space-at-eol >out &&
	test_cmp expect out &&

	but diff --ignore-space-at-eol --ignore-cr-at-eol >out &&
	test_cmp expect out &&

	tr "Q_" "\015 " <<-EOF >expect &&
	diff --but a/x b/x
	index_$before..$after 100644
	--- a/x
	+++ b/x
	@@ -1,6 +1,6 @@
	-whitespace at beginning
	-whitespace change
	-whitespace in the middle
	-whitespace at end
	+_	whitespace at beginning
	+whitespace_	_change
	+white space in the middle
	+whitespace at end__
	 unchanged line
	 CR at end
	EOF
	but diff --ignore-cr-at-eol >out &&
	test_cmp expect out
'

test_expect_success 'ignore-blank-lines: only new lines' '
	test_seq 5 >x &&
	but update-index x &&
	test_seq 5 | sed "/3/i\\
" >x &&
	but diff --ignore-blank-lines >out &&
	test_must_be_empty out
'

test_expect_success 'ignore-blank-lines: only new lines with space' '
	test_seq 5 >x &&
	but update-index x &&
	test_seq 5 | sed "/3/i\\
 " >x &&
	but diff -w --ignore-blank-lines >out &&
	test_must_be_empty out
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
	but update-index x &&
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
	but diff --inter-hunk-context=100 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --but a/x b/x
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
	but update-index x &&
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
	but diff --inter-hunk-context=100 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --but a/x b/x
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
	but update-index x &&
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
	but diff --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --but a/x b/x
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
	but update-index x &&
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
	but diff --inter-hunk-context=2 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --but a/x b/x
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
	but update-index x &&
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
	but diff --inter-hunk-context=4 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --but a/x b/x
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
	but update-index x &&
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
	but diff --inter-hunk-context=4 --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --but a/x b/x
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
	but update-index x &&
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
	but diff --ignore-blank-lines >out.tmp &&
	cat <<-\EOF >expected &&
	diff --but a/x b/x
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
	test_must_fail but diff --check >check &&
	grep "space before tab in indent" check
'

test_expect_success 'check mixed tabs and spaces in indent' '
	# This is indented with HT SP HT.
	echo "	 	foo();" >x &&
	test_must_fail but diff --check >check &&
	grep "space before tab in indent" check
'

test_expect_success 'check with no whitespace errors' '
	but cummit -m "snapshot" &&
	echo "foo();" >x &&
	but diff --check
'

test_expect_success 'check with trailing whitespace' '
	echo "foo(); " >x &&
	test_must_fail but diff --check
'

test_expect_success 'check with space before tab in indent' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	test_must_fail but diff --check
'

test_expect_success '--check and --exit-code are not exclusive' '
	but checkout x &&
	but diff --check --exit-code
'

test_expect_success '--check and --quiet are not exclusive' '
	but diff --check --quiet
'

test_expect_success '-w and --exit-code interact sensibly' '
	test_when_finished "but checkout x" &&
	{
		test_seq 15 &&
		echo " 16"
	} >x &&
	test_must_fail but diff --exit-code &&
	but diff -w >actual &&
	test_must_be_empty actual &&
	but diff -w --exit-code
'

test_expect_success '-I and --exit-code interact sensibly' '
	test_when_finished "but checkout x" &&
	{
		test_seq 15 &&
		echo " 16"
	} >x &&
	test_must_fail but diff --exit-code &&
	but diff -I. >actual &&
	test_must_be_empty actual &&
	but diff -I. --exit-code
'

test_expect_success 'check staged with no whitespace errors' '
	echo "foo();" >x &&
	but add x &&
	but diff --cached --check
'

test_expect_success 'check staged with trailing whitespace' '
	echo "foo(); " >x &&
	but add x &&
	test_must_fail but diff --cached --check
'

test_expect_success 'check staged with space before tab in indent' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	but add x &&
	test_must_fail but diff --cached --check
'

test_expect_success 'check with no whitespace errors (diff-index)' '
	echo "foo();" >x &&
	but add x &&
	but diff-index --check HEAD
'

test_expect_success 'check with trailing whitespace (diff-index)' '
	echo "foo(); " >x &&
	but add x &&
	test_must_fail but diff-index --check HEAD
'

test_expect_success 'check with space before tab in indent (diff-index)' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	but add x &&
	test_must_fail but diff-index --check HEAD
'

test_expect_success 'check staged with no whitespace errors (diff-index)' '
	echo "foo();" >x &&
	but add x &&
	but diff-index --cached --check HEAD
'

test_expect_success 'check staged with trailing whitespace (diff-index)' '
	echo "foo(); " >x &&
	but add x &&
	test_must_fail but diff-index --cached --check HEAD
'

test_expect_success 'check staged with space before tab in indent (diff-index)' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	but add x &&
	test_must_fail but diff-index --cached --check HEAD
'

test_expect_success 'check with no whitespace errors (diff-tree)' '
	echo "foo();" >x &&
	but cummit -m "new cummit" x &&
	but diff-tree --check HEAD^ HEAD
'

test_expect_success 'check with trailing whitespace (diff-tree)' '
	echo "foo(); " >x &&
	but cummit -m "another cummit" x &&
	test_must_fail but diff-tree --check HEAD^ HEAD
'

test_expect_success 'check with space before tab in indent (diff-tree)' '
	# indent has space followed by hard tab
	echo " 	foo();" >x &&
	but cummit -m "yet another" x &&
	test_must_fail but diff-tree --check HEAD^ HEAD
'

test_expect_success 'check with ignored trailing whitespace attr (diff-tree)' '
	test_when_finished "but reset --hard HEAD^" &&

	# create a whitespace error that should be ignored
	echo "* -whitespace" >.butattributes &&
	but add .butattributes &&
	echo "foo(); " >x &&
	but add x &&
	but cummit -m "add trailing space" &&

	# with a worktree diff-tree ignores the whitespace error
	but diff-tree --root --check HEAD &&

	# without a worktree diff-tree still ignores the whitespace error
	but -C .but diff-tree --root --check HEAD
'

test_expect_success 'check trailing whitespace (trailing-space: off)' '
	but config core.whitespace "-trailing-space" &&
	echo "foo ();   " >x &&
	but diff --check
'

test_expect_success 'check trailing whitespace (trailing-space: on)' '
	but config core.whitespace "trailing-space" &&
	echo "foo ();   " >x &&
	test_must_fail but diff --check
'

test_expect_success 'check space before tab in indent (space-before-tab: off)' '
	# indent contains space followed by HT
	but config core.whitespace "-space-before-tab" &&
	echo " 	foo ();" >x &&
	but diff --check
'

test_expect_success 'check space before tab in indent (space-before-tab: on)' '
	# indent contains space followed by HT
	but config core.whitespace "space-before-tab" &&
	echo " 	foo ();   " >x &&
	test_must_fail but diff --check
'

test_expect_success 'check spaces as indentation (indent-with-non-tab: off)' '
	but config core.whitespace "-indent-with-non-tab" &&
	echo "        foo ();" >x &&
	but diff --check
'

test_expect_success 'check spaces as indentation (indent-with-non-tab: on)' '
	but config core.whitespace "indent-with-non-tab" &&
	echo "        foo ();" >x &&
	test_must_fail but diff --check
'

test_expect_success 'ditto, but tabwidth=9' '
	but config core.whitespace "indent-with-non-tab,tabwidth=9" &&
	but diff --check
'

test_expect_success 'check tabs and spaces as indentation (indent-with-non-tab: on)' '
	but config core.whitespace "indent-with-non-tab" &&
	echo "	                foo ();" >x &&
	test_must_fail but diff --check
'

test_expect_success 'ditto, but tabwidth=10' '
	but config core.whitespace "indent-with-non-tab,tabwidth=10" &&
	test_must_fail but diff --check
'

test_expect_success 'ditto, but tabwidth=20' '
	but config core.whitespace "indent-with-non-tab,tabwidth=20" &&
	but diff --check
'

test_expect_success 'check tabs as indentation (tab-in-indent: off)' '
	but config core.whitespace "-tab-in-indent" &&
	echo "	foo ();" >x &&
	but diff --check
'

test_expect_success 'check tabs as indentation (tab-in-indent: on)' '
	but config core.whitespace "tab-in-indent" &&
	echo "	foo ();" >x &&
	test_must_fail but diff --check
'

test_expect_success 'check tabs and spaces as indentation (tab-in-indent: on)' '
	but config core.whitespace "tab-in-indent" &&
	echo "	                foo ();" >x &&
	test_must_fail but diff --check
'

test_expect_success 'ditto, but tabwidth=1 (must be irrelevant)' '
	but config core.whitespace "tab-in-indent,tabwidth=1" &&
	test_must_fail but diff --check
'

test_expect_success 'check tab-in-indent and indent-with-non-tab conflict' '
	but config core.whitespace "tab-in-indent,indent-with-non-tab" &&
	echo "foo ();" >x &&
	test_must_fail but diff --check
'

test_expect_success 'check tab-in-indent excluded from wildcard whitespace attribute' '
	but config --unset core.whitespace &&
	echo "x whitespace" >.butattributes &&
	echo "	  foo ();" >x &&
	but diff --check &&
	rm -f .butattributes
'

test_expect_success 'line numbers in --check output are correct' '
	echo "" >x &&
	echo "foo(); " >>x &&
	test_must_fail but diff --check >check &&
	grep "x:2:" check
'

test_expect_success 'checkdiff detects new trailing blank lines (1)' '
	echo "foo();" >x &&
	echo "" >>x &&
	test_must_fail but diff --check >check &&
	grep "new blank line" check
'

test_expect_success 'checkdiff detects new trailing blank lines (2)' '
	test_write_lines a b "" "" >x &&
	but add x &&
	test_write_lines a "" "" "" "" >x &&
	test_must_fail but diff --check >check &&
	grep "new blank line" check
'

test_expect_success 'checkdiff allows new blank lines' '
	but checkout x &&
	mv x y &&
	(
		echo "/* This is new */" &&
		echo "" &&
		cat y
	) >x &&
	but diff --check
'

test_expect_success 'whitespace-only changes not reported (diff)' '
	but reset --hard &&
	echo >x "hello world" &&
	but add x &&
	but cummit -m "hello 1" &&
	echo >x "hello  world" &&
	but diff -b >actual &&
	test_must_be_empty actual
'

test_expect_success 'whitespace-only changes not reported (diffstat)' '
	# reuse state from previous test
	but diff --stat -b >actual &&
	test_must_be_empty actual
'

test_expect_success 'whitespace changes with modification reported (diffstat)' '
	but reset --hard &&
	echo >x "hello  world" &&
	but update-index --chmod=+x x &&
	but diff --stat --cached -b >actual &&
	cat <<-EOF >expect &&
	 x | 0
	 1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	test_cmp expect actual
'

test_expect_success 'whitespace-only changes reported across renames (diffstat)' '
	but reset --hard &&
	for i in 1 2 3 4 5 6 7 8 9; do echo "$i$i$i$i$i$i" || return 1; done >x &&
	but add x &&
	but cummit -m "base" &&
	sed -e "5s/^/ /" x >z &&
	but rm x &&
	but add z &&
	but diff -w -M --cached --stat >actual &&
	cat <<-EOF >expect &&
	 x => z | 0
	 1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	test_cmp expect actual
'

test_expect_success 'whitespace-only changes reported across renames' '
	but reset --hard HEAD~1 &&
	for i in 1 2 3 4 5 6 7 8 9; do echo "$i$i$i$i$i$i" || return 1; done >x &&
	but add x &&
	hash_x=$(but hash-object x) &&
	before=$(but rev-parse --short "$hash_x") &&
	but cummit -m "base" &&
	sed -e "5s/^/ /" x >z &&
	but rm x &&
	but add z &&
	hash_z=$(but hash-object z) &&
	after=$(but rev-parse --short "$hash_z") &&
	but diff -w -M --cached >actual.raw &&
	sed -e "/^similarity index /s/[0-9][0-9]*/NUM/" actual.raw >actual &&
	cat <<-EOF >expect &&
	diff --but a/x b/z
	similarity index NUM%
	rename from x
	rename to z
	index $before..$after 100644
	EOF
	test_cmp expect actual
'

cat >expected <<\EOF
diff --but a/empty b/void
similarity index 100%
rename from empty
rename to void
EOF

test_expect_success 'rename empty' '
	but reset --hard &&
	>empty &&
	but add empty &&
	but cummit -m empty &&
	but mv empty void &&
	but diff -w --cached -M >current &&
	test_cmp expected current
'

test_expect_success 'combined diff with autocrlf conversion' '

	but reset --hard &&
	test_cummit "one side" x hello one-side &&
	but checkout HEAD^ &&
	echo >x goodbye &&
	but cummit -m "the other side" x &&
	but config core.autocrlf true &&
	test_must_fail but merge one-side >actual &&
	test_i18ngrep "Automatic merge failed" actual &&

	but diff >actual.raw &&
	sed -e "1,/^@@@/d" actual.raw >actual &&
	! grep "^-" actual

'

# Start testing the colored format for whitespace checks

test_expect_success 'setup diff colors' '
	but config color.diff.plain normal &&
	but config color.diff.meta bold &&
	but config color.diff.frag cyan &&
	but config color.diff.func normal &&
	but config color.diff.old red &&
	but config color.diff.new green &&
	but config color.diff.cummit yellow &&
	but config color.diff.whitespace blue &&

	but config core.autocrlf false
'

test_expect_success 'diff that introduces a line with only tabs' '
	but config core.whitespace blank-at-eol &&
	but reset --hard &&
	echo "test" >x &&
	old_hash_x=$(but hash-object x) &&
	before=$(but rev-parse --short "$old_hash_x") &&
	but cummit -m "initial" x &&
	echo "{NTN}" | tr "NT" "\n\t" >>x &&
	new_hash_x=$(but hash-object x) &&
	after=$(but rev-parse --short "$new_hash_x") &&
	but diff --color >current.raw &&
	test_decode_color <current.raw >current &&

	cat >expected <<-EOF &&
	<BOLD>diff --but a/x b/x<RESET>
	<BOLD>index $before..$after 100644<RESET>
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
	but reset --hard &&
	{
		echo "0. blank-at-eol " &&
		echo "1. blank-at-eol "
	} >x &&
	old_hash_x=$(but hash-object x) &&
	before=$(but rev-parse --short "$old_hash_x") &&
	but cummit -a --allow-empty -m preimage &&
	{
		echo "0. blank-at-eol " &&
		echo "1. still-blank-at-eol " &&
		echo "2. and a new line "
	} >x &&
	new_hash_x=$(but hash-object x) &&
	after=$(but rev-parse --short "$new_hash_x") &&

	but diff --color >current.raw &&
	test_decode_color <current.raw >current &&

	cat >expected <<-EOF &&
	<BOLD>diff --but a/x b/x<RESET>
	<BOLD>index $before..$after 100644<RESET>
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

	but reset --hard &&
	{
		echo "0. blank-at-eol " &&
		echo "1. blank-at-eol "
	} >x &&
	old_hash_x=$(but hash-object x) &&
	before=$(but rev-parse --short "$old_hash_x") &&
	but cummit -a --allow-empty -m preimage &&
	{
		echo "0. blank-at-eol " &&
		echo "1. still-blank-at-eol " &&
		echo "2. and a new line "
	} >x &&
	new_hash_x=$(but hash-object x) &&
	after=$(but rev-parse --short "$new_hash_x") &&

	cat >expect.default-old <<-EOF &&
	<BOLD>diff --but a/x b/x<RESET>
	<BOLD>index $before..$after 100644<RESET>
	<BOLD>--- a/x<RESET>
	<BOLD>+++ b/x<RESET>
	<CYAN>@@ -1,2 +1,3 @@<RESET>
	 0. blank-at-eol <RESET>
	<RED>-<RESET><RED>1. blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>1. still-blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>2. and a new line<RESET><BLUE> <RESET>
	EOF

	cat >expect.all <<-EOF &&
	<BOLD>diff --but a/x b/x<RESET>
	<BOLD>index $before..$after 100644<RESET>
	<BOLD>--- a/x<RESET>
	<BOLD>+++ b/x<RESET>
	<CYAN>@@ -1,2 +1,3 @@<RESET>
	 <RESET>0. blank-at-eol<RESET><BLUE> <RESET>
	<RED>-<RESET><RED>1. blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>1. still-blank-at-eol<RESET><BLUE> <RESET>
	<GREEN>+<RESET><GREEN>2. and a new line<RESET><BLUE> <RESET>
	EOF

	cat >expect.none <<-EOF
	<BOLD>diff --but a/x b/x<RESET>
	<BOLD>index $before..$after 100644<RESET>
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

	but diff --color --ws-error-highlight=default,old >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.default-old current &&

	but diff --color --ws-error-highlight=all >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.all current &&

	but diff --color --ws-error-highlight=none >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.none current

'

test_expect_success 'test diff.wsErrorHighlight config' '

	but -c diff.wsErrorHighlight=default,old diff --color >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.default-old current &&

	but -c diff.wsErrorHighlight=all diff --color >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.all current &&

	but -c diff.wsErrorHighlight=none diff --color >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.none current

'

test_expect_success 'option overrides diff.wsErrorHighlight' '

	but -c diff.wsErrorHighlight=none \
		diff --color --ws-error-highlight=default,old >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.default-old current &&

	but -c diff.wsErrorHighlight=default \
		diff --color --ws-error-highlight=all >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.all current &&

	but -c diff.wsErrorHighlight=all \
		diff --color --ws-error-highlight=none >current.raw &&
	test_decode_color <current.raw >current &&
	test_cmp expect.none current

'

test_expect_success 'detect moved code, complete file' '
	but reset --hard &&
	cat <<-\EOF >test.c &&
	#include<stdio.h>
	main()
	{
	printf("Hello World");
	}
	EOF
	but add test.c &&
	but cummit -m "add main function" &&
	file=$(but rev-parse --short HEAD:test.c) &&
	but mv test.c main.c &&
	test_config color.diff.oldMoved "normal red" &&
	test_config color.diff.newMoved "normal green" &&
	but diff HEAD --color-moved=zebra --color --no-renames >actual.raw &&
	test_decode_color <actual.raw >actual &&
	cat >expected <<-EOF &&
	<BOLD>diff --but a/main.c b/main.c<RESET>
	<BOLD>new file mode 100644<RESET>
	<BOLD>index 0000000..$file<RESET>
	<BOLD>--- /dev/null<RESET>
	<BOLD>+++ b/main.c<RESET>
	<CYAN>@@ -0,0 +1,5 @@<RESET>
	<BGREEN>+<RESET><BGREEN>#include<stdio.h><RESET>
	<BGREEN>+<RESET><BGREEN>main()<RESET>
	<BGREEN>+<RESET><BGREEN>{<RESET>
	<BGREEN>+<RESET><BGREEN>printf("Hello World");<RESET>
	<BGREEN>+<RESET><BGREEN>}<RESET>
	<BOLD>diff --but a/test.c b/test.c<RESET>
	<BOLD>deleted file mode 100644<RESET>
	<BOLD>index $file..0000000<RESET>
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

test_expect_success 'detect malicious moved code, inside file' '
	test_config color.diff.oldMoved "normal red" &&
	test_config color.diff.newMoved "normal green" &&
	test_config color.diff.oldMovedAlternative "blue" &&
	test_config color.diff.newMovedAlternative "yellow" &&
	but reset --hard &&
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
	but add main.c test.c &&
	but cummit -m "add main and test file" &&
	before_main=$(but rev-parse --short HEAD:main.c) &&
	before_test=$(but rev-parse --short HEAD:test.c) &&
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
			foo(u);
			if (!u->is_allowed_foo)
				return;
		}

		int another_function()
		{
			bar();
		}
	EOF
	hash_main=$(but hash-object main.c) &&
	after_main=$(but rev-parse --short "$hash_main") &&
	hash_test=$(but hash-object test.c) &&
	after_test=$(but rev-parse --short "$hash_test") &&
	but diff HEAD --no-renames --color-moved=zebra --color >actual.raw &&
	test_decode_color <actual.raw >actual &&
	cat <<-EOF >expected &&
	<BOLD>diff --but a/main.c b/main.c<RESET>
	<BOLD>index $before_main..$after_main 100644<RESET>
	<BOLD>--- a/main.c<RESET>
	<BOLD>+++ b/main.c<RESET>
	<CYAN>@@ -5,13 +5,6 @@<RESET> <RESET>printf("Hello ");<RESET>
	 printf("World\n");<RESET>
	 }<RESET>
	 <RESET>
	<BRED>-int secure_foo(struct user *u)<RESET>
	<BRED>-{<RESET>
	<BLUE>-if (!u->is_allowed_foo)<RESET>
	<BLUE>-return;<RESET>
	<RED>-foo(u);<RESET>
	<RED>-}<RESET>
	<RED>-<RESET>
	 int main()<RESET>
	 {<RESET>
	 foo();<RESET>
	<BOLD>diff --but a/test.c b/test.c<RESET>
	<BOLD>index $before_test..$after_test 100644<RESET>
	<BOLD>--- a/test.c<RESET>
	<BOLD>+++ b/test.c<RESET>
	<CYAN>@@ -4,6 +4,13 @@<RESET> <RESET>int bar()<RESET>
	 printf("Hello World, but different\n");<RESET>
	 }<RESET>
	 <RESET>
	<BGREEN>+<RESET><BGREEN>int secure_foo(struct user *u)<RESET>
	<BGREEN>+<RESET><BGREEN>{<RESET>
	<GREEN>+<RESET><GREEN>foo(u);<RESET>
	<BGREEN>+<RESET><BGREEN>if (!u->is_allowed_foo)<RESET>
	<BGREEN>+<RESET><BGREEN>return;<RESET>
	<GREEN>+<RESET><GREEN>}<RESET>
	<GREEN>+<RESET>
	 int another_function()<RESET>
	 {<RESET>
	 bar();<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success 'plain moved code, inside file' '
	test_config color.diff.oldMoved "normal red" &&
	test_config color.diff.newMoved "normal green" &&
	test_config color.diff.oldMovedAlternative "blue" &&
	test_config color.diff.newMovedAlternative "yellow" &&
	# needs previous test as setup
	but diff HEAD --no-renames --color-moved=plain --color >actual.raw &&
	test_decode_color <actual.raw >actual &&
	cat <<-EOF >expected &&
	<BOLD>diff --but a/main.c b/main.c<RESET>
	<BOLD>index $before_main..$after_main 100644<RESET>
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
	<BOLD>diff --but a/test.c b/test.c<RESET>
	<BOLD>index $before_test..$after_test 100644<RESET>
	<BOLD>--- a/test.c<RESET>
	<BOLD>+++ b/test.c<RESET>
	<CYAN>@@ -4,6 +4,13 @@<RESET> <RESET>int bar()<RESET>
	 printf("Hello World, but different\n");<RESET>
	 }<RESET>
	 <RESET>
	<BGREEN>+<RESET><BGREEN>int secure_foo(struct user *u)<RESET>
	<BGREEN>+<RESET><BGREEN>{<RESET>
	<BGREEN>+<RESET><BGREEN>foo(u);<RESET>
	<BGREEN>+<RESET><BGREEN>if (!u->is_allowed_foo)<RESET>
	<BGREEN>+<RESET><BGREEN>return;<RESET>
	<BGREEN>+<RESET><BGREEN>}<RESET>
	<BGREEN>+<RESET>
	 int another_function()<RESET>
	 {<RESET>
	 bar();<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success 'detect blocks of moved code' '
	but reset --hard &&
	cat <<-\EOF >lines.txt &&
		long line 1
		long line 2
		long line 3
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
		long line 14
		long line 15
		long line 16
	EOF
	but add lines.txt &&
	but cummit -m "add poetry" &&
	cat <<-\EOF >lines.txt &&
		line 4
		line 5
		line 6
		line 7
		line 8
		line 9
		long line 1
		long line 2
		long line 3
		long line 14
		long line 15
		long line 16
		line 10
		line 11
		line 12
		line 13
	EOF
	test_config color.diff.oldMoved "magenta" &&
	test_config color.diff.newMoved "cyan" &&
	test_config color.diff.oldMovedAlternative "blue" &&
	test_config color.diff.newMovedAlternative "yellow" &&
	test_config color.diff.oldMovedDimmed "normal magenta" &&
	test_config color.diff.newMovedDimmed "normal cyan" &&
	test_config color.diff.oldMovedAlternativeDimmed "normal blue" &&
	test_config color.diff.newMovedAlternativeDimmed "normal yellow" &&
	but diff HEAD --no-renames --color-moved=blocks --color >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,16 +1,16 @@<RESET>
	<MAGENTA>-long line 1<RESET>
	<MAGENTA>-long line 2<RESET>
	<MAGENTA>-long line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	 line 6<RESET>
	 line 7<RESET>
	 line 8<RESET>
	 line 9<RESET>
	<CYAN>+<RESET><CYAN>long line 1<RESET>
	<CYAN>+<RESET><CYAN>long line 2<RESET>
	<CYAN>+<RESET><CYAN>long line 3<RESET>
	<CYAN>+<RESET><CYAN>long line 14<RESET>
	<CYAN>+<RESET><CYAN>long line 15<RESET>
	<CYAN>+<RESET><CYAN>long line 16<RESET>
	 line 10<RESET>
	 line 11<RESET>
	 line 12<RESET>
	 line 13<RESET>
	<MAGENTA>-long line 14<RESET>
	<MAGENTA>-long line 15<RESET>
	<MAGENTA>-long line 16<RESET>
	EOF
	test_cmp expected actual

'

test_expect_success 'detect permutations inside moved code -- dimmed-zebra' '
	# reuse setup from test before!
	test_config color.diff.oldMoved "magenta" &&
	test_config color.diff.newMoved "cyan" &&
	test_config color.diff.oldMovedAlternative "blue" &&
	test_config color.diff.newMovedAlternative "yellow" &&
	test_config color.diff.oldMovedDimmed "normal magenta" &&
	test_config color.diff.newMovedDimmed "normal cyan" &&
	test_config color.diff.oldMovedAlternativeDimmed "normal blue" &&
	test_config color.diff.newMovedAlternativeDimmed "normal yellow" &&
	but diff HEAD --no-renames --color-moved=dimmed-zebra --color >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,16 +1,16 @@<RESET>
	<BMAGENTA>-long line 1<RESET>
	<BMAGENTA>-long line 2<RESET>
	<BMAGENTA>-long line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	 line 6<RESET>
	 line 7<RESET>
	 line 8<RESET>
	 line 9<RESET>
	<BCYAN>+<RESET><BCYAN>long line 1<RESET>
	<BCYAN>+<RESET><BCYAN>long line 2<RESET>
	<CYAN>+<RESET><CYAN>long line 3<RESET>
	<YELLOW>+<RESET><YELLOW>long line 14<RESET>
	<BYELLOW>+<RESET><BYELLOW>long line 15<RESET>
	<BYELLOW>+<RESET><BYELLOW>long line 16<RESET>
	 line 10<RESET>
	 line 11<RESET>
	 line 12<RESET>
	 line 13<RESET>
	<BMAGENTA>-long line 14<RESET>
	<BMAGENTA>-long line 15<RESET>
	<BMAGENTA>-long line 16<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'zebra alternate color is only used when necessary' '
	cat >old.txt <<-\EOF &&
	line 1A should be marked as oldMoved newMovedAlternate
	line 1B should be marked as oldMoved newMovedAlternate
	unchanged
	line 2A should be marked as oldMoved newMovedAlternate
	line 2B should be marked as oldMoved newMovedAlternate
	line 3A should be marked as oldMovedAlternate newMoved
	line 3B should be marked as oldMovedAlternate newMoved
	unchanged
	line 4A should be marked as oldMoved newMovedAlternate
	line 4B should be marked as oldMoved newMovedAlternate
	line 5A should be marked as oldMovedAlternate newMoved
	line 5B should be marked as oldMovedAlternate newMoved
	line 6A should be marked as oldMoved newMoved
	line 6B should be marked as oldMoved newMoved
	EOF
	cat >new.txt <<-\EOF &&
	  line 1A should be marked as oldMoved newMovedAlternate
	  line 1B should be marked as oldMoved newMovedAlternate
	unchanged
	  line 3A should be marked as oldMovedAlternate newMoved
	  line 3B should be marked as oldMovedAlternate newMoved
	  line 2A should be marked as oldMoved newMovedAlternate
	  line 2B should be marked as oldMoved newMovedAlternate
	unchanged
	  line 6A should be marked as oldMoved newMoved
	  line 6B should be marked as oldMoved newMoved
	    line 4A should be marked as oldMoved newMovedAlternate
	    line 4B should be marked as oldMoved newMovedAlternate
	  line 5A should be marked as oldMovedAlternate newMoved
	  line 5B should be marked as oldMovedAlternate newMoved
	EOF
	test_expect_code 1 but diff --no-index --color --color-moved=zebra \
		 --color-moved-ws=allow-indentation-change \
		 old.txt new.txt >output &&
	grep -v index output | test_decode_color >actual &&
	cat >expected <<-\EOF &&
	<BOLD>diff --but a/old.txt b/new.txt<RESET>
	<BOLD>--- a/old.txt<RESET>
	<BOLD>+++ b/new.txt<RESET>
	<CYAN>@@ -1,14 +1,14 @@<RESET>
	<BOLD;MAGENTA>-line 1A should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;MAGENTA>-line 1B should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>  line 1A should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>  line 1B should be marked as oldMoved newMovedAlternate<RESET>
	 unchanged<RESET>
	<BOLD;MAGENTA>-line 2A should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;MAGENTA>-line 2B should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;BLUE>-line 3A should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;BLUE>-line 3B should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>  line 3A should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>  line 3B should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;YELLOW>+<RESET><BOLD;YELLOW>  line 2A should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;YELLOW>+<RESET><BOLD;YELLOW>  line 2B should be marked as oldMoved newMovedAlternate<RESET>
	 unchanged<RESET>
	<BOLD;MAGENTA>-line 4A should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;MAGENTA>-line 4B should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;BLUE>-line 5A should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;BLUE>-line 5B should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;MAGENTA>-line 6A should be marked as oldMoved newMoved<RESET>
	<BOLD;MAGENTA>-line 6B should be marked as oldMoved newMoved<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>  line 6A should be marked as oldMoved newMoved<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>  line 6B should be marked as oldMoved newMoved<RESET>
	<BOLD;YELLOW>+<RESET><BOLD;YELLOW>    line 4A should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;YELLOW>+<RESET><BOLD;YELLOW>    line 4B should be marked as oldMoved newMovedAlternate<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>  line 5A should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>  line 5B should be marked as oldMovedAlternate newMoved<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'short lines of opposite sign do not get marked as moved' '
	cat >old.txt <<-\EOF &&
	this line should be marked as moved
	unchanged
	unchanged
	unchanged
	unchanged
	too short
	this line should be marked as oldMoved newMoved
	this line should be marked as oldMovedAlternate newMoved
	unchanged 1
	unchanged 2
	unchanged 3
	unchanged 4
	this line should be marked as oldMoved newMoved/newMovedAlternate
	EOF
	cat >new.txt <<-\EOF &&
	too short
	unchanged
	unchanged
	this line should be marked as moved
	too short
	unchanged
	unchanged
	this line should be marked as oldMoved newMoved/newMovedAlternate
	unchanged 1
	unchanged 2
	this line should be marked as oldMovedAlternate newMoved
	this line should be marked as oldMoved newMoved/newMovedAlternate
	unchanged 3
	this line should be marked as oldMoved newMoved
	unchanged 4
	EOF
	test_expect_code 1 but diff --no-index --color --color-moved=zebra \
		old.txt new.txt >output && cat output &&
	grep -v index output | test_decode_color >actual &&
	cat >expect <<-\EOF &&
	<BOLD>diff --but a/old.txt b/new.txt<RESET>
	<BOLD>--- a/old.txt<RESET>
	<BOLD>+++ b/new.txt<RESET>
	<CYAN>@@ -1,13 +1,15 @@<RESET>
	<BOLD;MAGENTA>-this line should be marked as moved<RESET>
	<GREEN>+<RESET><GREEN>too short<RESET>
	 unchanged<RESET>
	 unchanged<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>this line should be marked as moved<RESET>
	<GREEN>+<RESET><GREEN>too short<RESET>
	 unchanged<RESET>
	 unchanged<RESET>
	<RED>-too short<RESET>
	<BOLD;MAGENTA>-this line should be marked as oldMoved newMoved<RESET>
	<BOLD;BLUE>-this line should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>this line should be marked as oldMoved newMoved/newMovedAlternate<RESET>
	 unchanged 1<RESET>
	 unchanged 2<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>this line should be marked as oldMovedAlternate newMoved<RESET>
	<BOLD;YELLOW>+<RESET><BOLD;YELLOW>this line should be marked as oldMoved newMoved/newMovedAlternate<RESET>
	 unchanged 3<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>this line should be marked as oldMoved newMoved<RESET>
	 unchanged 4<RESET>
	<BOLD;MAGENTA>-this line should be marked as oldMoved newMoved/newMovedAlternate<RESET>
	EOF
	test_cmp expect actual
'

test_expect_success 'cmd option assumes configured colored-moved' '
	test_config color.diff.oldMoved "magenta" &&
	test_config color.diff.newMoved "cyan" &&
	test_config color.diff.oldMovedAlternative "blue" &&
	test_config color.diff.newMovedAlternative "yellow" &&
	test_config color.diff.oldMovedDimmed "normal magenta" &&
	test_config color.diff.newMovedDimmed "normal cyan" &&
	test_config color.diff.oldMovedAlternativeDimmed "normal blue" &&
	test_config color.diff.newMovedAlternativeDimmed "normal yellow" &&
	test_config diff.colorMoved zebra &&
	but diff HEAD --no-renames --color-moved --color >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,16 +1,16 @@<RESET>
	<MAGENTA>-long line 1<RESET>
	<MAGENTA>-long line 2<RESET>
	<MAGENTA>-long line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	 line 6<RESET>
	 line 7<RESET>
	 line 8<RESET>
	 line 9<RESET>
	<CYAN>+<RESET><CYAN>long line 1<RESET>
	<CYAN>+<RESET><CYAN>long line 2<RESET>
	<CYAN>+<RESET><CYAN>long line 3<RESET>
	<YELLOW>+<RESET><YELLOW>long line 14<RESET>
	<YELLOW>+<RESET><YELLOW>long line 15<RESET>
	<YELLOW>+<RESET><YELLOW>long line 16<RESET>
	 line 10<RESET>
	 line 11<RESET>
	 line 12<RESET>
	 line 13<RESET>
	<MAGENTA>-long line 14<RESET>
	<MAGENTA>-long line 15<RESET>
	<MAGENTA>-long line 16<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'no effect on diff from --color-moved with --word-diff' '
	cat <<-\EOF >text.txt &&
	Lorem Ipsum is simply dummy text of the printing and typesetting industry.
	EOF
	but add text.txt &&
	but cummit -a -m "clean state" &&
	cat <<-\EOF >text.txt &&
	simply Lorem Ipsum dummy is text of the typesetting and printing industry.
	EOF
	but diff --color-moved --word-diff >actual &&
	but diff --word-diff >expect &&
	test_cmp expect actual
'

test_expect_success !SANITIZE_LEAK 'no effect on show from --color-moved with --word-diff' '
	but show --color-moved --word-diff >actual &&
	but show --word-diff >expect &&
	test_cmp expect actual
'

test_expect_success 'set up whitespace tests' '
	but reset --hard &&
	# Note that these lines have no leading or trailing whitespace.
	cat <<-\EOF >lines.txt &&
	line 1
	line 2
	line 3
	line 4
	line 5
	long line 6
	long line 7
	long line 8
	long line 9
	EOF
	but add lines.txt &&
	but cummit -m "add poetry" &&
	but config color.diff.oldMoved "magenta" &&
	but config color.diff.newMoved "cyan"
'

test_expect_success 'move detection ignoring whitespace ' '
	q_to_tab <<-\EOF >lines.txt &&
	Qlong line 6
	Qlong line 7
	Qlong line 8
	Qchanged long line 9
	line 1
	line 2
	line 3
	line 4
	line 5
	EOF
	but diff HEAD --no-renames --color-moved --color >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,9 +1,9 @@<RESET>
	<GREEN>+<RESET>	<GREEN>long line 6<RESET>
	<GREEN>+<RESET>	<GREEN>long line 7<RESET>
	<GREEN>+<RESET>	<GREEN>long line 8<RESET>
	<GREEN>+<RESET>	<GREEN>changed long line 9<RESET>
	 line 1<RESET>
	 line 2<RESET>
	 line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	<RED>-long line 6<RESET>
	<RED>-long line 7<RESET>
	<RED>-long line 8<RESET>
	<RED>-long line 9<RESET>
	EOF
	test_cmp expected actual &&

	but diff HEAD --no-renames --color-moved --color \
		--color-moved-ws=ignore-all-space >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,9 +1,9 @@<RESET>
	<CYAN>+<RESET>	<CYAN>long line 6<RESET>
	<CYAN>+<RESET>	<CYAN>long line 7<RESET>
	<CYAN>+<RESET>	<CYAN>long line 8<RESET>
	<GREEN>+<RESET>	<GREEN>changed long line 9<RESET>
	 line 1<RESET>
	 line 2<RESET>
	 line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	<MAGENTA>-long line 6<RESET>
	<MAGENTA>-long line 7<RESET>
	<MAGENTA>-long line 8<RESET>
	<RED>-long line 9<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'move detection ignoring whitespace changes' '
	but reset --hard &&
	# Lines 6-8 have a space change, but 9 is new whitespace
	q_to_tab <<-\EOF >lines.txt &&
	longQline 6
	longQline 7
	longQline 8
	long liQne 9
	line 1
	line 2
	line 3
	line 4
	line 5
	EOF

	but diff HEAD --no-renames --color-moved --color >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,9 +1,9 @@<RESET>
	<GREEN>+<RESET><GREEN>long	line 6<RESET>
	<GREEN>+<RESET><GREEN>long	line 7<RESET>
	<GREEN>+<RESET><GREEN>long	line 8<RESET>
	<GREEN>+<RESET><GREEN>long li	ne 9<RESET>
	 line 1<RESET>
	 line 2<RESET>
	 line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	<RED>-long line 6<RESET>
	<RED>-long line 7<RESET>
	<RED>-long line 8<RESET>
	<RED>-long line 9<RESET>
	EOF
	test_cmp expected actual &&

	but diff HEAD --no-renames --color-moved --color \
		--color-moved-ws=ignore-space-change >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,9 +1,9 @@<RESET>
	<CYAN>+<RESET><CYAN>long	line 6<RESET>
	<CYAN>+<RESET><CYAN>long	line 7<RESET>
	<CYAN>+<RESET><CYAN>long	line 8<RESET>
	<GREEN>+<RESET><GREEN>long li	ne 9<RESET>
	 line 1<RESET>
	 line 2<RESET>
	 line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	<MAGENTA>-long line 6<RESET>
	<MAGENTA>-long line 7<RESET>
	<MAGENTA>-long line 8<RESET>
	<RED>-long line 9<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'move detection ignoring whitespace at eol' '
	but reset --hard &&
	# Lines 6-9 have new eol whitespace, but 9 also has it in the middle
	q_to_tab <<-\EOF >lines.txt &&
	long line 6Q
	long line 7Q
	long line 8Q
	longQline 9Q
	line 1
	line 2
	line 3
	line 4
	line 5
	EOF

	# avoid cluttering the output with complaints about our eol whitespace
	test_config core.whitespace -blank-at-eol &&

	but diff HEAD --no-renames --color-moved --color >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,9 +1,9 @@<RESET>
	<GREEN>+<RESET><GREEN>long line 6	<RESET>
	<GREEN>+<RESET><GREEN>long line 7	<RESET>
	<GREEN>+<RESET><GREEN>long line 8	<RESET>
	<GREEN>+<RESET><GREEN>long	line 9	<RESET>
	 line 1<RESET>
	 line 2<RESET>
	 line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	<RED>-long line 6<RESET>
	<RED>-long line 7<RESET>
	<RED>-long line 8<RESET>
	<RED>-long line 9<RESET>
	EOF
	test_cmp expected actual &&

	but diff HEAD --no-renames --color-moved --color \
		--color-moved-ws=ignore-space-at-eol >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat <<-\EOF >expected &&
	<BOLD>diff --but a/lines.txt b/lines.txt<RESET>
	<BOLD>--- a/lines.txt<RESET>
	<BOLD>+++ b/lines.txt<RESET>
	<CYAN>@@ -1,9 +1,9 @@<RESET>
	<CYAN>+<RESET><CYAN>long line 6	<RESET>
	<CYAN>+<RESET><CYAN>long line 7	<RESET>
	<CYAN>+<RESET><CYAN>long line 8	<RESET>
	<GREEN>+<RESET><GREEN>long	line 9	<RESET>
	 line 1<RESET>
	 line 2<RESET>
	 line 3<RESET>
	 line 4<RESET>
	 line 5<RESET>
	<MAGENTA>-long line 6<RESET>
	<MAGENTA>-long line 7<RESET>
	<MAGENTA>-long line 8<RESET>
	<RED>-long line 9<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'clean up whitespace-test colors' '
	but config --unset color.diff.oldMoved &&
	but config --unset color.diff.newMoved
'

test_expect_success '--color-moved block at end of diff output respects MIN_ALNUM_COUNT' '
	but reset --hard &&
	>bar &&
	cat <<-\EOF >foo &&
	irrelevant_line
	line1
	EOF
	but add foo bar &&
	but cummit -m x &&

	cat <<-\EOF >bar &&
	line1
	EOF
	cat <<-\EOF >foo &&
	irrelevant_line
	EOF

	but diff HEAD --color-moved=zebra --color --no-renames >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat >expected <<-\EOF &&
	<BOLD>diff --but a/bar b/bar<RESET>
	<BOLD>--- a/bar<RESET>
	<BOLD>+++ b/bar<RESET>
	<CYAN>@@ -0,0 +1 @@<RESET>
	<GREEN>+<RESET><GREEN>line1<RESET>
	<BOLD>diff --but a/foo b/foo<RESET>
	<BOLD>--- a/foo<RESET>
	<BOLD>+++ b/foo<RESET>
	<CYAN>@@ -1,2 +1 @@<RESET>
	 irrelevant_line<RESET>
	<RED>-line1<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success '--color-moved respects MIN_ALNUM_COUNT' '
	but reset --hard &&
	cat <<-\EOF >foo &&
	nineteen chars 456789
	irrelevant_line
	twenty chars 234567890
	EOF
	>bar &&
	but add foo bar &&
	but cummit -m x &&

	cat <<-\EOF >foo &&
	irrelevant_line
	EOF
	cat <<-\EOF >bar &&
	twenty chars 234567890
	nineteen chars 456789
	EOF

	but diff HEAD --color-moved=zebra --color --no-renames >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat >expected <<-\EOF &&
	<BOLD>diff --but a/bar b/bar<RESET>
	<BOLD>--- a/bar<RESET>
	<BOLD>+++ b/bar<RESET>
	<CYAN>@@ -0,0 +1,2 @@<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>twenty chars 234567890<RESET>
	<GREEN>+<RESET><GREEN>nineteen chars 456789<RESET>
	<BOLD>diff --but a/foo b/foo<RESET>
	<BOLD>--- a/foo<RESET>
	<BOLD>+++ b/foo<RESET>
	<CYAN>@@ -1,3 +1 @@<RESET>
	<RED>-nineteen chars 456789<RESET>
	 irrelevant_line<RESET>
	<BOLD;MAGENTA>-twenty chars 234567890<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success '--color-moved treats adjacent blocks as separate for MIN_ALNUM_COUNT' '
	but reset --hard &&
	cat <<-\EOF >foo &&
	7charsA
	irrelevant_line
	7charsB
	7charsC
	EOF
	>bar &&
	but add foo bar &&
	but cummit -m x &&

	cat <<-\EOF >foo &&
	irrelevant_line
	EOF
	cat <<-\EOF >bar &&
	7charsB
	7charsC
	7charsA
	EOF

	but diff HEAD --color-moved=zebra --color --no-renames >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat >expected <<-\EOF &&
	<BOLD>diff --but a/bar b/bar<RESET>
	<BOLD>--- a/bar<RESET>
	<BOLD>+++ b/bar<RESET>
	<CYAN>@@ -0,0 +1,3 @@<RESET>
	<GREEN>+<RESET><GREEN>7charsB<RESET>
	<GREEN>+<RESET><GREEN>7charsC<RESET>
	<GREEN>+<RESET><GREEN>7charsA<RESET>
	<BOLD>diff --but a/foo b/foo<RESET>
	<BOLD>--- a/foo<RESET>
	<BOLD>+++ b/foo<RESET>
	<CYAN>@@ -1,4 +1 @@<RESET>
	<RED>-7charsA<RESET>
	 irrelevant_line<RESET>
	<RED>-7charsB<RESET>
	<RED>-7charsC<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success '--color-moved rewinds for MIN_ALNUM_COUNT' '
	but reset --hard &&
	test_write_lines >file \
		A B C one two three four five six seven D E F G H I J &&
	but add file &&
	test_write_lines >file \
		one two A B C D E F G H I J two three four five six seven &&
	but diff --color-moved=zebra -- file &&

	but diff --color-moved=zebra --color -- file >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	cat >expected <<-\EOF &&
	<BOLD>diff --but a/file b/file<RESET>
	<BOLD>--- a/file<RESET>
	<BOLD>+++ b/file<RESET>
	<CYAN>@@ -1,13 +1,8 @@<RESET>
	<GREEN>+<RESET><GREEN>one<RESET>
	<GREEN>+<RESET><GREEN>two<RESET>
	 A<RESET>
	 B<RESET>
	 C<RESET>
	<RED>-one<RESET>
	<BOLD;MAGENTA>-two<RESET>
	<BOLD;MAGENTA>-three<RESET>
	<BOLD;MAGENTA>-four<RESET>
	<BOLD;MAGENTA>-five<RESET>
	<BOLD;MAGENTA>-six<RESET>
	<BOLD;MAGENTA>-seven<RESET>
	 D<RESET>
	 E<RESET>
	 F<RESET>
	<CYAN>@@ -15,3 +10,9 @@<RESET> <RESET>G<RESET>
	 H<RESET>
	 I<RESET>
	 J<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>two<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>three<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>four<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>five<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>six<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>seven<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success !SANITIZE_LEAK 'move detection with submodules' '
	test_create_repo bananas &&
	echo ripe >bananas/recipe &&
	but -C bananas add recipe &&
	test_cummit fruit &&
	test_cummit -C bananas recipe &&
	but submodule add ./bananas &&
	but add bananas &&
	but cummit -a -m "bananas are like a heavy library?" &&
	echo foul >bananas/recipe &&
	echo ripe >fruit.t &&

	but diff --submodule=diff --color-moved --color >actual &&

	# no move detection as the moved line is across repository boundaries.
	test_decode_color <actual >decoded_actual &&
	! grep BGREEN decoded_actual &&
	! grep BRED decoded_actual &&

	# nor did we mess with it another way
	but diff --submodule=diff --color >expect.raw &&
	test_decode_color <expect.raw >expect &&
	test_cmp expect decoded_actual &&
	rm -rf bananas &&
	but submodule deinit bananas
'

test_expect_success 'only move detection ignores white spaces' '
	but reset --hard &&
	q_to_tab <<-\EOF >text.txt &&
		a long line to exceed per-line minimum
		another long line to exceed per-line minimum
		original file
	EOF
	but add text.txt &&
	but cummit -m "add text" &&
	q_to_tab <<-\EOF >text.txt &&
		Qa long line to exceed per-line minimum
		Qanother long line to exceed per-line minimum
		new file
	EOF

	# Make sure we get a different diff using -w
	but diff --color --color-moved -w >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	q_to_tab <<-\EOF >expected &&
	<BOLD>diff --but a/text.txt b/text.txt<RESET>
	<BOLD>--- a/text.txt<RESET>
	<BOLD>+++ b/text.txt<RESET>
	<CYAN>@@ -1,3 +1,3 @@<RESET>
	 Qa long line to exceed per-line minimum<RESET>
	 Qanother long line to exceed per-line minimum<RESET>
	<RED>-original file<RESET>
	<GREEN>+<RESET><GREEN>new file<RESET>
	EOF
	test_cmp expected actual &&

	# And now ignoring white space only in the move detection
	but diff --color --color-moved \
		--color-moved-ws=ignore-all-space,ignore-space-change,ignore-space-at-eol >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&
	q_to_tab <<-\EOF >expected &&
	<BOLD>diff --but a/text.txt b/text.txt<RESET>
	<BOLD>--- a/text.txt<RESET>
	<BOLD>+++ b/text.txt<RESET>
	<CYAN>@@ -1,3 +1,3 @@<RESET>
	<BOLD;MAGENTA>-a long line to exceed per-line minimum<RESET>
	<BOLD;MAGENTA>-another long line to exceed per-line minimum<RESET>
	<RED>-original file<RESET>
	<BOLD;CYAN>+<RESET>Q<BOLD;CYAN>a long line to exceed per-line minimum<RESET>
	<BOLD;CYAN>+<RESET>Q<BOLD;CYAN>another long line to exceed per-line minimum<RESET>
	<GREEN>+<RESET><GREEN>new file<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'compare whitespace delta across moved blocks' '

	but reset --hard &&
	q_to_tab <<-\EOF >text.txt &&
	QIndented
	QText across
	Qsome lines
	QBut! <- this stands out
	QAdjusting with
	QQdifferent starting
	Qwhite spaces
	QAnother outlier
	QQQIndented
	QQQText across
	QQQfive lines
	QQQthat has similar lines
	QQQto previous blocks, but with different indent
	QQQYetQAnotherQoutlierQ
	QLine with internal w h i t e s p a c e change
	EOF

	but add text.txt &&
	but cummit -m "add text.txt" &&

	q_to_tab <<-\EOF >text.txt &&
	QQIndented
	QQText across
	QQsome lines
	QQQBut! <- this stands out
	Adjusting with
	Qdifferent starting
	white spaces
	AnotherQoutlier
	QQIndented
	QQText across
	QQfive lines
	QQthat has similar lines
	QQto previous blocks, but with different indent
	QQYetQAnotherQoutlier
	QLine with internal whitespace change
	EOF

	but diff --color --color-moved --color-moved-ws=allow-indentation-change >actual.raw &&
	grep -v "index" actual.raw | test_decode_color >actual &&

	q_to_tab <<-\EOF >expected &&
		<BOLD>diff --but a/text.txt b/text.txt<RESET>
		<BOLD>--- a/text.txt<RESET>
		<BOLD>+++ b/text.txt<RESET>
		<CYAN>@@ -1,15 +1,15 @@<RESET>
		<BOLD;MAGENTA>-QIndented<RESET>
		<BOLD;MAGENTA>-QText across<RESET>
		<BOLD;MAGENTA>-Qsome lines<RESET>
		<RED>-QBut! <- this stands out<RESET>
		<BOLD;MAGENTA>-QAdjusting with<RESET>
		<BOLD;MAGENTA>-QQdifferent starting<RESET>
		<BOLD;MAGENTA>-Qwhite spaces<RESET>
		<RED>-QAnother outlier<RESET>
		<BOLD;MAGENTA>-QQQIndented<RESET>
		<BOLD;MAGENTA>-QQQText across<RESET>
		<BOLD;MAGENTA>-QQQfive lines<RESET>
		<BOLD;MAGENTA>-QQQthat has similar lines<RESET>
		<BOLD;MAGENTA>-QQQto previous blocks, but with different indent<RESET>
		<RED>-QQQYetQAnotherQoutlierQ<RESET>
		<RED>-QLine with internal w h i t e s p a c e change<RESET>
		<BOLD;CYAN>+<RESET>QQ<BOLD;CYAN>Indented<RESET>
		<BOLD;CYAN>+<RESET>QQ<BOLD;CYAN>Text across<RESET>
		<BOLD;CYAN>+<RESET>QQ<BOLD;CYAN>some lines<RESET>
		<GREEN>+<RESET>QQQ<GREEN>But! <- this stands out<RESET>
		<BOLD;CYAN>+<RESET><BOLD;CYAN>Adjusting with<RESET>
		<BOLD;CYAN>+<RESET>Q<BOLD;CYAN>different starting<RESET>
		<BOLD;CYAN>+<RESET><BOLD;CYAN>white spaces<RESET>
		<GREEN>+<RESET><GREEN>AnotherQoutlier<RESET>
		<BOLD;CYAN>+<RESET>QQ<BOLD;CYAN>Indented<RESET>
		<BOLD;CYAN>+<RESET>QQ<BOLD;CYAN>Text across<RESET>
		<BOLD;CYAN>+<RESET>QQ<BOLD;CYAN>five lines<RESET>
		<BOLD;CYAN>+<RESET>QQ<BOLD;CYAN>that has similar lines<RESET>
		<BOLD;CYAN>+<RESET>QQ<BOLD;CYAN>to previous blocks, but with different indent<RESET>
		<GREEN>+<RESET>QQ<GREEN>YetQAnotherQoutlier<RESET>
		<GREEN>+<RESET>Q<GREEN>Line with internal whitespace change<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success 'bogus settings in move detection erroring out' '
	test_must_fail but diff --color-moved=bogus 2>err &&
	test_i18ngrep "must be one of" err &&
	test_i18ngrep bogus err &&

	test_must_fail but -c diff.colormoved=bogus diff 2>err &&
	test_i18ngrep "must be one of" err &&
	test_i18ngrep "from command-line config" err &&

	test_must_fail but diff --color-moved-ws=bogus 2>err &&
	test_i18ngrep "possible values" err &&
	test_i18ngrep bogus err &&

	test_must_fail but -c diff.colormovedws=bogus diff 2>err &&
	test_i18ngrep "possible values" err &&
	test_i18ngrep "from command-line config" err
'

test_expect_success 'compare whitespace delta incompatible with other space options' '
	test_must_fail but diff \
		--color-moved-ws=allow-indentation-change,ignore-all-space \
		2>err &&
	test_i18ngrep allow-indentation-change err
'

EMPTY=''
test_expect_success 'compare mixed whitespace delta across moved blocks' '

	but reset --hard &&
	tr "^|Q_" "\f\v\t " <<-EOF >text.txt &&
	^__
	|____too short without
	^
	___being grouped across blank line
	${EMPTY}
	context
	lines
	to
	anchor
	____Indented text to
	_Q____be further indented by four spaces across
	____Qseveral lines
	QQ____These two lines have had their
	____indentation reduced by four spaces
	Qdifferent indentation change
	____too short
	EOF

	but add text.txt &&
	but cummit -m "add text.txt" &&

	tr "^|Q_" "\f\v\t " <<-EOF >text.txt &&
	context
	lines
	to
	anchor
	QIndented text to
	QQbe further indented by four spaces across
	Q____several lines
	${EMPTY}
	QQtoo short without
	${EMPTY}
	^Q_______being grouped across blank line
	${EMPTY}
	Q_QThese two lines have had their
	indentation reduced by four spaces
	QQdifferent indentation change
	__Qtoo short
	EOF

	but -c color.diff.whitespace="normal red" \
		-c core.whitespace=space-before-tab \
		diff --color --color-moved --ws-error-highlight=all \
		--color-moved-ws=allow-indentation-change >actual.raw &&
	grep -v "index" actual.raw | tr "\f\v" "^|" | test_decode_color >actual &&

	cat <<-\EOF >expected &&
	<BOLD>diff --but a/text.txt b/text.txt<RESET>
	<BOLD>--- a/text.txt<RESET>
	<BOLD>+++ b/text.txt<RESET>
	<CYAN>@@ -1,16 +1,16 @@<RESET>
	<BOLD;MAGENTA>-<RESET><BOLD;MAGENTA>^<RESET><BRED>  <RESET>
	<BOLD;MAGENTA>-<RESET><BOLD;MAGENTA>|    too short without<RESET>
	<BOLD;MAGENTA>-<RESET><BOLD;MAGENTA>^<RESET>
	<BOLD;MAGENTA>-<RESET><BOLD;MAGENTA>   being grouped across blank line<RESET>
	<BOLD;MAGENTA>-<RESET>
	 <RESET>context<RESET>
	 <RESET>lines<RESET>
	 <RESET>to<RESET>
	 <RESET>anchor<RESET>
	<BOLD;MAGENTA>-<RESET><BOLD;MAGENTA>    Indented text to<RESET>
	<BOLD;MAGENTA>-<RESET><BRED> <RESET>	<BOLD;MAGENTA>    be further indented by four spaces across<RESET>
	<BOLD;MAGENTA>-<RESET><BRED>    <RESET>	<BOLD;MAGENTA>several lines<RESET>
	<BOLD;BLUE>-<RESET>		<BOLD;BLUE>    These two lines have had their<RESET>
	<BOLD;BLUE>-<RESET><BOLD;BLUE>    indentation reduced by four spaces<RESET>
	<BOLD;MAGENTA>-<RESET>	<BOLD;MAGENTA>different indentation change<RESET>
	<RED>-<RESET><RED>    too short<RESET>
	<BOLD;CYAN>+<RESET>	<BOLD;CYAN>Indented text to<RESET>
	<BOLD;CYAN>+<RESET>		<BOLD;CYAN>be further indented by four spaces across<RESET>
	<BOLD;CYAN>+<RESET>	<BOLD;CYAN>    several lines<RESET>
	<BOLD;YELLOW>+<RESET>
	<BOLD;YELLOW>+<RESET>		<BOLD;YELLOW>too short without<RESET>
	<BOLD;YELLOW>+<RESET>
	<BOLD;YELLOW>+<RESET><BOLD;YELLOW>^	       being grouped across blank line<RESET>
	<BOLD;YELLOW>+<RESET>
	<BOLD;CYAN>+<RESET>	<BRED> <RESET>	<BOLD;CYAN>These two lines have had their<RESET>
	<BOLD;CYAN>+<RESET><BOLD;CYAN>indentation reduced by four spaces<RESET>
	<BOLD;YELLOW>+<RESET>		<BOLD;YELLOW>different indentation change<RESET>
	<GREEN>+<RESET><BRED>  <RESET>	<GREEN>too short<RESET>
	EOF

	test_cmp expected actual
'

test_expect_success 'combine --ignore-blank-lines with --function-context' '
	test_write_lines 1 "" 2 3 4 5 >a &&
	test_write_lines 1    2 3 4   >b &&
	test_must_fail but diff --no-index \
		--ignore-blank-lines --function-context a b >actual.raw &&
	sed -n "/@@/,\$p" <actual.raw >actual &&
	cat <<-\EOF >expect &&
	@@ -1,6 +1,4 @@
	 1
	-
	 2
	 3
	 4
	-5
	EOF
	test_cmp expect actual
'

test_expect_success 'combine --ignore-blank-lines with --function-context 2' '
	test_write_lines    a b c "" function 1 2 3 4 5 "" 6 7 8 9 >a &&
	test_write_lines "" a b c "" function 1 2 3 4 5    6 7 8   >b &&
	test_must_fail but diff --no-index \
		--ignore-blank-lines --function-context a b >actual.raw &&
	sed -n "/@@/,\$p" <actual.raw >actual &&
	cat <<-\EOF >expect &&
	@@ -5,11 +6,9 @@ c
	 function
	 1
	 2
	 3
	 4
	 5
	-
	 6
	 7
	 8
	-9
	EOF
	test_cmp expect actual
'

test_done
