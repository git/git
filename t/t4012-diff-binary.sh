#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='Binary diff and apply
'

. ./test-lib.sh

cat >expect.binary-numstat <<\EOF
1	1	a
-	-	b
1	1	c
-	-	d
EOF

test_expect_success 'prepare repository' '
	echo AIT >a && echo BIT >b && echo CIT >c && echo DIT >d &&
	git update-index --add a b c d &&
	echo git >a &&
	cat "$TEST_DIRECTORY"/test-binary-1.png >b &&
	echo git >c &&
	cat b b >d
'

cat > expected <<\EOF
 a |    2 +-
 b |  Bin
 c |    2 +-
 d |  Bin
 4 files changed, 2 insertions(+), 2 deletions(-)
EOF
test_expect_success 'apply --stat output for binary file change' '
	git diff >diff &&
	git apply --stat --summary <diff >current &&
	test_i18ncmp expected current
'

test_expect_success 'diff --shortstat output for binary file change' '
	tail -n 1 expected >expect &&
	git diff --shortstat >current &&
	test_i18ncmp expect current
'

test_expect_success 'diff --shortstat output for binary file change only' '
	echo " 1 file changed, 0 insertions(+), 0 deletions(-)" >expected &&
	git diff --shortstat -- b >current &&
	test_i18ncmp expected current
'

test_expect_success 'apply --numstat notices binary file change' '
	git diff >diff &&
	git apply --numstat <diff >current &&
	test_cmp expect.binary-numstat current
'

test_expect_success 'apply --numstat understands diff --binary format' '
	git diff --binary >diff &&
	git apply --numstat <diff >current &&
	test_cmp expect.binary-numstat current
'

# apply needs to be able to skip the binary material correctly
# in order to report the line number of a corrupt patch.
test_expect_success C_LOCALE_OUTPUT 'apply detecting corrupt patch correctly' '
	git diff >output &&
	sed -e "s/-CIT/xCIT/" <output >broken &&
	test_must_fail git apply --stat --summary broken 2>detected &&
	detected=$(cat detected) &&
	detected=$(expr "$detected" : "error.*at line \\([0-9]*\\)\$") &&
	detected=$(sed -ne "${detected}p" broken) &&
	test "$detected" = xCIT
'

test_expect_success C_LOCALE_OUTPUT 'apply detecting corrupt patch correctly' '
	git diff --binary | sed -e "s/-CIT/xCIT/" >broken &&
	test_must_fail git apply --stat --summary broken 2>detected &&
	detected=$(cat detected) &&
	detected=$(expr "$detected" : "error.*at line \\([0-9]*\\)\$") &&
	detected=$(sed -ne "${detected}p" broken) &&
	test "$detected" = xCIT
'

test_expect_success 'initial commit' 'git commit -a -m initial'

# Try removal (b), modification (d), and creation (e).
test_expect_success 'diff-index with --binary' '
	echo AIT >a && mv b e && echo CIT >c && cat e >d &&
	git update-index --add --remove a b c d e &&
	tree0=$(git write-tree) &&
	git diff --cached --binary >current &&
	git apply --stat --summary current
'

test_expect_success 'apply binary patch' '
	git reset --hard &&
	git apply --binary --index <current &&
	tree1=$(git write-tree) &&
	test "$tree1" = "$tree0"
'

test_expect_success 'diff --no-index with binary creation' '
	echo Q | q_to_nul >binary &&
	(: hide error code from diff, which just indicates differences
	 git diff --binary --no-index /dev/null binary >current ||
	 true
	) &&
	rm binary &&
	git apply --binary <current &&
	echo Q >expected &&
	nul_to_q <binary >actual &&
	test_cmp expected actual
'

cat >expect <<EOF
 binfile  |   Bin 0 -> 1026 bytes
 textfile | 10000 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
EOF

test_expect_success 'diff --stat with binary files and big change count' '
	printf "\01\00%1024d" 1 >binfile &&
	git add binfile &&
	i=0 &&
	while test $i -lt 10000; do
		echo $i &&
		i=$(($i + 1))
	done >textfile &&
	git add textfile &&
	git diff --cached --stat binfile textfile >output &&
	grep " | " output >actual &&
	test_cmp expect actual
'

test_done
